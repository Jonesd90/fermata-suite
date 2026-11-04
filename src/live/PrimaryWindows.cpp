// fermata:Live 0.3.0: the Primary's smaller windows - Audio settings (the device, the drives, the pre-take), Preamps (the Merging devices, found
// with ANEMAN), Project Designer (the tracks), Bounce out (a stereo mix of a take, whole or between marks 1 and 2) and the log.
#include "LiveMainWindow.h"
#include "../core/Bounce.h"
#include "../core/Playback.h"

namespace td::live
{
static void styleCaption (juce::Label& l, const juce::String& t, bool bold = false)
{
    l.setText (t, juce::dontSendNotification);
    l.setFont (juce::FontOptions (bold ? 13.0f : 12.5f, bold ? juce::Font::bold : juce::Font::plain));
    l.setColour (juce::Label::textColourId, bold ? theme::text : theme::dimText);
}

//==============================================================================================================================================
class AudioSettingsPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit AudioSettingsPanel (PrimaryApp& a) : app (a)
    {
        for (auto* l : { &lType, &lDevice, &lRate, &lBuffer, &lBackups, &lPre, &lBuf, &status, &sessionHelp, &driveTitle, &sessionTitle, &deviceTitle })
            addAndMakeVisible (l);
        styleCaption (deviceTitle, "THE AUDIO DEVICE (opened by the recorder)", true);
        styleCaption (lType, "Driver type"); styleCaption (lDevice, "Device"); styleCaption (lRate, "Sample rate"); styleCaption (lBuffer, "Buffer");
        styleCaption (driveTitle, "THE DRIVES", true);
        styleCaption (lBackups, "Backup drives (one folder per line). The primary drive is the project's own; every drive gets the whole recording.");
        styleCaption (sessionTitle, "THE SESSION", true);
        styleCaption (lPre, "Pre-take (seconds a take reaches back before Record, written to the drives - up to 300)");
        styleCaption (lBuf, "Memory buffer (seconds a drive may stall before it gets a gap: 30 - 60)");
        styleCaption (sessionHelp, "Carry on the last session: after the PC or fermata:Live stopped in the middle of a session, this carries the same session on "
                                   "(the timeline continues from the time of day, the stretch missed becomes a gap).");
        sessionHelp.setMinimumHorizontalScale (1.0f); sessionHelp.setJustificationType (juce::Justification::topLeft);
        status.setFont (juce::FontOptions (13.0f, juce::Font::bold)); status.setColour (juce::Label::textColourId, juce::Colour (0xff8fe0a0));
        for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &deviceBox, &rateBox, &bufferBox, &openBtn, &closeBtn, &refreshBtn, &backups, &addBackup, &clearBackups, &preTake, &bufSecs, &resumeBtn })
            addAndMakeVisible (c);
        typeBox.onChange = [this] { fillDevices(); };
        int id = 1;
        for (auto r : Project::kProjectRates) rateBox.addItem (juce::String (r / 1000.0, 1) + " kHz", id++);
        rateBox.setSelectedItemIndex (1, juce::dontSendNotification);
        bufferBox.addItem ("Largest the driver offers (the safest)", 1);
        for (int b : { 256, 512, 1024, 2048, 4096, 8192 }) bufferBox.addItem (juce::String (b) + " samples", b);
        bufferBox.setSelectedId (1, juce::dontSendNotification);
        openBtn.onClick = [this]
        {
            if (deviceBox.getText().isEmpty()) { showError ("Audio settings", "Choose a device first."); return; }
            if (app.isRecording()) { showError ("Audio settings", "A take is recording: Stop it first."); return; }
            if (app.model.sessionOn) { showError ("Audio settings", "The session is on (the pre-take is being written): opening the device again would end it. End the session first (the Pre-take square in the main window)."); return; }
            app.openDevice (typeBox.getText(), deviceBox.getText(), Project::kProjectRates[(size_t) juce::jlimit (0, 3, rateBox.getSelectedItemIndex())], bufferBox.getSelectedId() == 1 ? 0 : bufferBox.getSelectedId());
        };
        closeBtn.onClick = [this] { if (app.model.sessionOn) showError ("Audio settings", "End the session first (the Pre-take square in the main window)."); else app.closeDevice(); };
        refreshBtn.onClick = [this] { app.askForDevices(); };
        backups.setMultiLine (true); backups.setReturnKeyStartsNewLine (true);
        backups.setText (app.backupFolders().joinIntoString ("\n"), false);
        backups.onTextChange = [this] { juce::StringArray a; a.addLines (backups.getText()); app.setBackupFolders (a); };
        addBackup.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a folder on a backup drive", juce::File());
            juce::Component::SafePointer<AudioSettingsPanel> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                safe->backups.setText ((safe->backups.getText().trim() + "\n" + fc.getResult().getFullPathName()).trim(), true);
            });
        };
        clearBackups.onClick = [this] { backups.setText ({}, true); };
        preTake.setText (juce::String (app.preTakeSeconds()), false);
        preTake.setInputRestrictions (3, "0123456789");
        preTake.onTextChange = [this] { app.setPreTakeSeconds (preTake.getText().getIntValue()); };
        bufSecs.setText (juce::String (app.bufferSeconds()), false);
        bufSecs.setInputRestrictions (2, "0123456789");
        bufSecs.onFocusLost = [this] { app.setBufferSeconds (bufSecs.getText().getIntValue()); bufSecs.setText (juce::String (app.bufferSeconds()), false); };
        resumeBtn.onClick = [this] { app.startSession (true); };
        app.addChangeListener (this);
        app.askForDevices();
        refresh();
        startTimer (1000);
    }
    ~AudioSettingsPanel() override { app.removeChangeListener (this); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        deviceTitle.setBounds (r.removeFromTop (22));
        auto row = [&] (juce::Label& l, juce::Component& c) { auto x = r.removeFromTop (28); l.setBounds (x.removeFromLeft (110)); c.setBounds (x.reduced (0, 2)); r.removeFromTop (4); };
        row (lType, typeBox); row (lDevice, deviceBox); row (lRate, rateBox); row (lBuffer, bufferBox);
        {
            auto x = r.removeFromTop (30);
            openBtn.setBounds (x.removeFromLeft (130)); x.removeFromLeft (8);
            closeBtn.setBounds (x.removeFromLeft (130)); x.removeFromLeft (8);
            refreshBtn.setBounds (x.removeFromLeft (150));
        }
        r.removeFromTop (4);
        status.setBounds (r.removeFromTop (22));
        r.removeFromTop (12);
        driveTitle.setBounds (r.removeFromTop (22));
        lBackups.setBounds (r.removeFromTop (20));
        {
            auto x = r.removeFromTop (70);
            auto b = x.removeFromRight (130);
            backups.setBounds (x.withTrimmedRight (8));
            addBackup.setBounds (b.removeFromTop (30)); b.removeFromTop (6); clearBackups.setBounds (b.removeFromTop (30));
        }
        r.removeFromTop (12);
        sessionTitle.setBounds (r.removeFromTop (22));
        { auto x = r.removeFromTop (28); preTake.setBounds (x.removeFromRight (70).reduced (0, 2)); lPre.setBounds (x); }
        { auto x = r.removeFromTop (28); bufSecs.setBounds (x.removeFromRight (70).reduced (0, 2)); lBuf.setBounds (x); }
        r.removeFromTop (8);
        resumeBtn.setBounds (r.removeFromTop (30).removeFromLeft (220));
        r.removeFromTop (4);
        sessionHelp.setBounds (r.removeFromTop (48));
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override { refresh(); }
    void refresh()
    {
        if (app.devicesInfo.isObject() && ! (app.devicesInfo == shownInfo))
        {
            shownInfo = app.devicesInfo;
            const auto want = typeBox.getText().isNotEmpty() ? typeBox.getText() : app.settings->getValue ("deviceType", "ASIO");
            typeBox.clear (juce::dontSendNotification);
            int id = 1;
            if (auto* types = app.devicesInfo["types"].getArray()) for (auto& t : *types) typeBox.addItem (t["type"].toString(), id++);
            for (int i = 0; i < typeBox.getNumItems(); ++i) if (typeBox.getItemText (i) == want) typeBox.setSelectedItemIndex (i, juce::dontSendNotification);
            if (typeBox.getSelectedId() == 0 && typeBox.getNumItems() > 0) typeBox.setSelectedItemIndex (0, juce::dontSendNotification);
            fillDevices();
            const double rate = app.hasProject() && app.project.sampleRate > 0 ? app.project.sampleRate : app.settings->getDoubleValue ("deviceRate", 48000.0);
            for (int i = 0; i < 4; ++i) if (std::abs (Project::kProjectRates[(size_t) i] - rate) < 0.5) rateBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        status.setText (app.deviceSummary(), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, app.model.deviceRunning ? juce::Colour (0xff8fe0a0) : juce::Colour (0xffffc070));
        const bool on = app.model.sessionOn;
        openBtn.setEnabled (! on); closeBtn.setEnabled (! on);           // (either would end the session: End it first)
        preTake.setEnabled (! on); bufSecs.setEnabled (! on); backups.setReadOnly (on); addBackup.setEnabled (! on); clearBackups.setEnabled (! on);
        resumeBtn.setEnabled (! on && app.model.deviceRunning && app.hasProject());
        if (app.hasProject() && app.project.sampleRate > 0)
        {
            const bool lock = app.project.hasTakes();
            for (int i = 0; i < 4; ++i) rateBox.setItemEnabled (i + 1, ! lock || std::abs (Project::kProjectRates[(size_t) i] - app.project.sampleRate) < 0.5);
        }
    }
    void fillDevices()
    {
        const auto want = deviceBox.getText().isNotEmpty() ? deviceBox.getText() : app.settings->getValue ("deviceName");
        deviceBox.clear (juce::dontSendNotification);
        if (auto* types = app.devicesInfo["types"].getArray())
            for (auto& t : *types)
                if (t["type"].toString() == typeBox.getText())
                    if (auto* names = t["devices"].getArray()) { int id = 1; for (auto& n : *names) deviceBox.addItem (n.toString(), id++); }
        for (int i = 0; i < deviceBox.getNumItems(); ++i) if (deviceBox.getItemText (i) == want) deviceBox.setSelectedItemIndex (i, juce::dontSendNotification);
        if (deviceBox.getSelectedId() == 0 && deviceBox.getNumItems() > 0) deviceBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    PrimaryApp& app;
    juce::var shownInfo;
    juce::Label deviceTitle, lType, lDevice, lRate, lBuffer, status, driveTitle, lBackups, sessionTitle, lPre, lBuf, sessionHelp;
    juce::ComboBox typeBox, deviceBox, rateBox, bufferBox;
    juce::TextButton openBtn { "Open device" }, closeBtn { "Close device" }, refreshBtn { "Look for devices again" }, addBackup { "Add folder..." }, clearBackups { "Clear" },
                     resumeBtn { "Carry on the last session" };
    juce::TextEditor backups, preTake, bufSecs;
    std::unique_ptr<juce::FileChooser> chooser;
};

//==============================================================================================================================================
class PreampsPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit PreampsPanel (PrimaryApp& a) : app (a)
    {
        styleCaption (title, "THE PREAMPS: the Merging devices (Anubis, Hapi, MT48) whose preamps fermata:Live controls, as fermata does", true);
        styleCaption (help, "Find them with ANEMAN (Merging's network manager on this PC) - it says which device feeds which input - or add one by hand: its name, "
                            "its address and the first driver input it feeds. The gain dials and preamp keys appear on those inputs' strips in the main window.");
        help.setJustificationType (juce::Justification::topLeft);
        result.setMultiLine (true); result.setReadOnly (true); result.setCaretVisible (false);
        list.setModel (this); list.setRowHeight (24); list.setColour (juce::ListBox::backgroundColourId, theme::field);
        list.setColour (juce::ListBox::outlineColourId, theme::border); list.setOutlineThickness (1);
        result.setTextToShowWhenEmpty ("What ANEMAN found shows here.", theme::dimText.withAlpha (0.6f));
        for (auto* c : std::initializer_list<juce::Component*> { &title, &help, &aneman, &list, &name, &host, &first, &addBtn, &removeBtn, &result, &lName, &lHost, &lFirst })
            addAndMakeVisible (c);
        styleCaption (lName, "Name"); styleCaption (lHost, "Address (IP)"); styleCaption (lFirst, "First input");
        name.setTextToShowWhenEmpty ("Anubis 1", theme::dimText.withAlpha (0.5f));
        host.setTextToShowWhenEmpty ("192.168.1.50", theme::dimText.withAlpha (0.5f));
        first.setTextToShowWhenEmpty ("1", theme::dimText.withAlpha (0.5f)); first.setInputRestrictions (4, "0123456789");
        aneman.onClick = [this]
        {
            result.setText ("Asking ANEMAN...");
            juce::Component::SafePointer<PreampsPanel> safe (this);
            app.findPreampsWithAneman ([safe] (const juce::String& r) { if (safe != nullptr) { safe->result.setText (r); safe->list.updateContent(); } });
        };
        addBtn.onClick = [this]
        {
            if (name.getText().trim().isEmpty() || host.getText().trim().isEmpty()) { showError ("Preamps", "Type the device's name and address."); return; }
            auto devs = app.project.preampDevices;
            PreampDeviceCfg c; c.name = name.getText().trim(); c.host = host.getText().trim(); c.firstInput = juce::jmax (0, first.getText().getIntValue() - 1);
            devs.push_back (c);
            app.setPreampDevices (devs);
            list.updateContent();
        };
        removeBtn.onClick = [this]
        {
            const int row = list.getSelectedRow();
            auto devs = app.project.preampDevices;
            if (! juce::isPositiveAndBelow (row, (int) devs.size())) return;
            devs.erase (devs.begin() + row);
            app.setPreampDevices (devs);
            list.updateContent();
        };
        startTimer (1000);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (22));
        help.setBounds (r.removeFromTop (48));
        aneman.setBounds (r.removeFromTop (30).removeFromLeft (260));
        r.removeFromTop (8);
        result.setBounds (r.removeFromBottom (110));
        r.removeFromBottom (8);
        {
            auto x = r.removeFromBottom (28);
            lName.setBounds (x.removeFromLeft (44)); name.setBounds (x.removeFromLeft (150).reduced (0, 2)); x.removeFromLeft (8);
            lHost.setBounds (x.removeFromLeft (84)); host.setBounds (x.removeFromLeft (130).reduced (0, 2)); x.removeFromLeft (8);
            lFirst.setBounds (x.removeFromLeft (70)); first.setBounds (x.removeFromLeft (50).reduced (0, 2)); x.removeFromLeft (8);
            addBtn.setBounds (x.removeFromLeft (70)); x.removeFromLeft (6); removeBtn.setBounds (x.removeFromLeft (juce::jmax (130, x.getWidth())));
        }
        r.removeFromBottom (6);
        list.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
    void paintOverChildren (juce::Graphics& g) override
    {
        if (! app.project.preampDevices.empty()) return;                   // (the empty list says what goes in it)
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No preamp devices yet: find them with ANEMAN, or add one below.", list.getBounds().reduced (12), juce::Justification::centred, true);
    }
private:
    int getNumRows() override { return (int) app.project.preampDevices.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) app.project.preampDevices.size())) return;
        const auto& d = app.project.preampDevices[(size_t) row];
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        juce::String state = "not connected";
        if (auto* drv = app.preampDriver())
            for (auto& inf : drv->info()) if (inf.name == d.name && inf.host == d.host) state = inf.connected ? "connected, " + juce::String (inf.channels) + " preamps" : inf.status;
        g.setColour (theme::text); g.setFont (juce::FontOptions (13.0f));
        g.drawText (d.name + "   " + d.host + "   inputs from " + juce::String (d.firstInput + 1) + (d.map.empty() ? juce::String() : " (patched by ANEMAN)"), 8, 0, w - 220, h, juce::Justification::centredLeft, true);
        g.setColour (state.startsWith ("connected") ? juce::Colour (0xff8fe0a0) : juce::Colour (0xffffc070));
        g.drawText (state, w - 214, 0, 206, h, juce::Justification::centredRight, true);
    }
    void timerCallback() override { list.repaint(); }
    PrimaryApp& app;
    juce::Label title, help, lName, lHost, lFirst;
    juce::TextButton aneman { "Find the devices with ANEMAN" }, addBtn { "Add" }, removeBtn { "Remove selected" };
    juce::ListBox list;
    juce::TextEditor name, host, first, result;
};

//==============================================================================================================================================
/** The tracks: their names, mono or stereo, and the inputs they record. */
class DesignerPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit DesignerPanel (PrimaryApp& a) : app (a)
    {
        styleCaption (title, "THE TRACKS: what is recorded (each track is one file per take, mono or stereo, as in fermata)", true);
        styleCaption (help, "Name each track and choose its first input (a stereo track takes that input and the next). The session records the armed tracks "
                            "(ARM in the take window); while it runs, only the names can change.");
        help.setJustificationType (juce::Justification::topLeft);
        for (auto* c : std::initializer_list<juce::Component*> { &title, &help, &view, &addMono, &addStereo, &perInput, &perCount, &lockNote })
            addAndMakeVisible (c);
        view.setViewedComponent (&rows, false); view.setScrollBarsShown (true, false);
        perCount.setText ("16", false); perCount.setInputRestrictions (3, "0123456789");
        addMono.onClick = [this] { add (TrackFormat::Mono); };
        addStereo.onClick = [this] { add (TrackFormat::Stereo); };
        perInput.onClick = [this]
        {
            if (locked()) return;
            const int n = juce::jlimit (1, juce::jmax (1, (int) app.project.inputs.size()), perCount.getText().getIntValue());
            for (int i = 0; i < n; ++i)
            {
                bool used = false;
                for (auto& t : app.project.tracks) for (int c = 0; c < t.channelCount(); ++c) used = used || t.inputOf (c) == i;
                if (used) continue;
                const auto nm = app.project.inputLabel (i).isNotEmpty() ? app.project.inputLabel (i) : "Input " + juce::String (i + 1);
                app.project.addTrack (nm, TrackFormat::Mono, i);
            }
            app.project.syncMixers(); app.markDirty(); app.mixChanged(); rebuild();
        };
        lockNote.setColour (juce::Label::textColourId, juce::Colour (0xffffc070));
        app.addChangeListener (this);
        rebuild();
    }
    ~DesignerPanel() override { app.removeChangeListener (this); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (22));
        help.setBounds (r.removeFromTop (40));
        auto bottom = r.removeFromBottom (30);
        addMono.setBounds (bottom.removeFromLeft (130)); bottom.removeFromLeft (6);
        addStereo.setBounds (bottom.removeFromLeft (130)); bottom.removeFromLeft (16);
        perInput.setBounds (bottom.removeFromLeft (250)); bottom.removeFromLeft (4);
        perCount.setBounds (bottom.removeFromLeft (50).reduced (0, 2));
        r.removeFromBottom (4);
        lockNote.setBounds (r.removeFromBottom (20));
        view.setBounds (r);
        layoutRows();
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
private:
    struct Row final : public juce::Component
    {
        Row (DesignerPanel& o, int i) : owner (o), index (i)
        {
            auto& t = owner.app.project.tracks[(size_t) i];
            name.setText (t.name, false);
            name.onReturnKey = [this] { commitName(); };
            name.onFocusLost = [this] { commitName(); };
            format.addItem ("Mono", 1); format.addItem ("Stereo", 2);
            format.setSelectedId (t.format == TrackFormat::Stereo ? 2 : 1, juce::dontSendNotification);
            format.onChange = [this] { owner.setFormat (index, format.getSelectedId() == 2 ? TrackFormat::Stereo : TrackFormat::Mono); };
            for (int k = 0; k < (int) owner.app.project.inputs.size(); ++k)
                input.addItem (juce::String (k + 1).paddedLeft ('0', 2) + "  " + owner.app.project.inputLabel (k), k + 2);
            input.addItem ("(none)", 1);
            input.setSelectedId (t.inputOf (0) + 2, juce::dontSendNotification);
            input.onChange = [this] { owner.setInput (index, input.getSelectedId() - 2); };
            remove.onClick = [this] { owner.removeTrack (index); };
            for (auto* c : std::initializer_list<juce::Component*> { &name, &format, &input, &remove, &number }) addAndMakeVisible (c);
            number.setText (juce::String (i + 1), juce::dontSendNotification); number.setColour (juce::Label::textColourId, theme::dimText);
            const bool lock = owner.locked();
            format.setEnabled (! lock); input.setEnabled (! lock); remove.setEnabled (! lock);
        }
        void commitName()
        {
            auto& p = owner.app.project;
            if (! juce::isPositiveAndBelow (index, (int) p.tracks.size())) return;
            if (p.tracks[(size_t) index].name != name.getText().trim() && name.getText().trim().isNotEmpty()) { p.tracks[(size_t) index].name = name.getText().trim(); p.structureChanged(); owner.app.markDirty(); }
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (0, 2);
            number.setBounds (r.removeFromLeft (30));
            name.setBounds (r.removeFromLeft (260)); r.removeFromLeft (8);
            format.setBounds (r.removeFromLeft (100)); r.removeFromLeft (8);
            input.setBounds (r.removeFromLeft (260)); r.removeFromLeft (8);
            remove.setBounds (r.removeFromLeft (80));
        }
        DesignerPanel& owner; int index;
        juce::Label number; juce::TextEditor name; juce::ComboBox format, input; juce::TextButton remove { "Remove" };
    };
    bool locked() const { return app.model.sessionOn; }
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if ((int) app.project.tracks.size() != rowList.size() || locked() != wasLocked) rebuild();
    }
    void rebuild()
    {
        wasLocked = locked();
        rowList.clear();
        for (int i = 0; i < (int) app.project.tracks.size(); ++i)
            if (! app.project.tracks[(size_t) i].isLtc) rows.addAndMakeVisible (rowList.add (new Row (*this, i)));
        lockNote.setText (locked() ? "A session is running: the tracks it records are fixed until it ends (names can still change)." : juce::String(), juce::dontSendNotification);
        for (auto* b : { &addMono, &addStereo, &perInput }) b->setEnabled (! locked());
        layoutRows();
    }
    void layoutRows()
    {
        int y = 0;
        for (auto* r : rowList) { r->setBounds (0, y, juce::jmax (760, view.getWidth() - 16), 30); y += 32; }
        rows.setSize (juce::jmax (760, view.getWidth() - 16), y);
    }
    void add (TrackFormat f)
    {
        if (locked()) return;
        int next = 0;
        for (auto& t : app.project.tracks) for (int c = 0; c < t.channelCount(); ++c) next = juce::jmax (next, t.inputOf (c) + 1);
        app.project.addTrack (f == TrackFormat::Stereo ? "Stereo " + juce::String ((int) app.project.tracks.size() + 1) : "Track " + juce::String ((int) app.project.tracks.size() + 1), f,
                              juce::jmin (next, juce::jmax (0, (int) app.project.inputs.size() - 1)));
        app.project.syncMixers(); app.markDirty(); app.mixChanged(); rebuild();
    }
    void setFormat (int i, TrackFormat f)
    {
        if (locked() || ! juce::isPositiveAndBelow (i, (int) app.project.tracks.size())) return;
        auto& t = app.project.tracks[(size_t) i];
        const int first = t.inputOf (0);
        t.format = f;
        app.project.assignInputsFrom (t, first);
        app.project.structureChanged(); app.project.syncMixers(); app.markDirty(); app.mixChanged();
    }
    void setInput (int i, int first)
    {
        if (locked() || ! juce::isPositiveAndBelow (i, (int) app.project.tracks.size())) return;
        auto& t = app.project.tracks[(size_t) i];
        if (first < 0) t.inputs.fill (-1); else app.project.assignInputsFrom (t, first);
        app.project.structureChanged(); app.markDirty(); app.mixChanged();
    }
    void removeTrack (int i)
    {
        if (locked() || ! juce::isPositiveAndBelow (i, (int) app.project.tracks.size())) return;
        if (app.project.trackHasAudio (app.project.tracks[(size_t) i].id)) { showError ("Project Designer", "This track has recordings in the project: it stays (fermata can remove it)."); return; }
        app.project.removeTrack (i);
        app.markDirty(); app.mixChanged();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DesignerPanel> (this)] { if (safe != nullptr) safe->rebuild(); });
    }

    PrimaryApp& app;
    juce::Label title, help, lockNote;
    juce::Viewport view;
    juce::Component rows;
    juce::OwnedArray<Row> rowList;
    juce::TextButton addMono { "Add a mono track" }, addStereo { "Add a stereo track" }, perInput { "A mono track for each of the first inputs:" };
    juce::TextEditor perCount;
    bool wasLocked = false;
};

//==============================================================================================================================================
/** Bounce out: a stereo mix of a take through the processing mixer - the whole take, or the part between marks 1 and 2. */
class BouncePanel final : public juce::Component, private juce::Timer
{
public:
    explicit BouncePanel (PrimaryApp& a) : app (a)
    {
        styleCaption (title, "BOUNCE OUT: a stereo mix of a take, through the processing mixer", true);
        styleCaption (help, "Choose a take of the piece (merged takes only). \"Between marks 1 and 2\" uses the marks set with keys 1 and 2 in the take window. "
                            "The file goes to the project's Bounced Media folder (24-bit WAV).");
        help.setJustificationType (juce::Justification::topLeft);
        for (auto* c : std::initializer_list<juce::Component*> { &title, &help, &takeBox, &whole, &marks, &go, &progressLabel, &result, &reveal, &lTake })
            addAndMakeVisible (c);
        styleCaption (lTake, "Take");
        whole.setRadioGroupId (1); marks.setRadioGroupId (1); whole.setToggleState (true, juce::dontSendNotification);
        go.onClick = [this] { start(); };
        reveal.onClick = [this] { if (lastFile.existsAsFile()) lastFile.revealToUser(); };
        reveal.setEnabled (false);
        result.setMultiLine (true); result.setReadOnly (true); result.setCaretVisible (false);
        fillTakes();
        startTimer (200);
    }
    ~BouncePanel() override { job.reset(); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        title.setBounds (r.removeFromTop (22));
        help.setBounds (r.removeFromTop (48));
        { auto x = r.removeFromTop (28); lTake.setBounds (x.removeFromLeft (50)); takeBox.setBounds (x.reduced (0, 2)); }
        r.removeFromTop (6);
        whole.setBounds (r.removeFromTop (24)); marks.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
        { auto x = r.removeFromTop (30); go.setBounds (x.removeFromLeft (160)); x.removeFromLeft (10); reveal.setBounds (x.removeFromLeft (160)); x.removeFromLeft (10); progressLabel.setBounds (x); }
        r.removeFromTop (8);
        result.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
private:
    void fillTakes()
    {
        takeBox.clear (juce::dontSendNotification);
        if (auto* w = app.pieceWindow())
            for (auto& g : w->groups) takeBox.addItem (g.number > 0 ? juce::String (g.number).paddedLeft ('0', 3) + " - " + (g.label.isNotEmpty() ? g.label : w->name) + "  (" + juce::String (g.lengthSeconds() / 60.0, 1) + " min)" : g.label, g.number > 0 ? g.number : 999);
        if (takeBox.getNumItems() > 0 && takeBox.getSelectedId() == 0) takeBox.setSelectedItemIndex (takeBox.getNumItems() - 1, juce::dontSendNotification);
        shownTakes = takeBox.getNumItems();
    }
    void timerCallback() override
    {
        if (auto* w = app.pieceWindow()) if ((int) w->groups.size() != shownTakes) fillTakes();
        if (job != nullptr && job->isRunning()) progressLabel.setText ("Bouncing... " + juce::String ((int) std::lround (job->getProgress() * 100.0f)) + " %", juce::dontSendNotification);
    }
    void start()
    {
        if (job != nullptr && job->isRunning()) { job->cancel(); return; }
        auto* w = app.pieceWindow();
        if (w == nullptr) return;
        const TakeGroup* g = nullptr;
        for (auto& x : w->groups) if (x.number == takeBox.getSelectedId()) g = &x;
        if (g == nullptr) { showError ("Bounce out", "Choose a take (a take is here once it has been merged)."); return; }
        auto* m = app.monitorMixer();
        if (m == nullptr) return;
        BounceSettings bs;
        bs.folder = app.project.bouncedFolder(); bs.folder.createDirectory();
        bs.mixerId = m->id;
        for (int bi : app.project.busIndices (true)) if (auto* b = m->busFor (app.project.buses[(size_t) bi].id)) if (b->isMain.get()) bs.sources.push_back (app.project.buses[(size_t) bi].id);
        if (bs.sources.empty()) { showError ("Bounce out", "The processing mixer has no main output to bounce."); return; }
        bs.sampleRate = g->sampleRate;
        bs.segments = segmentsForTake (app.project, *g);
        bs.addOutputName = false;
        bs.alignOwner = app.project.alignOwnerFor (false, w->id);
        const auto base = juce::String (g->number).paddedLeft ('0', 3) + " - " + (g->label.isNotEmpty() ? g->label : w->name) + " - bounce";
        if (marks.getToggleState())
        {
            if (w->editTake != g->id || ! (w->editIn >= 0.0 && w->editOut > w->editIn)) { showError ("Bounce out", "Set marks 1 and 2 in this take first (keys 1 and 2 in the take window)."); return; }
            BounceItem item; item.name = base;
            item.start = (juce::int64) std::llround (w->editIn * g->sampleRate); item.end = (juce::int64) std::llround (w->editOut * g->sampleRate);
            bs.items.push_back (item);
        }
        else { BounceItem item; item.name = base; item.start = 0; item.end = (juce::int64) g->lengthSamples; bs.items.push_back (item); }
        if (bs.segments.empty()) { showError ("Bounce out", "This take has no audio."); return; }
        juce::Component::SafePointer<BouncePanel> safe (this);
        result.setText ("Bouncing " + base + "...");
        go.setButtonText ("Cancel");
        job = std::make_unique<BounceJob> (app.project, bs, [safe] (BounceResult r)
        {
            if (safe == nullptr) return;
            safe->go.setButtonText ("Bounce");
            safe->progressLabel.setText ({}, juce::dontSendNotification);
            if (r.error.isNotEmpty()) { safe->result.setText ("The bounce did not work: " + r.error); return; }
            if (r.cancelled) { safe->result.setText ("Cancelled."); return; }
            safe->lastFile = r.files.isEmpty() ? juce::File() : juce::File (r.files[0]);
            safe->reveal.setEnabled (safe->lastFile.existsAsFile());
            safe->result.setText ("Bounced: " + r.files.joinIntoString ("\n") + "\nPeak " + juce::String (r.peakDb, 1) + " dBFS" + (r.clipped ? " - IT CLIPPED (over 0 dBFS): lower the mixer and bounce again" : ""));
        });
    }
    PrimaryApp& app;
    juce::Label title, help, lTake, progressLabel;
    juce::ComboBox takeBox;
    juce::ToggleButton whole { "The whole take" }, marks { "Between marks 1 and 2" };
    juce::TextButton go { "Bounce" }, reveal { "Show the file" };
    juce::TextEditor result;
    std::unique_ptr<BounceJob> job;
    juce::File lastFile;
    int shownTakes = -1;
};

//==============================================================================================================================================
class LogPanel final : public juce::Component, private juce::Timer
{
public:
    explicit LogPanel (PrimaryApp& a) : app (a)
    {
        text.setMultiLine (true); text.setReadOnly (true); text.setCaretVisible (false); text.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        addAndMakeVisible (text);
        startTimer (500);
        timerCallback();
    }
    void resized() override { text.setBounds (getLocalBounds().reduced (8)); }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
private:
    void timerCallback() override
    {
        if ((juce::int64) app.logLines == shown) return;
        shown = (juce::int64) app.logLines;
        text.setText (app.recentLog.joinIntoString ("\n"), false);
        text.moveCaretToEnd();
    }
    PrimaryApp& app; juce::TextEditor text; juce::int64 shown = -1;
};

std::unique_ptr<juce::Component> makeAudioSettings (PrimaryApp& a) { return std::make_unique<AudioSettingsPanel> (a); }
std::unique_ptr<juce::Component> makePreampsPanel (PrimaryApp& a) { return std::make_unique<PreampsPanel> (a); }
std::unique_ptr<juce::Component> makeDesigner (PrimaryApp& a)      { return std::make_unique<DesignerPanel> (a); }
std::unique_ptr<juce::Component> makeBouncePanel (PrimaryApp& a)   { return std::make_unique<BouncePanel> (a); }
std::unique_ptr<juce::Component> makeLogPanel (PrimaryApp& a)      { return std::make_unique<LogPanel> (a); }
} // namespace td::live
