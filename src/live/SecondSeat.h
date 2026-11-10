#pragma once
/** fermata:Live 0.6.0: the SECOND SEAT - a mouse and a keyboard of the Fixer's own, on the same PC as the Primary's.

    Windows makes every mouse move the one pointer and sends every keyboard to whichever window is active, so two people cannot share a PC that way.
    The Interception driver (installed by the user: see docs/LIVE-SECOND-SEAT.md) lets one program take chosen devices for itself: Windows then never
    sees them. The Fixer takes its own mouse and keyboard this way. Its mouse moves an orange pointer of its own, kept on the Fixer's screen; its
    clicks, drags and wheel reach only the Fixer's windows (JUCE's pen source: a pointer of its own beside the real mouse, so the two never fight);
    its keys go to the Fixer only. The Primary's mouse and keyboard are never touched: only the chosen devices are filtered.

    The PC's own keyboard stays the Primary's: the seat's clicks never make a Fixer window the active one (nothing they click takes the PC's
    keyboard; the seat's keys go to what the seat last clicked or opened), and on Windows a Fixer window that becomes the active one anyway (one it
    opened, say) hands the PC's keyboard straight back to the window that had it (keepForeground). JUCE's drop-down menus follow the ordinary mouse
    only, so a drop-down list clicked with the seat opens the seat's own list of its items (SeatList).

    Safety, because this runs on the recording PC:
      - the recorder never uses it; only the Fixer's process does, and closing (or a crash of) the Fixer gives the devices back to Windows;
      - learning which device is the Fixer's passes every stroke straight on (key presses or left clicks only, for 15 s at most);
      - a keyboard or mouse is refused when it is the only one the driver sees;
      - Esc pressed five times quickly on the Fixer's keyboard, or Let go in the panel (clickable with either mouse), gives them back at once.

    Threads: the driver's strokes are read on the seat's own thread; the message thread only asks the driver which devices there are (devices(),
    under driverLock, so the two never overlap in the library). Everything else is the message thread's. The strokes cross over in a queue under
    a lock held only to move them. */
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace td::live
{
class SecondSeat final : private juce::Thread, private juce::AsyncUpdater, private juce::Timer
{
public:
    /** One device the driver sees: its number (1-10 keyboards, 11-20 mice) and its hardware id. */
    struct Device
    {
        int number = 0; juce::String hardwareId; bool keyboard = false;
        bool valid() const noexcept { return number > 0; }
        /** "Keyboard 2 (VID 046D, PID C31C)" */
        juce::String describe() const;
    };

    explicit SecondSeat (juce::PropertiesFile& settings);
    ~SecondSeat() override;

    // ---- the driver
    /** The driver is there (its library loaded and its devices answering); else why not, in words for the panel. */
    bool driverPresent() const noexcept             { return problem.isEmpty(); }
    const juce::String& driverProblem() const noexcept { return problem; }
    /** What the driver sees now. */
    std::vector<Device> devices() const;

    // ---- choosing the Fixer's devices
    enum class Learn { none, keyboard, mouse };
    /** The next key pressed on any keyboard (or left click on any mouse) chooses that device; everything is passed straight on meanwhile. 15 s. */
    void learn (Learn what);
    Learn learning() const noexcept                 { return learningNow; }
    Device keyboard() const                         { return chosenKeyboard; }
    /** The pointer's speed, kept in the settings. */
    void setSpeed (double s);
    Device mouse() const                            { return chosenMouse; }
    void forget (bool keyboardToo, bool mouseToo);

    // ---- on and off
    /** Takes the chosen devices for the Fixer (and remembers it: the next start of the Fixer takes them again). False and why when it cannot. */
    bool start (juce::String& whyNot);
    /** Gives them back to Windows ('remember': the next start does not take them). */
    void stop (bool remember = true);
    bool isOn() const noexcept                      { return on; }
    /** At the Fixer's start: takes them again if it was on when the Fixer closed (and the devices are there). */
    void startIfWanted();

    // ---- what the Fixer tells it
    std::function<juce::Rectangle<int>()> area;     // the screen area the pointer stays in (the Fixer's screen)
    std::function<juce::Component*()> home;         // where the keys go when nothing else of the Fixer has them (its main component)
    std::function<void()> onChanged;                // anything the panel shows changed
    std::function<void (const juce::String&)> say;  // a line for the Fixer's status
    juce::String label;                              // the name beside the pointer
    double speed = 1.5;                              // the pointer's speed (1 = one pixel a count; setSpeed keeps it)
    /** True while the seat's own input is being handed on (so the Fixer knows a key or click came from its own seat). */
    bool dispatching() const noexcept               { return inDispatch; }
    /** The component the seat's keyboard types into from now on (a text box it opened, say). */
    void setKeyTarget (juce::Component* c)          { keyTarget = c; }

private:
    struct Stroke
    {
        int device = 0; bool keyboard = false;
        unsigned short state = 0, flags = 0, code = 0; short rolling = 0; int x = 0, y = 0;
        int juceKey = 0; juce::juce_wchar juceChar = 0; int juceMods = 0;        // (the Linux test feed: a key already mapped)
    };
    class PointerWindow;
    class SeatList;
    struct Api;

    void run() override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void applyMouse (const Stroke&);
    void applyKey (const Stroke&);
    void sendMouse();
    void deliverKey (const juce::KeyPress&);
    juce::ComponentPeer* peerAt (juce::Point<float>) const;
    void clampToArea();
    void showPointer (bool);
    bool openListAt (juce::ComponentPeer&, juce::Point<float> local);   // a drop-down list clicked: the seat's own list of its items opens
    bool listMouse();                                                    // the seat's mouse while that list is open (true: used)
    void closeList();
    void keepForeground();                                               // (Windows: the PC's keyboard stays where it was while the seat has its own)
    void checkDevices();                                                 // (a chosen device gone from where it was: the seat lets go, and says so)
    void drain (bool sendOn);                                            // (the seat's thread: strokes the driver still holds under the old filter)
    Device find (const juce::String& id, int number, bool keyboard) const;
    void saveChoice();
    int countOf (bool keyboard) const;
    void setMode (int m);                            // 0 off, 1 learn keyboard, 2 learn mouse, 3 the seat is on

    juce::PropertiesFile& settings;
    std::unique_ptr<Api> api;
    void* context = nullptr;
    juce::String problem;
    std::atomic<int> mode { 0 }, appliedMode { -1 };
    std::atomic<int> seatKeyboard { 0 }, seatMouse { 0 };
    std::mutex queueLock;
    std::vector<Stroke> queue;
    mutable std::mutex driverLock;                   // (the driver's calls: the seat's thread and devices() on the message thread never overlap)
    int ticks = 0;                                   // (the timer's ticks while on: checkDevices once a second)
    Learn learningNow = Learn::none;
    juce::uint32 learnUntil = 0;
    Device chosenKeyboard, chosenMouse;
    bool on = false, inDispatch = false;
    // the pointer and the buttons
    juce::Point<float> pos;
    int buttons = 0, keyMods = 0;                   // juce::ModifierKeys flags: the seat's mouse buttons, its keyboard's shift / ctrl / alt
    bool lShift = false, rShift = false, lCtrl = false, rCtrl = false, lAlt = false, rAlt = false;
    juce::Component::SafePointer<juce::Component> keyTarget;
    juce::ComponentPeer* dragPeer = nullptr;
    std::unique_ptr<PointerWindow> pointer;
    std::unique_ptr<SeatList> list;                  // (JUCE's drop-down menus follow the ordinary mouse, never a second pointer: the seat shows its own)
    bool listHeld = false, swallowPress = false;
    void* otherWindow = nullptr;                     // (Windows: the window outside the Fixer that last had the PC's keyboard)     // (the seat's button held while the list is open; a press that closed it goes no further)
    std::vector<juce::uint32> escapes;               // (Esc five times quickly lets go)
    juce::String testFeed;                           // (Linux only: a file of fake strokes, for trying it without the driver)
    juce::int64 testFeedRead = 0;
};
} // namespace td::live
