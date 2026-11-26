// fermata:Live 0.6.0: the second seat (see SecondSeat.h and docs/LIVE-SECOND-SEAT.md).
#include "SecondSeat.h"
#include "../ui/KeyMap.h"

// The Windows part (the driver's library, the keys' names, the pointer that clicks go through) is compiled on Windows only;
// FERMATA_SEAT_WINDOWS_STUBS lets tools/check-windows-code.sh compile it on Linux against stand-in headers.
#if JUCE_WINDOWS || defined (FERMATA_SEAT_WINDOWS_STUBS)
 #define FERMATA_SEAT_WINDOWS 1
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #define FERMATA_SEAT_WINDOWS 0
#endif
#include <algorithm>
#include <cstring>

namespace td::live
{
// ---- the Interception driver's interface (interception.h), as interception.dll exports it - loaded when the Fixer starts, never linked in
namespace icp
{
using Context = void*;
using Predicate = int (*) (int);
struct MouseStroke { unsigned short state = 0, flags = 0; short rolling = 0; int x = 0, y = 0; unsigned int information = 0; };
struct KeyStroke   { unsigned short code = 0, state = 0; unsigned int information = 0; };
constexpr int strokeBytes = (int) sizeof (MouseStroke);                 // (InterceptionStroke: the bigger of the two)
constexpr unsigned short keyUp = 0x01, keyE0 = 0x02;
constexpr unsigned short filterNone = 0x0000, filterAll = 0xFFFF, filterKeyDown = 0x0001, filterKeyE0 = 0x0004, filterMouseLeftDown = 0x0001;
constexpr unsigned short leftDown = 0x001, leftUp = 0x002, rightDown = 0x004, rightUp = 0x008, middleDown = 0x010, middleUp = 0x020,
                         wheel = 0x400, hwheel = 0x800;
constexpr unsigned short moveAbsolute = 0x001;
constexpr int maxKeyboards = 10;
inline bool isKeyboard (int d) noexcept { return d >= 1 && d <= maxKeyboards; }
inline bool isMouse (int d) noexcept    { return d > maxKeyboards && d <= 2 * maxKeyboards; }
}

struct SecondSeat::Api
{
    icp::Context (*create)() = nullptr;
    void (*destroy) (icp::Context) = nullptr;
    void (*setFilter) (icp::Context, icp::Predicate, unsigned short) = nullptr;
    int (*waitTimeout) (icp::Context, unsigned long) = nullptr;
    int (*send) (icp::Context, int, const void*, unsigned int) = nullptr;
    int (*receive) (icp::Context, int, void*, unsigned int) = nullptr;
    unsigned int (*hardwareId) (icp::Context, int, void*, unsigned int) = nullptr;
    void* module = nullptr;

    bool load (juce::String& why)
    {
       #if FERMATA_SEAT_WINDOWS
        auto* m = LoadLibraryW (L"interception.dll");
        if (m == nullptr) { why = "interception.dll was not found: put it beside fermata-Live.exe (docs/LIVE-SECOND-SEAT.md says where it comes from)."; return false; }
        module = (void*) m;
        auto get = [m] (const char* name) { return (void*) GetProcAddress (m, name); };
        create = (icp::Context (*)()) get ("interception_create_context");
        destroy = (void (*) (icp::Context)) get ("interception_destroy_context");
        setFilter = (void (*) (icp::Context, icp::Predicate, unsigned short)) get ("interception_set_filter");
        waitTimeout = (int (*) (icp::Context, unsigned long)) get ("interception_wait_with_timeout");
        send = (int (*) (icp::Context, int, const void*, unsigned int)) get ("interception_send");
        receive = (int (*) (icp::Context, int, void*, unsigned int)) get ("interception_receive");
        hardwareId = (unsigned int (*) (icp::Context, int, void*, unsigned int)) get ("interception_get_hardware_id");
        if (create == nullptr || destroy == nullptr || setFilter == nullptr || waitTimeout == nullptr || send == nullptr || receive == nullptr || hardwareId == nullptr)
        { why = "interception.dll is not the Interception library this was made for (a function is missing)."; return false; }
        return true;
       #else
        why = "The second seat needs Windows and the Interception driver.";
        return false;
       #endif
    }
};

// ---- which devices the driver should hand over (its predicates are plain functions: they read these)
static std::atomic<int> gSeatKeyboard { 0 }, gSeatMouse { 0 };
static int anyDevice (int) { return 1; }
static int aKeyboard (int d) { return icp::isKeyboard (d) ? 1 : 0; }
static int aMouse (int d) { return icp::isMouse (d) ? 1 : 0; }
static int seatKeyboardOnly (int d) { return d != 0 && d == gSeatKeyboard.load() ? 1 : 0; }
static int seatMouseOnly (int d) { return d != 0 && d == gSeatMouse.load() ? 1 : 0; }

juce::String SecondSeat::Device::describe() const
{
    if (! valid()) return "none";
    juce::String s = keyboard ? "Keyboard " + juce::String (number) : "Mouse " + juce::String (number - icp::maxKeyboards);
    const auto up = hardwareId.toUpperCase();
    const auto vid = up.fromFirstOccurrenceOf ("VID_", false, false).substring (0, 4), pid = up.fromFirstOccurrenceOf ("PID_", false, false).substring (0, 4);
    if (vid.isNotEmpty() && pid.isNotEmpty()) s << " (VID " << vid << ", PID " << pid << ")";
    else if (hardwareId.isNotEmpty()) s << " (" << hardwareId.substring (0, 40) << ")";
    return s;
}

// ---- the pointer: a small window of its own, always on top, that every click goes through. It sits one pixel right of and below the point it
//      points at, so it never covers that point (JUCE asks the system which window is under a click; it must be the Fixer's, not the pointer)
class SecondSeat::PointerWindow final : public juce::Component
{
public:
    explicit PointerWindow (const juce::String& name) : label (name)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (false);
        setWantsKeyboardFocus (false);
        const int w = label.isNotEmpty() ? juce::jmin (180, 28 + juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (12.0f, juce::Font::bold), label) + 12) : 22;
        setSize (w, 36);
    }
    void paint (juce::Graphics& g) override
    {
        juce::Path arrow;
        arrow.startNewSubPath (1.5f, 1.5f); arrow.lineTo (1.5f, 23.0f); arrow.lineTo (7.0f, 18.0f); arrow.lineTo (10.5f, 27.0f);
        arrow.lineTo (14.0f, 25.5f); arrow.lineTo (10.5f, 16.5f); arrow.lineTo (17.5f, 16.5f); arrow.closeSubPath();
        g.setColour (juce::Colour (0xffff8a1f)); g.fillPath (arrow);
        g.setColour (juce::Colour (0xff1a1206)); g.strokePath (arrow, juce::PathStrokeType (1.5f));
        if (label.isEmpty()) return;
        auto r = juce::Rectangle<float> (18.0f, 20.0f, (float) getWidth() - 19.0f, 15.0f);
        g.setColour (juce::Colour (0xffff8a1f)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xff1a1206)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (label, r.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
    }
    juce::String label;
};

/** A drop-down list (a juce::ComboBox) clicked with the seat's mouse: JUCE's own menu would follow the ORDINARY mouse (it watches the PC's pointer,
    and closes when its program is not the one in front), so the seat shows the list's items itself, below the box, in the menu's colours. Only the
    seat works it - its mouse (a click on an item, or press on the box, drag and let go on one), its wheel and its keys (Up, Down, Enter, Esc); the
    ordinary mouse goes through it. Choosing sets the box as a choice in JUCE's menu would. */
class SecondSeat::SeatList final : public juce::Component
{
public:
    struct Item { int id = 0; juce::String text; bool enabled = true, separator = false, heading = false; };

    SeatList (juce::ComboBox& c, juce::Rectangle<int> screenArea) : combo (&c)
    {
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
        for (juce::PopupMenu::MenuItemIterator it (*c.getRootMenu(), true); it.next();)
        {
            const auto& m = it.getItem();
            if (m.subMenu != nullptr) continue;                               // (a sub-menu's own row: its items follow)
            Item x; x.id = m.itemID; x.text = m.text; x.enabled = m.isEnabled && m.itemID != 0; x.separator = m.isSeparator; x.heading = m.isSectionHeader;
            if (x.separator || x.heading) x.enabled = false;
            items.push_back (x);
            if (x.id != 0 && x.id == c.getSelectedId()) chosen = (int) items.size() - 1;
        }
        int w = c.getWidth();
        for (auto& x : items) w = juce::jmax (w, juce::GlyphArrangement::getStringWidthInt (juce::FontOptions (15.0f), x.text) + 44);
        const auto cb = c.getScreenBounds();
        juce::Rectangle<int> r (cb.getX(), cb.getBottom(), w, juce::jmin ((int) items.size(), maxRows) * rowH + 4);
        if (! screenArea.isEmpty())
        {
            if (r.getBottom() > screenArea.getBottom() && cb.getY() - r.getHeight() >= screenArea.getY()) r.setY (cb.getY() - r.getHeight());
            r = r.constrainedWithin (screenArea);
        }
        setBounds (r);
        hovered = chosen;
        if (hovered >= maxRows) first = juce::jmin (hovered, (int) items.size() - maxRows);
        bg = c.findColour (juce::PopupMenu::backgroundColourId); text = c.findColour (juce::PopupMenu::textColourId);
        hiBg = c.findColour (juce::PopupMenu::highlightedBackgroundColourId); hiText = c.findColour (juce::PopupMenu::highlightedTextColourId);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks);
       #if FERMATA_SEAT_WINDOWS
        if (auto* h = (HWND) getWindowHandle())                               // (never the window in front: the Primary's keyboard stays where it is)
            SetWindowLongPtrW (h, GWL_EXSTYLE, GetWindowLongPtrW (h, GWL_EXSTYLE) | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
       #endif
        setAlwaysOnTop (true);
        setVisible (true);
    }

    bool isFor (const juce::ComboBox* c) const noexcept { return combo.getComponent() == c; }
    bool over (juce::Point<float> screen) const        { return getScreenBounds().toFloat().contains (screen); }
    void hoverAt (juce::Point<float> screen)
    {
        if (! over (screen)) return;                                          // (off the list: what is lit stays lit, for the keys)
        const auto l = screen - getScreenPosition().toFloat();
        const int row = first + (int) std::floor ((l.y - 2.0f) / (float) rowH);
        const int h = juce::isPositiveAndBelow (row, (int) items.size()) && items[(size_t) row].enabled ? row : -1;
        if (h != hovered) { hovered = h; repaint(); }
    }
    void wheel (float dy) { scrollTo (first + (dy > 0 ? -3 : 3)); }
    /** Up, Down, Home, End, Page Up / Down move; Enter chooses (true); the rest are kept from the Fixer while the list is open. */
    bool key (const juce::KeyPress& k)
    {
        const int code = k.getKeyCode();
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) step (code == juce::KeyPress::upKey ? -1 : 1, 1);
        else if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey) step (code == juce::KeyPress::pageUpKey ? -1 : 1, maxRows - 1);
        else if (code == juce::KeyPress::homeKey) { hovered = -1; step (1, 1); }
        else if (code == juce::KeyPress::endKey) { hovered = (int) items.size(); step (-1, 1); }
        else if (code == juce::KeyPress::returnKey) return true;
        return false;
    }
    /** The item under the seat's pointer (or moved to with the keys) chosen: the box takes it, as from its own menu. */
    void choose()
    {
        if (auto* c = combo.getComponent())
            if (juce::isPositiveAndBelow (hovered, (int) items.size()) && items[(size_t) hovered].enabled && c->isEnabled())
                c->setSelectedId (items[(size_t) hovered].id);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (bg);
        g.setColour (juce::Colour (0xffff8a1f).withAlpha (0.8f)); g.drawRect (getLocalBounds(), 1);   // (the seat's orange: this list is the seat's)
        g.setFont (juce::FontOptions (15.0f));
        for (int i = first, y = 2; i < (int) items.size() && y < getHeight() - 2; ++i, y += rowH)
        {
            const auto& x = items[(size_t) i];
            auto r = juce::Rectangle<int> (1, y, getWidth() - 2, rowH);
            if (x.separator) { g.setColour (text.withAlpha (0.3f)); g.fillRect (r.reduced (8, 0).withSizeKeepingCentre (r.getWidth() - 16, 1)); continue; }
            if (i == hovered) { g.setColour (hiBg); g.fillRect (r); }
            g.setColour (x.heading ? text.withAlpha (0.65f) : i == hovered ? hiText : x.enabled ? text : text.withAlpha (0.4f));
            if (i == chosen) g.drawText (juce::String::charToString ((juce::juce_wchar) 0x2713), r.withWidth (22), juce::Justification::centred, false);
            g.setFont (juce::FontOptions (15.0f, x.heading ? juce::Font::bold : juce::Font::plain));
            g.drawText (x.text, r.withTrimmedLeft (24).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
        if (first > 0) { g.setColour (text.withAlpha (0.6f)); g.drawText ("^", getLocalBounds().removeFromTop (12).removeFromRight (16), juce::Justification::centred, false); }
        if (first + maxRows < (int) items.size()) { g.setColour (text.withAlpha (0.6f)); g.drawText ("v", getLocalBounds().removeFromBottom (12).removeFromRight (16), juce::Justification::centred, false); }
    }

private:
    void scrollTo (int f)
    {
        f = juce::jlimit (0, juce::jmax (0, (int) items.size() - maxRows), f);
        if (f != first) { first = f; repaint(); }
    }
    void step (int dir, int count)
    {
        int h = hovered < 0 ? (dir > 0 ? -1 : (int) items.size()) : hovered;
        for (int moved = 0, i = h + dir; i >= 0 && i < (int) items.size() && moved < count; i += dir)
            if (items[(size_t) i].enabled) { h = i; ++moved; }
        if (! juce::isPositiveAndBelow (h, (int) items.size())) return;
        hovered = h;
        if (hovered < first) first = hovered; else if (hovered >= first + maxRows) first = hovered - maxRows + 1;
        repaint();
    }

    static constexpr int rowH = 22, maxRows = 18;
    juce::Component::SafePointer<juce::ComboBox> combo;
    std::vector<Item> items;
    int hovered = -1, chosen = -1, first = 0;
    juce::Colour bg, text, hiBg, hiText;
};

SecondSeat::SecondSeat (juce::PropertiesFile& s) : juce::Thread ("fermata:Live second seat"), settings (s)
{
    speed = juce::jlimit (0.3, 5.0, settings.getDoubleValue ("seatSpeed", 1.5));
    api = std::make_unique<Api>();
   #if JUCE_LINUX
    testFeed = juce::SystemStats::getEnvironmentVariable ("FERMATA_SEAT_TEST_FEED", {});   // (trying it here without the driver: a file of fake strokes)
   #endif
    if (testFeed.isNotEmpty()) problem.clear();
    else if (api->load (problem))
    {
        context = api->create();
        if (context == nullptr) problem = "The Interception driver is not installed (or the PC has not been restarted since): docs/LIVE-SECOND-SEAT.md says how.";
    }
    chosenKeyboard = find (settings.getValue ("seatKeyboardId"), settings.getIntValue ("seatKeyboardNumber"), true);
    chosenMouse = find (settings.getValue ("seatMouseId"), settings.getIntValue ("seatMouseNumber"), false);
    if (driverPresent()) startThread (juce::Thread::Priority::high);       // (its work is passing strokes on at once: it must never be the slow part)
}

SecondSeat::~SecondSeat()
{
    stopTimer();
    cancelPendingUpdate();
    mode = 0;
    signalThreadShouldExit();
    stopThread (3000);                                                    // (the thread gives every device back before it ends)
    if (context != nullptr && api->destroy != nullptr) api->destroy (context);
   #if FERMATA_SEAT_WINDOWS
    if (api->module != nullptr) FreeLibrary ((HMODULE) api->module);
   #endif
    list.reset();
    pointer.reset();
}

std::vector<SecondSeat::Device> SecondSeat::devices() const
{
    std::vector<Device> out;
    if (testFeed.isNotEmpty())                                            // (the test feed: one keyboard and one mouse of each kind)
    {
        out.push_back ({ 1, "HID\\VID_1111&PID_0001", true }); out.push_back ({ 2, "HID\\VID_2222&PID_0002", true });
        out.push_back ({ 11, "HID\\VID_3333&PID_0003", false }); out.push_back ({ 12, "HID\\VID_4444&PID_0004", false });
        return out;
    }
    if (context == nullptr) return out;
    const std::lock_guard<std::mutex> g (driverLock);
    for (int d = 1; d <= 2 * icp::maxKeyboards; ++d)
    {
        wchar_t buf[512] = {};
        const auto n = api->hardwareId (context, d, buf, (unsigned int) sizeof (buf) - sizeof (wchar_t));
        if (n == 0) continue;
        Device x; x.number = d; x.keyboard = icp::isKeyboard (d); x.hardwareId = juce::String (buf);
        out.push_back (x);
    }
    return out;
}

SecondSeat::Device SecondSeat::find (const juce::String& id, int number, bool keyboard) const
{
    if (id.isEmpty()) return {};
    const auto all = devices();
    for (auto& d : all) if (d.number == number && d.keyboard == keyboard && d.hardwareId == id) return d;     // (the same device, where it was)
    // plugged in elsewhere since: found by its id - but only when no other device has the same id (two keyboards of the same make and model: the
    // seat never guesses which is the Fixer's, as it could take the Primary's; Learn chooses again)
    const Device* only = nullptr; int same = 0;
    for (auto& d : all) if (d.keyboard == keyboard && d.hardwareId == id) { only = &d; ++same; }
    if (same == 1) return *only;
    Device missing; missing.hardwareId = id; missing.keyboard = keyboard;                                     // (not there now: remembered, number 0)
    return missing;
}

int SecondSeat::countOf (bool keyboard) const
{
    int n = 0;
    for (auto& d : devices()) if (d.keyboard == keyboard) ++n;
    return n;
}

void SecondSeat::saveChoice()
{
    settings.setValue ("seatKeyboardId", chosenKeyboard.hardwareId); settings.setValue ("seatKeyboardNumber", chosenKeyboard.number);
    settings.setValue ("seatMouseId", chosenMouse.hardwareId); settings.setValue ("seatMouseNumber", chosenMouse.number);
    settings.setValue ("seatSpeed", speed);
    settings.saveIfNeeded();
}

void SecondSeat::setSpeed (double v)
{
    speed = juce::jlimit (0.3, 5.0, v);
    settings.setValue ("seatSpeed", speed);
}

void SecondSeat::setMode (int m)
{
    mode = m;
    notify();
}

void SecondSeat::learn (Learn what)
{
    if (! driverPresent() || on) return;
    learningNow = what;
    learnUntil = juce::Time::getMillisecondCounter() + 15000;
    setMode (what == Learn::keyboard ? 1 : what == Learn::mouse ? 2 : 0);
    startTimer (250);
    if (testFeed.isNotEmpty() && what != Learn::none)
    {   // (the test feed: the second of each kind is "pressed")
        Stroke st; st.device = what == Learn::keyboard ? 2 : 12; st.keyboard = what == Learn::keyboard;
        { const std::lock_guard<std::mutex> g (queueLock); queue.push_back (st); }
        triggerAsyncUpdate();
    }
    if (onChanged) onChanged();
}

void SecondSeat::forget (bool keyboardToo, bool mouseToo)
{
    if (on) stop (false);
    if (keyboardToo) chosenKeyboard = {};
    if (mouseToo) chosenMouse = {};
    saveChoice();
    if (onChanged) onChanged();
}

bool SecondSeat::start (juce::String& why)
{
    if (! driverPresent()) { why = problem; return false; }
    if (on) return true;
    chosenKeyboard = find (chosenKeyboard.hardwareId, chosenKeyboard.number, true);
    chosenMouse = find (chosenMouse.hardwareId, chosenMouse.number, false);
    if (chosenKeyboard.hardwareId.isEmpty() && chosenMouse.hardwareId.isEmpty()) { why = "Choose the Fixer's own keyboard and mouse first (Learn)."; return false; }
    // one that was chosen but is not found now: never started without it (the Fixer's keyboard would type into the Primary's window)
    for (auto* d : { &chosenKeyboard, &chosenMouse })
        if (d->hardwareId.isNotEmpty() && ! d->valid())
        {
            why = juce::String ("The Fixer's ") + (d->keyboard ? "keyboard" : "mouse") + " is not plugged in (or cannot be told from another of the same "
                  "model): Learn it again - or Forget it, to use the seat without one.";
            return false;
        }
    // never the only keyboard or the only mouse of the PC: the Primary would be left without one
    if (chosenKeyboard.valid() && countOf (true) < 2) { why = "That is the only keyboard the PC has: the Fixer needs one of its own."; return false; }
    if (chosenMouse.valid() && countOf (false) < 2) { why = "That is the only mouse the PC has: the Fixer needs one of its own."; return false; }
    seatKeyboard = chosenKeyboard.number; seatMouse = chosenMouse.number;
    buttons = 0; keyMods = 0; lShift = rShift = lCtrl = rCtrl = lAlt = rAlt = false; escapes.clear(); dragPeer = nullptr;
    const auto a = area ? area() : juce::Rectangle<int> (0, 0, 800, 600);
    pos = a.getCentre().toFloat();
    // 0.8.0: a Learn still waiting for its key or click ends here. Left waiting, the first key of the seat's own keyboard (or click of its mouse)
    // was taken as the answer to Learn, which then switched the driver off while the panel went on saying ON: the Fixer's keyboard typed into
    // whichever window was in front - the Primary's.
    learningNow = Learn::none;
    on = true;
    setMode (3);
    startTimerHz (20);                                                       // (the PC's keyboard stays the Primary's: keepForeground)
    settings.setValue ("seatOn", true); saveChoice();
    if (chosenMouse.valid()) showPointer (true);
    if (onChanged) onChanged();
    return true;
}

void SecondSeat::stop (bool remember)
{
    if (on && (buttons != 0) && dragPeer != nullptr && juce::ComponentPeer::isValidPeer (dragPeer))
    {   // a drag the seat was making ends where it is (a mouse-up), so nothing is left held down
        buttons = 0; sendMouse();
    }
    closeList(); swallowPress = false;
    on = false;
    setMode (0);
    if (learningNow == Learn::none) stopTimer();
    seatKeyboard = 0; seatMouse = 0;
    showPointer (false);
    if (remember) settings.setValue ("seatOn", false);
    settings.saveIfNeeded();
    if (onChanged) onChanged();
}

void SecondSeat::startIfWanted()
{
    if (! settings.getBoolValue ("seatOn", false) || ! driverPresent()) return;
    juce::String why;
    if (! start (why) && say) say ("The second seat did not start: " + why);
}

void SecondSeat::showPointer (bool show)
{
    if (! show) { pointer.reset(); return; }
    if (pointer == nullptr)
    {
        pointer = std::make_unique<PointerWindow> (label);
        pointer->addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresMouseClicks);
       #if FERMATA_SEAT_WINDOWS
        if (auto* h = (HWND) pointer->getWindowHandle())                  // (every click goes through it, to any program's window: WS_EX_TRANSPARENT)
            SetWindowLongPtrW (h, GWL_EXSTYLE, GetWindowLongPtrW (h, GWL_EXSTYLE) | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
       #endif
        pointer->setAlwaysOnTop (true);
    }
    pointer->setTopLeftPosition ((int) std::floor (pos.x) + 1, (int) std::floor (pos.y) + 1);
    pointer->setVisible (true);
}

// ---- the seat's thread: the driver's strokes, handed on at once (or kept, for the seat's own devices)
void SecondSeat::run()
{
    while (! threadShouldExit())
    {
        if (testFeed.isNotEmpty())
        {   // (Linux, no driver: strokes read from a file, one a line - "move dx dy", "down L|R|M", "up L|R|M", "wheel n", "scan code e0 down|up",
            //  "key juceKeyCode char down" - so the pointer, the clicks and the keys can be tried here)
            const juce::File f (testFeed);
            if (mode.load() == 3 && f.getSize() > testFeedRead)
            {
                juce::FileInputStream in (f);
                in.setPosition (testFeedRead);
                std::vector<Stroke> got;
                while (! in.isExhausted())
                {
                    const auto line = in.readNextLine().trim();
                    juce::StringArray w; w.addTokens (line, " ", "");
                    if (w.isEmpty()) continue;
                    Stroke s;
                    if (w[0] == "move") { s.device = 11; s.x = w[1].getIntValue(); s.y = w[2].getIntValue(); }
                    else if (w[0] == "down" || w[0] == "up")
                    {
                        const bool down = w[0] == "down"; s.device = 11;
                        s.state = w[1] == "R" ? (down ? icp::rightDown : icp::rightUp) : w[1] == "M" ? (down ? icp::middleDown : icp::middleUp) : (down ? icp::leftDown : icp::leftUp);
                    }
                    else if (w[0] == "wheel") { s.device = 11; s.state = icp::wheel; s.rolling = (short) w[1].getIntValue(); }
                    else if (w[0] == "scan") { s.device = 1; s.keyboard = true; s.code = (unsigned short) w[1].getHexValue32(); s.state = (unsigned short) ((w[2] == "1" ? icp::keyE0 : 0) | (w[3] == "up" ? icp::keyUp : 0)); }
                    else if (w[0] == "key") { s.device = 1; s.keyboard = true; s.juceKey = w[1].getIntValue(); s.juceChar = (juce::juce_wchar) w[2].getIntValue(); if (w[3] == "up") s.state = icp::keyUp; }
                    else continue;
                    got.push_back (s);
                }
                testFeedRead = f.getSize();
                if (! got.empty()) { { const std::lock_guard<std::mutex> g (queueLock); for (auto& s : got) queue.push_back (s); } triggerAsyncUpdate(); }
            }
            appliedMode = mode.load();
            wait (30);
            continue;
        }
        const int want = mode.load();
        if (want != appliedMode.load())
        {
            const int was = appliedMode.load();
            const std::lock_guard<std::mutex> g (driverLock);
            api->setFilter (context, anyDevice, icp::filterNone);           // (everything back first)
            drain (was == 1 || was == 2);                                   // (strokes the driver still held: learning's go on to Windows, the seat's go)
            if (want == 1) api->setFilter (context, aKeyboard, (unsigned short) (icp::filterKeyDown | icp::filterKeyE0));   // (any key, the arrows too)
            else if (want == 2) api->setFilter (context, aMouse, icp::filterMouseLeftDown);
            else if (want == 3)
            {
                gSeatKeyboard = seatKeyboard.load(); gSeatMouse = seatMouse.load();
                if (gSeatKeyboard.load() != 0) api->setFilter (context, seatKeyboardOnly, icp::filterAll);
                if (gSeatMouse.load() != 0) api->setFilter (context, seatMouseOnly, icp::filterAll);
            }
            appliedMode = want;
        }
        if (want == 0) { wait (200); continue; }
        const int d = api->waitTimeout (context, 50);                      // (the wait is on the driver's events, not a call into it: not locked)
        if (d <= 0) continue;
        alignas (8) char buf[icp::strokeBytes] = {};
        const bool seatDevice = want == 3 && ((d == gSeatKeyboard.load() && gSeatKeyboard.load() != 0) || (d == gSeatMouse.load() && gSeatMouse.load() != 0));
        {
            const std::lock_guard<std::mutex> g (driverLock);
            if (api->receive (context, d, buf, 1) <= 0) continue;
            if (! seatDevice) api->send (context, d, buf, 1);               // (learning, or anything else: straight on to Windows, at once)
        }
        if (want == 3 && ! seatDevice) continue;
        Stroke s; s.device = d; s.keyboard = icp::isKeyboard (d);
        if (s.keyboard) { icp::KeyStroke k; std::memcpy (&k, buf, sizeof k); s.code = k.code; s.state = k.state; }
        else { icp::MouseStroke m; std::memcpy (&m, buf, sizeof m); s.state = m.state; s.flags = m.flags; s.rolling = m.rolling; s.x = m.x; s.y = m.y; }
        {
            const std::lock_guard<std::mutex> g (queueLock);
            if (queue.size() > 8192) queue.erase (queue.begin(), queue.begin() + 4096);   // (the window stuck: the oldest go - never the PC's input)
            queue.push_back (s);
        }
        triggerAsyncUpdate();
    }
    if (context != nullptr && testFeed.isEmpty())
    {   // closing: every device is Windows' again (what the driver still held of the seat's goes; learning's goes on)
        const std::lock_guard<std::mutex> g (driverLock);
        const int was = appliedMode.load();
        api->setFilter (context, anyDevice, icp::filterNone);
        drain (was == 1 || was == 2);
    }
}

void SecondSeat::drain (bool sendOn)
{   // (driverLock held, the filter just cleared: nothing new comes - only what was captured before)
    for (int n = 0; n < 4096; ++n)
    {
        const int d = api->waitTimeout (context, 0);
        if (d <= 0) return;
        alignas (8) char buf[icp::strokeBytes] = {};
        if (api->receive (context, d, buf, 1) <= 0) return;
        if (sendOn) api->send (context, d, buf, 1);
    }
}

// ---- the message thread
void SecondSeat::timerCallback()
{
    if (learningNow != Learn::none && juce::Time::getMillisecondCounter() > learnUntil)
    {
        learningNow = Learn::none; setMode (on ? 3 : 0);
        if (say) say ("No key or click came: nothing was chosen.");
        if (onChanged) onChanged();
    }
    if (on)
    {
        keepForeground();
        if (++ticks >= 20) { ticks = 0; checkDevices(); }
        return;
    }
    if (learningNow == Learn::none) stopTimer();
}

void SecondSeat::checkDevices()
{
    if (testFeed.isNotEmpty()) return;
    for (auto* c : { &chosenKeyboard, &chosenMouse })
    {
        if (! c->valid()) continue;
        const auto now = find (c->hardwareId, c->number, c->keyboard);
        if (now.number == c->number) continue;
        // unplugged, or plugged in elsewhere (the driver gives it a new number): the filter would be on the old number, so the device would be an
        // ordinary one of Windows' - typing into whichever window is in front. The seat lets go instead, and says so.
        stop (true);
        if (say) say (juce::String ("The Fixer's ") + (c->keyboard ? "keyboard" : "mouse") + " went (unplugged?): the second seat let go. Plug it in and Start again"
                      + (now.valid() ? "." : " (Learn it again if it is not found)."));
        return;
    }
}

#if FERMATA_SEAT_WINDOWS
static BOOL CALLBACK findPrimary (HWND h, LPARAM out)
{   // the Primary's main window: "fermata:Live <version> - Primary", of another program than this one
    DWORD pid = 0; GetWindowThreadProcessId (h, &pid);
    if (pid == GetCurrentProcessId() || ! IsWindowVisible (h) || IsIconic (h)) return TRUE;
    WCHAR t[200] = {};
    GetWindowTextW (h, t, 199);
    const juce::String title (t);
    if (title.startsWith ("fermata:Live") && title.endsWith ("- Primary")) { *(HWND*) out = h; return FALSE; }
    return TRUE;
}
#endif

void SecondSeat::keepForeground()
{
   #if FERMATA_SEAT_WINDOWS
    // While the seat has a keyboard of its own, the PC's keyboard is the Primary's: a window of the Fixer that becomes the active one anyway (a window
    // it opened, the ordinary mouse clicking it, Windows' own timing) gives it straight back to the window that had it - the Primary's, usually.
    if (seatKeyboard.load() == 0) return;
    const HWND fg = GetForegroundWindow();
    if (fg == nullptr) return;
    DWORD pid = 0; GetWindowThreadProcessId (fg, &pid);
    if (pid != GetCurrentProcessId()) { otherWindow = fg; return; }
    HWND back = (HWND) otherWindow;
    if (back == nullptr || ! IsWindow (back) || ! IsWindowVisible (back) || IsIconic (back)) { back = nullptr; EnumWindows (findPrimary, (LPARAM) &back); }
    if (back == nullptr) return;
    otherWindow = back;
    SetForegroundWindow (back);                                           // (allowed: this program is the one in front at this moment)
   #endif
}

void SecondSeat::handleAsyncUpdate()
{
    std::vector<Stroke> got;
    { const std::lock_guard<std::mutex> g (queueLock); got.swap (queue); }
    bool movedOnly = false;
    for (auto& s : got)
    {
        if (learningNow != Learn::none)
        {
            const bool wantKeyboard = learningNow == Learn::keyboard;
            if (s.keyboard != wantKeyboard) continue;
            Device d; d.number = s.device; d.keyboard = s.keyboard;
            for (auto& x : devices()) if (x.number == s.device) d.hardwareId = x.hardwareId;
            learningNow = Learn::none; setMode (0); stopTimer();
            if (countOf (wantKeyboard) < 2)
            { if (say) say (juce::String ("That is the only ") + (wantKeyboard ? "keyboard" : "mouse") + " the PC has: the Fixer needs one of its own."); }
            else
            {
                (wantKeyboard ? chosenKeyboard : chosenMouse) = d;
                saveChoice();
                if (say) say ((wantKeyboard ? "The Fixer's keyboard: " : "The Fixer's mouse: ") + d.describe());
            }
            if (onChanged) onChanged();
            continue;
        }
        if (! on) continue;
        if (s.keyboard) { if (movedOnly) { sendMouse(); movedOnly = false; } applyKey (s); }
        else
        {
            const bool justMove = (s.state & (icp::leftDown | icp::leftUp | icp::rightDown | icp::rightUp | icp::middleDown | icp::middleUp | icp::wheel | icp::hwheel)) == 0;
            if (! justMove && movedOnly) { sendMouse(); movedOnly = false; }  // (the moves before a click or a release reach the window first: a drag's last moves are not lost)
            applyMouse (s);
            if (justMove) movedOnly = true;                                   // (many small moves in one go: one event for them all)
            else { movedOnly = false; }
        }
        if (! on) break;                                                       // (Esc five times: let go)
    }
    if (movedOnly && on) sendMouse();
}

void SecondSeat::clampToArea()
{
    auto a = area ? area() : juce::Rectangle<int>();
    if (a.isEmpty()) return;
    pos.x = juce::jlimit ((float) a.getX(), (float) a.getRight() - 1.0f, pos.x);
    pos.y = juce::jlimit ((float) a.getY(), (float) a.getBottom() - 1.0f, pos.y);
}

/** The seat's own Shift / Ctrl / Alt, for the moment one of its clicks, drags or wheel turns is handed on: JUCE takes a mouse event's keys from its
    global modifiers, so the seat keyboard's stand in for them, and the Primary keyboard's are put back at once. */
/** A click of the seat's never makes a window of the Fixer the PC's active one - the Primary's keyboard would follow it there. So for the moment it
    is handed on, what is clicked does not bring its window to the front as the active one, nor take the PC's keyboard (the seat's keys go where
    it clicked: keyTarget); the window is raised, not activated. */
/** The component a click at this place reaches, found exactly as JUCE finds it (the same scaling and the same float hit test): what it goes to
    is what NoActivate must quieten, and what the seat's keys then go to. */
static juce::Component* hitAt (juce::ComponentPeer& peer, juce::Point<float> screenPos)
{
    auto& comp = peer.getComponent();
    auto local = peer.globalToLocal (screenPos);
    if (const float scale = comp.getDesktopScaleFactor(); scale != 1.0f) local /= scale;   // (JUCE's unscaledScreenPosToScaled)
    return comp.contains (local) ? comp.getComponentAt (local) : nullptr;
}

struct NoActivate
{
    NoActivate (juce::Component& top, juce::Component* hit)
    {
        for (auto* c = hit != nullptr ? hit : &top; c != nullptr; c = c->getParentComponent())
            if (c->isBroughtToFrontOnMouseClick()) { front.add (c); c->setBroughtToFrontOnMouseClick (false); }
        if (hit != nullptr && hit->getMouseClickGrabsKeyboardFocus()) { focus = hit; hit->setMouseClickGrabsKeyboardFocus (false); }
        auto& desk = juce::Desktop::getInstance();                           // (raised above the Fixer's other windows, if it is not the front one)
        for (int i = desk.getNumComponents(); --i >= 0;)
            if (auto* c = desk.getComponent (i); c != nullptr && c->isVisible() && c->getPeer() != nullptr
                                                  && (c->getPeer()->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) == 0)
            {
                if (c != &top) top.toFront (false);
                break;
            }
    }
    ~NoActivate()
    {
        for (auto& c : front) if (c != nullptr) c->setBroughtToFrontOnMouseClick (true);
        if (focus != nullptr) focus->setMouseClickGrabsKeyboardFocus (true);
    }
    juce::Array<juce::Component::SafePointer<juce::Component>> front;
    juce::Component::SafePointer<juce::Component> focus;
};

struct SeatKeys
{
    explicit SeatKeys (int keyMods) : saved (juce::ModifierKeys::currentModifiers)
    {
        const int keyFlags = juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier | juce::ModifierKeys::commandModifier;
        juce::ModifierKeys::currentModifiers = saved.withoutFlags (keyFlags).withFlags (keyMods);
    }
    ~SeatKeys() { juce::ModifierKeys::currentModifiers = saved; }
    const juce::ModifierKeys saved;
};

/** JUCE makes a pen's input source on the first pen event a window hands it, and drops that event: the seat's very first event would be lost
    (its first click, when the pointer had not moved over a window of the Fixer's before). So the source is made first, by an event of its own. */
static void makePenSource (juce::ComponentPeer& peer, juce::Point<float> local)
{
    for (auto& s : juce::Desktop::getInstance().getMouseSources())
        if (s.isPen()) return;
    peer.handleMouseEvent (juce::MouseInputSource::InputSourceType::pen, local, juce::ModifierKeys(), juce::MouseInputSource::defaultPressure,
                           juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
}

void SecondSeat::applyMouse (const Stroke& s)
{
    if ((s.flags & icp::moveAbsolute) != 0)
    {   // (a tablet, or a remote desktop: 0 - 65535 across the screen)
        const auto a = area ? area() : juce::Rectangle<int> (0, 0, 800, 600);
        pos = { (float) a.getX() + (float) s.x / 65535.0f * (float) a.getWidth(), (float) a.getY() + (float) s.y / 65535.0f * (float) a.getHeight() };
    }
    else if (s.x != 0 || s.y != 0)
    {   // a gentle acceleration, as Windows gives the ordinary pointer: small moves exact, quick ones further
        const double dx = s.x, dy = s.y, mag = std::sqrt (dx * dx + dy * dy);
        const double accel = mag > 2.0 ? 1.0 + juce::jmin (1.5, (mag - 2.0) * 0.08) : 1.0;
        pos += juce::Point<float> ((float) (dx * speed * accel), (float) (dy * speed * accel));
    }
    clampToArea();
    if (pointer != nullptr)
    {
        pointer->setTopLeftPosition ((int) std::floor (pos.x) + 1, (int) std::floor (pos.y) + 1);
        auto& desk = juce::Desktop::getInstance();                           // (a window the Fixer opened since - the mixer, say - came over it)
        if (desk.getNumComponents() > 0 && desk.getComponent (desk.getNumComponents() - 1) != pointer.get()) pointer->toFront (false);
    }
    const int before = buttons;
    if ((s.x != 0 || s.y != 0) && (s.state & (icp::leftDown | icp::leftUp | icp::rightDown | icp::rightUp | icp::middleDown | icp::middleUp)) != 0)
        sendMouse();                                                          // (a stroke that moves and clicks: the move first, with the buttons as they were)
    if (s.state & icp::leftDown) buttons |= juce::ModifierKeys::leftButtonModifier;
    if (s.state & icp::leftUp) buttons &= ~juce::ModifierKeys::leftButtonModifier;
    if (s.state & icp::rightDown) buttons |= juce::ModifierKeys::rightButtonModifier;
    if (s.state & icp::rightUp) buttons &= ~juce::ModifierKeys::rightButtonModifier;
    if (s.state & icp::middleDown) buttons |= juce::ModifierKeys::middleButtonModifier;
    if (s.state & icp::middleUp) buttons &= ~juce::ModifierKeys::middleButtonModifier;
    if (buttons != before) sendMouse();
    if ((s.state & (icp::wheel | icp::hwheel)) != 0 && list != nullptr)
    {
        if (list->over (pos) && (s.state & icp::wheel) != 0) list->wheel ((float) s.rolling);
        return;
    }
    if ((s.state & (icp::wheel | icp::hwheel)) != 0)
        if (auto* peer = peerAt (pos))
        {
            juce::MouseWheelDetails w;
            const float amount = 0.5f * (float) s.rolling / 256.0f;           // (as JUCE reads a Windows wheel)
            w.deltaX = (s.state & icp::hwheel) != 0 ? amount : 0.0f; w.deltaY = (s.state & icp::wheel) != 0 ? amount : 0.0f;
            w.isReversed = false; w.isSmooth = false; w.isInertial = false;
            const juce::ScopedValueSetter<bool> d (inDispatch, true);
            const SeatKeys keys (keyMods);                                     // (Ctrl + the wheel zooms: the seat's own Ctrl)
            makePenSource (*peer, peer->globalToLocal (pos));
            peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::pen, peer->globalToLocal (pos), juce::Time::currentTimeMillis(), w);
        }
}

juce::ComponentPeer* SecondSeat::peerAt (juce::Point<float> p) const
{
    auto& desk = juce::Desktop::getInstance();
    for (int i = desk.getNumComponents(); --i >= 0;)                      // (front to back)
    {
        auto* c = desk.getComponent (i);
        if (c == nullptr || c == pointer.get() || ! c->isVisible()) continue;
        if (auto* peer = c->getPeer())
        {
            if ((peer->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) != 0) continue;   // (a tooltip: clicks go through it)
            if (! peer->isMinimised() && c->getScreenBounds().toFloat().contains (p)) return peer;
        }
    }
    return nullptr;
}

void SecondSeat::sendMouse()
{
    if (list != nullptr && listMouse()) return;                            // (the seat's own drop-down list is open: its mouse works that)
    if (swallowPress) { if (buttons != 0) return; swallowPress = false; }   // (the press that closed the list: nothing else gets it)
    juce::ComponentPeer* peer = nullptr;
    if (dragPeer != nullptr && juce::ComponentPeer::isValidPeer (dragPeer) && buttons != 0) peer = dragPeer;   // (a drag stays with its window)
    if (peer == nullptr) peer = peerAt (pos);
    if (peer == nullptr) { dragPeer = nullptr; return; }                    // (over nothing of the Fixer's: the pointer only moves)
    const bool wasDown = dragPeer != nullptr;
    if (buttons != 0 && ! wasDown && openListAt (*peer, peer->globalToLocal (pos))) return;
    {
        const juce::ScopedValueSetter<bool> d (inDispatch, true);
        const SeatKeys keys (keyMods);
        std::unique_ptr<NoActivate> still;
        if (buttons != 0 && ! wasDown)
        {
            still = std::make_unique<NoActivate> (peer->getComponent(), hitAt (*peer, pos));
            if (pointer != nullptr) pointer->toFront (false);
        }
        makePenSource (*peer, peer->globalToLocal (pos));
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::pen, peer->globalToLocal (pos), juce::ModifierKeys (keyMods | buttons),
                                juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
    }
    if (! juce::ComponentPeer::isValidPeer (peer)) { dragPeer = nullptr; return; }
    if (buttons != 0 && ! wasDown)
    {   // a click: what the seat's keyboard types into from now on - the text box clicked (or whatever took the focus)
        dragPeer = peer;
        juce::Component* hit = hitAt (*peer, pos);
        while (hit != nullptr && ! hit->getWantsKeyboardFocus()) hit = hit->getParentComponent();
        if (auto* w = dynamic_cast<juce::ResizableWindow*> (hit)) if (auto* c = w->getContentComponent()) hit = c;   // (the window itself: its contents have its keys)
        keyTarget = hit;
    }
    if (buttons == 0) dragPeer = nullptr;
}

bool SecondSeat::openListAt (juce::ComponentPeer& peer, juce::Point<float> local)
{
    auto* hit = hitAt (peer, peer.localToGlobal (local));
    juce::ComboBox* box = nullptr;
    for (auto* c = hit; c != nullptr && box == nullptr; c = c->getParentComponent()) box = dynamic_cast<juce::ComboBox*> (c);
    if (box == nullptr || ! box->isEnabled() || box->isTextEditable() || box->getNumItems() == 0 || box->isCurrentlyBlockedByAnotherModalComponent()) return false;
    list = std::make_unique<SeatList> (*box, area ? area() : juce::Rectangle<int>());
    listHeld = true;                                                        // (the press that opened it: letting go on an item chooses it, as in a menu)
    if (pointer != nullptr) pointer->toFront (false);
    return true;
}

bool SecondSeat::listMouse()
{
    const bool inList = list->over (pos);
    list->hoverAt (pos);
    const bool pressed = buttons != 0 && ! listHeld, released = buttons == 0 && listHeld;
    listHeld = buttons != 0;
    if (pressed && ! inList) { closeList(); swallowPress = true; return true; }   // (a click anywhere else closes it, as a menu does, and does nothing more)
    if (released && inList) { list->choose(); closeList(); }
    return true;
}

void SecondSeat::closeList()
{
    list.reset();
    listHeld = false;
}

/** A key of the seat's keyboard as JUCE names it (Windows: from its scan code, with the seat's own shift for the character it types). */
static juce::KeyPress keyFromScan (unsigned short scan, bool e0, int mods)
{
   #if FERMATA_SEAT_WINDOWS
    // the number pad (with Num Lock on, as the Fixer will want numbers there)
    if (! e0)
        switch (scan)
        {
            case 0x47: return juce::KeyPress (juce::KeyPress::numberPad7, juce::ModifierKeys (mods), '7');
            case 0x48: return juce::KeyPress (juce::KeyPress::numberPad8, juce::ModifierKeys (mods), '8');
            case 0x49: return juce::KeyPress (juce::KeyPress::numberPad9, juce::ModifierKeys (mods), '9');
            case 0x4B: return juce::KeyPress (juce::KeyPress::numberPad4, juce::ModifierKeys (mods), '4');
            case 0x4C: return juce::KeyPress (juce::KeyPress::numberPad5, juce::ModifierKeys (mods), '5');
            case 0x4D: return juce::KeyPress (juce::KeyPress::numberPad6, juce::ModifierKeys (mods), '6');
            case 0x4F: return juce::KeyPress (juce::KeyPress::numberPad1, juce::ModifierKeys (mods), '1');
            case 0x50: return juce::KeyPress (juce::KeyPress::numberPad2, juce::ModifierKeys (mods), '2');
            case 0x51: return juce::KeyPress (juce::KeyPress::numberPad3, juce::ModifierKeys (mods), '3');
            case 0x52: return juce::KeyPress (juce::KeyPress::numberPad0, juce::ModifierKeys (mods), '0');
            case 0x53: return juce::KeyPress (juce::KeyPress::numberPadDecimalPoint, juce::ModifierKeys (mods), '.');
            case 0x4E: return juce::KeyPress (juce::KeyPress::numberPadAdd, juce::ModifierKeys (mods), '+');
            case 0x4A: return juce::KeyPress (juce::KeyPress::numberPadSubtract, juce::ModifierKeys (mods), '-');
            case 0x37: return juce::KeyPress (juce::KeyPress::numberPadMultiply, juce::ModifierKeys (mods), '*');
            default: break;
        }
    if (e0 && scan == 0x35) return juce::KeyPress (juce::KeyPress::numberPadDivide, juce::ModifierKeys (mods), '/');
    const UINT vk = MapVirtualKeyW ((UINT) scan | (e0 ? 0xE000u : 0u), 3 /* MAPVK_VSC_TO_VK_EX */);
    if (vk == 0) return {};
    int code = 0;
    switch (vk)
    {
        case VK_RETURN: code = juce::KeyPress::returnKey; break;
        case VK_ESCAPE: code = juce::KeyPress::escapeKey; break;
        case VK_TAB:    code = juce::KeyPress::tabKey; break;
        case VK_SPACE:  code = juce::KeyPress::spaceKey; break;
        case VK_BACK:   code = juce::KeyPress::backspaceKey; break;
        case VK_DELETE: code = juce::KeyPress::deleteKey; break;
        case VK_INSERT: code = juce::KeyPress::insertKey; break;
        case VK_LEFT:   code = juce::KeyPress::leftKey; break;
        case VK_RIGHT:  code = juce::KeyPress::rightKey; break;
        case VK_UP:     code = juce::KeyPress::upKey; break;
        case VK_DOWN:   code = juce::KeyPress::downKey; break;
        case VK_HOME:   code = juce::KeyPress::homeKey; break;
        case VK_END:    code = juce::KeyPress::endKey; break;
        case VK_PRIOR:  code = juce::KeyPress::pageUpKey; break;
        case VK_NEXT:   code = juce::KeyPress::pageDownKey; break;
        case VK_F1: code = juce::KeyPress::F1Key; break;   case VK_F2: code = juce::KeyPress::F2Key; break;   case VK_F3: code = juce::KeyPress::F3Key; break;
        case VK_F4: code = juce::KeyPress::F4Key; break;   case VK_F5: code = juce::KeyPress::F5Key; break;   case VK_F6: code = juce::KeyPress::F6Key; break;
        case VK_F7: code = juce::KeyPress::F7Key; break;   case VK_F8: code = juce::KeyPress::F8Key; break;   case VK_F9: code = juce::KeyPress::F9Key; break;
        case VK_F10: code = juce::KeyPress::F10Key; break; case VK_F11: code = juce::KeyPress::F11Key; break; case VK_F12: code = juce::KeyPress::F12Key; break;
        default:
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) code = (int) vk;
            else
            {
                const UINT ch = MapVirtualKeyW (vk, 2 /* MAPVK_VK_TO_CHAR */) & 0x7fffu;
                if (ch == 0) return {};
                code = (int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) ch);
            }
            break;
    }
    juce::juce_wchar text = 0;
    if ((mods & (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier)) == 0)
    {   // what it types, with the seat keyboard's own shift (not the Primary's), on this PC's keyboard layout
        BYTE state[256] = {};
        if ((mods & juce::ModifierKeys::shiftModifier) != 0) state[VK_SHIFT] = 0x80;
        WCHAR out[4] = {};
        if (ToUnicodeEx (vk, (UINT) scan, state, out, 4, 4 /* leave the keyboard's dead-key state alone */, GetKeyboardLayout (0)) == 1 && out[0] >= 32)
            text = (juce::juce_wchar) out[0];
    }
    return juce::KeyPress (code, juce::ModifierKeys (mods), text);
   #else
    juce::ignoreUnused (scan, e0, mods);
    return {};
   #endif
}

void SecondSeat::applyKey (const Stroke& s)
{
    const bool up = (s.state & icp::keyUp) != 0, e0 = (s.state & icp::keyE0) != 0;
    if (s.juceKey == 0)
    {
        bool modifier = true;
        switch (s.code)
        {
            // 0.8.0: only the real Shift keys. A keyboard on the PS/2 plug (a laptop's own, say) sends a "pretend" Shift - the same code with the
            // E0 mark - round the arrows, Home, End, Insert, Delete and the Page keys while Num Lock is on: taken for the Shift key, every arrow
            // was Shift + arrow. It is kept from Windows as before, and used for nothing.
            case 0x2A: if (! e0) lShift = ! up; break;
            case 0x36: if (! e0) rShift = ! up; break;
            case 0x1D: (e0 ? rCtrl : lCtrl) = ! up; break;
            case 0x38: (e0 ? rAlt : lAlt) = ! up; break;
            case 0x5B: case 0x5C: case 0x5D: modifier = e0; break;            // (the Windows keys and the menu key: kept from Windows, used for nothing)
            default: modifier = false; break;
        }
        keyMods = ((lShift || rShift) ? juce::ModifierKeys::shiftModifier : 0) | ((lCtrl || rCtrl) ? juce::ModifierKeys::ctrlModifier : 0)
                | ((lAlt || rAlt) ? juce::ModifierKeys::altModifier : 0);
        if (modifier) { if (buttons != 0) sendMouse(); return; }              // (Alt pressed in the middle of a drag counts from now on)
    }
    if (up) return;
    // Esc five times within two seconds: the seat lets go of both devices at once (whoever's they turned out to be)
    if ((s.juceKey == 0 && s.code == 0x01 && ! e0) || s.juceKey == juce::KeyPress::escapeKey)
    {
        const auto now = juce::Time::getMillisecondCounter();
        escapes.push_back (now);
        escapes.erase (std::remove_if (escapes.begin(), escapes.end(), [now] (juce::uint32 t) { return now - t > 2000; }), escapes.end());
        if (escapes.size() >= 5)
        {
            stop (true);
            if (say) say ("Esc five times: the second seat let go - the Fixer's keyboard and mouse are ordinary again.");
            return;
        }
    }
    const auto k = s.juceKey != 0 ? juce::KeyPress (s.juceKey, juce::ModifierKeys (keyMods), s.juceChar) : keyFromScan (s.code, e0, keyMods);
    if (k.isValid()) deliverKey (k);
}

void SecondSeat::deliverKey (const juce::KeyPress& k)
{
    if (list != nullptr)
    {   // the seat's drop-down list is open: its keys work that (Enter chooses, Esc closes it), and go nowhere else meanwhile
        if (k.getKeyCode() == juce::KeyPress::escapeKey) closeList();
        else if (list->key (k)) { list->choose(); closeList(); }
        return;
    }
    // where it goes: what the seat last clicked or opened (a text box, the timeline, a window's contents), else the Fixer itself. Never what has
    // the PC's keyboard: that is the Primary's (the seat's clicks never take it)
    juce::Component* target = keyTarget.getComponent();
    if (target != nullptr && ! target->isShowing()) target = nullptr;
    if (target == nullptr && home) target = home();
    if (target == nullptr) return;
    if (target->isCurrentlyBlockedByAnotherModalComponent())
        if (auto* modal = juce::Component::getCurrentlyModalComponent()) target = modal;
    // 0.10.16: the keys in use (HotKeys): a changed key is handed on as the standard one, a standard key whose job moved does nothing
    juce::KeyPress key;
    if (! keymap::Translator::get().translateFor (target, k, key)) return;
    const juce::ScopedValueSetter<bool> d (inDispatch, true);
    for (juce::Component* c = target; c != nullptr; c = c->getParentComponent())
    {
        juce::Component::SafePointer<juce::Component> alive (c);
        if (c->keyPressed (key)) break;
        if (alive == nullptr) break;
    }
}
} // namespace td::live
