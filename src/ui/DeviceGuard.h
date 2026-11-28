#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <map>

namespace td
{
/** 3.31.0: fermata never changes the audio device by itself.

    JUCE's AudioDeviceManager listens to every driver type (ASIO, Windows Audio, DirectSound) and, when the device in use is no longer in its type's
    list, closes it and opens the saved or default device in its place (AudioDeviceManager::audioDeviceListChanged, private in JUCE) - with nobody asked.
    Windows Audio's lists change whenever an interface is plugged in or pulled out (WM_DEVICECHANGE). The user asked (4 November 2026): "fermata should
    never try to switch to a new driver without asking the user first ... a new interface or driver is detected, would you like to either keep using the
    current one or change to the new one. If the user selects use the new one, the playback is stopped first. If the user is recording ... the program
    waits until the recording stops and THEN asks the question."

    So the manager is given WRAPPED driver types (GuardedType): every "my list changed" from a real type comes to fermata first (DeviceGuard), which
    passes it on to the manager only when that cannot make JUCE switch - that is, while the device in use is still in its type's list. When the device
    in use has gone, nothing is passed on: fermata says so, and opens the same device again when it comes back. A device plugged in is asked about:
    keep the one in use, or change to the new one (playback stopped first; never during a recording - the question waits until the take has stopped).
    Message thread only. */
class GuardedType final : public juce::AudioIODeviceType, private juce::AudioIODeviceType::Listener
{
public:
    GuardedType (std::unique_ptr<juce::AudioIODeviceType> real, std::function<void (GuardedType&)> changed)
        : juce::AudioIODeviceType (real->getTypeName()), inner (std::move (real)), onChanged (std::move (changed))
    {
        inner->addListener (this);
    }
    ~GuardedType() override { inner->removeListener (this); }

    void scanForDevices() override                                               { inner->scanForDevices(); }
    juce::StringArray getDeviceNames (bool wantInputNames) const override         { return inner->getDeviceNames (wantInputNames); }
    int getDefaultDeviceIndex (bool forInput) const override                      { return inner->getDefaultDeviceIndex (forInput); }
    int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override    { return inner->getIndexOfDevice (d, asInput); }
    bool hasSeparateInputsAndOutputs() const override                             { return inner->hasSeparateInputsAndOutputs(); }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String& in) override { return inner->createDevice (out, in); }

    /** The list change goes on to the AudioDeviceManager (and the Audio settings window's lists). */
    void passOn() { callDeviceChangeListeners(); }
    juce::AudioIODeviceType& real() noexcept { return *inner; }

private:
    void audioDeviceListChanged() override
    {
        // (looked at a moment later, not from inside the real type's own scan or reset)
        juce::WeakReference<GuardedType> self (this);
        juce::MessageManager::callAsync ([self] { if (auto* t = self.get()) { if (t->onChanged) t->onChanged (*t); else t->passOn(); } });
    }
    std::unique_ptr<juce::AudioIODeviceType> inner;
    std::function<void (GuardedType&)> onChanged;
    JUCE_DECLARE_WEAK_REFERENCEABLE (GuardedType)
};

/** The AudioDeviceManager fermata uses: JUCE's own, with every driver type wrapped (see above). */
class GuardedDeviceManager : public juce::AudioDeviceManager
{
public:
    /** Told when a driver type's list of devices changed. Not set: passed straight on (JUCE's own behaviour). */
    std::function<void (GuardedType&)> onListChanged;
    /** (tests) The real types to wrap, instead of JUCE's. */
    std::function<void (juce::OwnedArray<juce::AudioIODeviceType>&)> makeRealTypes;

    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& list) override
    {
        juce::OwnedArray<juce::AudioIODeviceType> real;
        if (makeRealTypes) makeRealTypes (real); else juce::AudioDeviceManager::createAudioDeviceTypes (real);
        for (auto* t : real)
            list.add (new GuardedType (std::unique_ptr<juce::AudioIODeviceType> (t), [this] (GuardedType& g) { if (onListChanged) onListChanged (g); else g.passOn(); }));
        real.clear (false);
    }
};

/** What fermata does when a driver type's list changes (see the top of this file). */
class DeviceGuard : private juce::Timer
{
public:
    struct Hooks
    {
        std::function<bool()> recording;                    // a take is being recorded
        std::function<bool()> playing;
        std::function<void()> stopPlayback;
        std::function<void()> afterChange;                  // the device was changed or opened again: the channels, the saved setup, the engine
        std::function<void()> openAudioSettings;
        /** A question: answer (the index of the button pressed; 0 = the first, also for Escape / the window closed). */
        std::function<void (const juce::String& title, const juce::String& message, const juce::StringArray& buttons, std::function<void (int)> answer)> ask;
        std::function<std::unique_ptr<juce::XmlElement>()> savedSetup;      // the device the user chose last (the settings' "audioDevice")
        std::function<double()> projectRate;                // the rate to open a new device at (0: the device's own)
        std::function<void (const juce::String&)> notice;   // a line in the main window
    };
    Hooks hooks;

    explicit DeviceGuard (GuardedDeviceManager& m) : mgr (m) { mgr.onListChanged = [this] (GuardedType& t) { listChanged (t); }; }
    ~DeviceGuard() override { mgr.onListChanged = nullptr; stopTimer(); }

    /** After the device has been opened at start-up: the lists as they are now (so only what changes from here is new). 'startupError': the saved
        device could not be opened (then it is opened by itself when it appears). */
    void start (const juce::String& startupError = {})
    {
        for (auto* t : mgr.getAvailableDeviceTypes()) known[t->getTypeName()] = namesOf (*t);
        startTimer (500);
        const auto w = wanted();
        if (startupError.isNotEmpty() && w.any())
        {
            deviceGone = true; goneName = w.out.isNotEmpty() ? w.out : w.in;
            log ("The audio device used last could not be opened (" + w.text() + "): " + startupError + ". No other device was opened in its place.");
            startupMessage = "The audio device you used last could not be opened:\n\n    " + w.text() + "\n    (" + startupError.trim() + ")\n\n"
                             "fermata has not opened another device in its place. When this one is plugged in or switched on, fermata opens it by itself. "
                             "Or choose another device in Project > Audio settings.";
            startupMessageMs = juce::Time::getMillisecondCounter() + 2500;      // (once the main window is up)
        }
    }

private:
    struct Names { juce::StringArray in, out; };
    struct Wanted
    {
        juce::String type, in, out;
        bool any() const { return in.isNotEmpty() || out.isNotEmpty(); }
        juce::String text() const { return (out.isNotEmpty() ? out : in) + (in.isNotEmpty() && out.isNotEmpty() && in != out ? " / " + in : juce::String()) + " (" + type + ")"; }
    };
    struct NewDevice { juce::String type, name; bool input = false; };

    static Names namesOf (juce::AudioIODeviceType& t)
    {
        Names n; n.out = t.getDeviceNames (false);
        if (t.hasSeparateInputsAndOutputs()) n.in = t.getDeviceNames (true);
        return n;
    }
    static bool listHas (const juce::StringArray& l, const juce::String& name) { for (auto& s : l) if (s.trim().equalsIgnoreCase (name.trim())) return true; return false; }
    void log (const juce::String& t) const { juce::Logger::writeToLog ("Audio device: " + t); }
    void tell (const juce::String& title, const juce::String& message, bool offerSettings)
    {
        if (! hooks.ask) return;
        juce::StringArray buttons { "OK" };
        if (offerSettings) buttons.add ("Audio settings");
        hooks.ask (title, message, buttons, [this, offerSettings] (int b) { if (offerSettings && b == 1 && hooks.openAudioSettings) hooks.openAudioSettings(); });
    }

    /** The device in use - or, with none open (it went, or could not be opened at start-up), the one the user chose last. */
    Wanted wanted() const
    {
        Wanted w;
        if (mgr.getCurrentAudioDevice() != nullptr)
        {
            const auto s = mgr.getAudioDeviceSetup();
            w.type = mgr.getCurrentAudioDeviceType(); w.in = s.inputDeviceName; w.out = s.outputDeviceName;
            return w;
        }
        if (auto x = hooks.savedSetup ? hooks.savedSetup() : nullptr)
        {
            w.type = x->getStringAttribute ("deviceType");
            if (x->getStringAttribute ("audioDeviceName").isNotEmpty()) w.in = w.out = x->getStringAttribute ("audioDeviceName");
            else { w.in = x->getStringAttribute ("audioInputDeviceName"); w.out = x->getStringAttribute ("audioOutputDeviceName"); }
        }
        return w;
    }
    bool present (const Wanted& w, const Names& n) const
    {
        const auto& ins = n.in.isEmpty() ? n.out : n.in;
        return (w.out.isEmpty() || listHas (n.out, w.out)) && (w.in.isEmpty() || listHas (ins, w.in));
    }
    bool isRecording() const { return hooks.recording && hooks.recording(); }

    /** JUCE's own test (AudioDeviceManager::audioDeviceListChanged), on the device that is open: is its name still in its driver type's lists? When it
        is not, JUCE closes it and opens another - so then nothing may be passed on, whichever type's list changed (one device is often listed by
        several types: Windows Audio's three modes and DirectSound). */
    bool listed (juce::AudioIODevice& d) const
    {
        for (auto* t : mgr.getAvailableDeviceTypes())
            if (t->getTypeName() == d.getTypeName())
            {
                for (auto& n : t->getDeviceNames (true))  if (n == d.getName()) return true;
                for (auto& n : t->getDeviceNames (false)) if (n == d.getName()) return true;
                return false;
            }
        return true;                                                // (a type JUCE does not know either: it would not act)
    }

    void listChanged (GuardedType& t)
    {
        const auto type = t.getTypeName();
        const auto now = namesOf (t);
        auto& was = known[type];
        juce::StringArray added, gone;
        for (auto& n : now.out) if (! listHas (was.out, n)) added.add (n);
        for (auto& n : now.in)  if (! listHas (was.in, n) && ! listHas (added, n)) added.add (n);
        for (auto& n : was.out) if (! listHas (now.out, n)) gone.add (n);
        for (auto& n : was.in)  if (! listHas (now.in, n) && ! listHas (gone, n)) gone.add (n);
        was = now;
        if (! added.isEmpty()) log ("plugged in (" + type + "): " + added.joinIntoString (", "));
        if (! gone.isEmpty())  log ("gone (" + type + "): " + gone.joinIntoString (", "));
        auto addPending = [&] { for (auto& n : added) pendingAdd ({ type, n, listHas (now.in, n) && ! listHas (now.out, n) }); };

        if (auto* dev = mgr.getCurrentAudioDevice())
        {
            if (! listed (*dev))
            {   // the device in use has gone: NOT passed on (JUCE would close it and open another device). Said once; opened again when it is back.
                if (! deviceGone)
                {
                    deviceGone = true; goneName = dev->getName();
                    const auto what = wanted().text();
                    const bool rec = isRecording();
                    log ("the device in use has gone: " + what + ". fermata does not change to another device by itself" + (rec ? " (a take is recording: the device is left as it is until it stops)" : ""));
                    if (! rec) mgr.closeAudioDevice();
                    tell ("Audio device", "The audio device fermata was using has gone:\n\n    " + what + "\n\n"
                          "fermata has not changed to another device. When this one comes back, fermata opens it again by itself"
                          + juce::String (rec ? " once the take being recorded has been stopped (until then it gets no audio)" : "")
                          + ". Or choose another device in Project > Audio settings.", true);
                }
                return;
            }
            if (deviceGone)
            {
                if (dev->getName() == goneName)
                {   // it is back - but this is the dead device left open under a take: opened again (the same device, not a change) once nothing records
                    deviceGone = false;
                    if (isRecording()) { reopenWhenFree = true; log ("the device in use is back: " + goneName + " - opened again once the take has stopped"); }
                    else reopenSaved ("the device in use is back");
                }
                else deviceGone = false;                            // (another device has been chosen meanwhile, in Audio settings: nothing to wait for)
            }
            addPending();
            t.passOn();                                             // (safe: the open device is in its type's list)
            return;
        }
        // nothing open: the device in use went, or could not be opened at start-up. Any change of any list may be it coming back - an ASIO driver
        // is always listed (its list is the installed drivers), so plugging its interface in changes only the Windows Audio lists: the saved device
        // is tried again first, before anything is asked about the devices that came with it
        if (deviceGone && ! isRecording() && reopenSaved ("the device used last is back"))
        {
            t.passOn();
            return;
        }
        addPending();
        t.passOn();                                                 // (safe: with nothing open JUCE changes nothing)
    }

    void pendingAdd (const NewDevice& d)
    {
        if (juce::Time::getMillisecondCounter() < quietUntilMs) return;      // (the devices that came with the one just opened again)
        const auto w = wanted();
        if (d.name == w.out || d.name == w.in) return;          // (the device in use coming back is not new)
        for (auto& p : pending) if (p.name == d.name && p.type == d.type) return;
        pending.push_back (d);
        questionDueMs = juce::Time::getMillisecondCounter() + 1500;     // (an interface brings several devices, one type after another: asked once, together)
    }

    /** The device the user chose last (the saved setup), opened again - if its type lists it. True when it is open. */
    bool reopenSaved (const juce::String& why)
    {
        auto x = hooks.savedSetup ? hooks.savedSetup() : nullptr;
        if (x == nullptr) return false;
        Wanted w;
        w.type = x->getStringAttribute ("deviceType");
        if (x->getStringAttribute ("audioDeviceName").isNotEmpty()) w.in = w.out = x->getStringAttribute ("audioDeviceName");
        else { w.in = x->getStringAttribute ("audioInputDeviceName"); w.out = x->getStringAttribute ("audioOutputDeviceName"); }
        if (! w.any()) return false;
        if (auto* t = findType (w.type); t == nullptr || ! present (w, namesOf (*t))) return false;     // (not there yet)
        mgr.closeAudioDevice();
        const auto err = mgr.initialise (256, 256, x.get(), false);
        if (err.isNotEmpty() || mgr.getCurrentAudioDevice() == nullptr)
        {
            log (why + "? " + w.text() + " could not be opened yet" + (err.isNotEmpty() ? ": " + err : juce::String()));
            mgr.closeAudioDevice();
            deviceGone = true;
            return false;
        }
        deviceGone = false; goneName.clear();
        pending.clear(); quietUntilMs = juce::Time::getMillisecondCounter() + 3000;     // (its own other devices are not new)
        log (why + ": " + w.text() + " opened again");
        if (hooks.afterChange) hooks.afterChange();
        if (hooks.notice) hooks.notice ("The audio device is back: " + w.text() + " - opened again.");
        return true;
    }

    void timerCallback() override
    {
        auto* dev = mgr.getCurrentAudioDevice();
        if (startupMessage.isNotEmpty() && juce::Time::getMillisecondCounter() >= startupMessageMs)
        {
            const auto m = startupMessage; startupMessage.clear();
            if (deviceGone && dev == nullptr) tell ("Audio device", m, true);   // (not if a device has been opened meanwhile)
        }
        if (deviceGone && dev != nullptr && ! isRecording())
        {
            if (! listed (*dev)) { log ("the take has stopped: the device that went is closed"); mgr.closeAudioDevice(); dev = nullptr; }
            else if (dev->getName() != goneName) deviceGone = false;           // (another device chosen in Audio settings)
        }
        if (reopenWhenFree && ! isRecording()) { reopenWhenFree = false; reopenSaved ("the device in use is back"); }
        if (pending.empty() || asking || isRecording() || juce::Time::getMillisecondCounter() < questionDueMs) return;
        askAboutNew();
    }

    /** The best of the new devices to change to: an ASIO driver first, then one of the type in use, then Windows Audio, then the rest. */
    bool candidate (NewDevice& out, juce::String& in) const
    {
        const auto cur = wanted().type;
        int best = 99;
        for (auto& p : pending)
        {
            const int rank = p.type == "ASIO" ? 0 : p.type == cur ? 1 : p.type == "Windows Audio" ? 2 : 3;
            if (rank < best && ! p.input) { best = rank; out = p; }
        }
        if (best == 99) return false;
        in.clear();
        if (auto* t = findType (out.type); t != nullptr && t->hasSeparateInputsAndOutputs())
        {
            for (auto& p : pending) if (p.type == out.type && p.input) { in = p.name; break; }
        }
        else in = out.name;
        return true;
    }
    juce::AudioIODeviceType* findType (const juce::String& name) const
    {
        for (auto* t : mgr.getAvailableDeviceTypes()) if (t->getTypeName() == name) return t;
        return nullptr;
    }

    void askAboutNew()
    {
        // only what is still there (and not the device in use)
        for (auto it = pending.begin(); it != pending.end();)
        {
            auto* t = findType (it->type);
            const bool still = t != nullptr && (listHas (t->getDeviceNames (false), it->name) || (t->hasSeparateInputsAndOutputs() && listHas (t->getDeviceNames (true), it->name)));
            if (! still) it = pending.erase (it); else ++it;
        }
        if (pending.empty() || ! hooks.ask) { pending.clear(); return; }
        // the names, once each (the three Windows Audio modes and DirectSound list the same devices), with the type they will be opened as
        NewDevice cand; juce::String candIn;
        const bool canChange = candidate (cand, candIn);
        juce::StringArray lines;
        for (auto& p : pending)
        {
            bool dup = false; for (auto& l : lines) if (l.startsWith ("    " + p.name + "  (")) dup = true;
            if (! dup) lines.add ("    " + p.name + "  (" + p.type + (p.input ? ", input" : "") + ")");
        }
        const auto w = wanted();
        const bool inUse = mgr.getCurrentAudioDevice() != nullptr;
        juce::String msg = "A new audio interface or driver has been found:\n\n" + lines.joinIntoString ("\n") + "\n\n";
        msg << (inUse ? "fermata is using " + w.text() + ", and goes on using it unless you change." : juce::String ("No audio device is open at the moment."));
        if (canChange && hooks.playing && hooks.playing()) msg << "\n\nChanging stops playback first.";
        juce::StringArray buttons { inUse ? "Keep using " + (w.out.isNotEmpty() ? w.out : w.in) : juce::String ("Not now") };
        const juce::String changeText = canChange ? "Change to " + cand.name : juce::String();
        if (canChange) buttons.add (changeText);
        buttons.add ("Choose in Audio settings");
        log ("asked about the new device(s): " + lines.joinIntoString (";").trim());
        asking = true;
        pending.clear();
        hooks.ask ("New audio device", msg, buttons, [this, canChange, cand, candIn] (int b)
        {
            asking = false;
            if (b == 0) { log ("kept the device in use"); return; }
            if (canChange && b == 1) { changeTo (cand, candIn); return; }
            log ("Audio settings opened to choose a device");
            if (hooks.openAudioSettings) hooks.openAudioSettings();
        });
    }

    void changeTo (const NewDevice& d, const juce::String& inName)
    {
        if (isRecording()) { log ("not changed: a take is recording"); if (hooks.notice) hooks.notice ("A take is recording: the audio device was not changed."); return; }
        if (hooks.stopPlayback) hooks.stopPlayback();
        auto previous = hooks.savedSetup ? hooks.savedSetup() : nullptr;
        juce::XmlElement x ("DEVICESETUP");
        x.setAttribute ("deviceType", d.type);
        if (inName == d.name) x.setAttribute ("audioDeviceName", d.name);
        else { x.setAttribute ("audioOutputDeviceName", d.name); x.setAttribute ("audioInputDeviceName", inName); }
        const double rate = hooks.projectRate ? hooks.projectRate() : 0.0;
        if (rate > 0.0) x.setAttribute ("audioDeviceRate", rate);
        mgr.closeAudioDevice();
        const auto err = mgr.initialise (256, 256, &x, false);
        if (err.isEmpty() && mgr.getCurrentAudioDevice() != nullptr)
        {
            deviceGone = false; goneName.clear();
            log ("changed to " + d.name + " (" + d.type + "), as asked");
            if (hooks.afterChange) hooks.afterChange();
            if (hooks.notice) hooks.notice ("The audio device is now " + d.name + " (" + d.type + ").");
            return;
        }
        log ("could not change to " + d.name + " (" + d.type + "): " + err + " - the device used before is opened again");
        if (previous != nullptr) { mgr.closeAudioDevice(); mgr.initialise (256, 256, previous.get(), false); }
        if (hooks.afterChange) hooks.afterChange();
        tell ("Audio device", d.name + " (" + d.type + ") could not be opened:\n\n    " + (err.isNotEmpty() ? err.trim() : juce::String ("it did not start")) + "\n\n"
              "fermata has gone back to the device it was using.", true);
    }

    GuardedDeviceManager& mgr;
    std::map<juce::String, Names> known;                        // per driver type: the devices last seen
    std::vector<NewDevice> pending;                             // devices plugged in, not yet asked about
    juce::uint32 questionDueMs = 0;
    bool asking = false, deviceGone = false, reopenWhenFree = false;
    juce::String goneName;                                      // (the open device that went, by name)
    juce::uint32 quietUntilMs = 0;
    juce::String startupMessage; juce::uint32 startupMessageMs = 0;
};
} // namespace td
