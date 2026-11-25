#pragma once
/** 3.64.0 (asked for on 24 November 2026): "WITH THE EXCEPTION of the 'Take Display' screen, we absolutely should make it so that all the windows
    open over the window you clicked in". The user's PC has its HDMI output always plugged into a fibre distribution box, so Windows thinks there
    are two screens even when nothing shows the second one, and a box that opened there (Make a stereo track, 24 November) left fermata waiting
    for an answer nobody could see.

    Why that box went there: JUCE centres a box on the MIDDLE of the component it is given (TopLevelWindow::centreAroundComponent) and keeps it on
    the screen that holds most of that component. A take or edit window's timeline is far wider than the window (it scrolls inside it), so its
    middle - and most of it - can lie to the right of the window, on the second screen. And a window with nothing to go by was centred on Windows'
    main screen (centreWithSize), whichever screen fermata was on.

    What this does:
    - Tracker: the window you last clicked in (a click in the menu bar counts as the main window; a click in a menu, a call-out or a Take Display
      screen does not change it).
    - anchor (c): the window a new box or window opens over - the window 'c' is in (not 'c' itself), else the one you last clicked in, else the
      active one, else the main window.
    - over (w, anchor): centres w on the part of the anchor that is on its screen, all of w on that screen.
    - sameScreen: is a remembered place on the anchor's screen (a window's remembered place is used only then).
    - The net (Tracker's timer, 20 times a second): any other new window (a TopLevelWindow: boxes, dialogs, the program's windows, plug-in windows)
      that appears on a different screen from the anchor is moved over it, and the log says so. A window that appears on the anchor's screen is left
      exactly where it opened. Components that are not TopLevelWindows - menus, call-outs, tooltips, the splash, the menu bar, the Take Display
      screens - are never moved. fermata only (fermata:Live does not include it). */
#include <juce_gui_basics/juce_gui_basics.h>

namespace td::place
{
/** Marks a component (the menu bar) whose clicks count as clicks in the main window. */
inline const juce::Identifier kPartOfMain { "tdPlacePartOfMain" };
/** Marks a window that places itself (a Take Display screen): never moved, and a click in it does not change the anchor. */
inline const juce::Identifier kOwnPlace { "tdPlaceOwn" };
/** Set on every window the net has looked at (kept on the window itself, so a new window at a freed window's address is still new). */
inline const juce::Identifier kSeen { "tdPlaceSeen" };
inline void partOfMain (juce::Component& c) { c.getProperties().set (kPartOfMain, true); }
inline void ownPlace (juce::Component& c)   { c.getProperties().set (kOwnPlace, true); }

inline bool usable (juce::Component* c)
{
    if (c == nullptr || ! c->isShowing()) return false;
    auto* peer = c->getPeer();
    return peer != nullptr && ! peer->isMinimised();
}

inline const juce::Displays::Display* displayOf (juce::Rectangle<int> r)
{
    return juce::Desktop::getInstance().getDisplays().getDisplayForRect (r);
}

class Tracker : private juce::MouseListener, private juce::Timer, private juce::FocusChangeListener
{
public:
    static Tracker& get() { static Tracker t; return t; }

    /** The application calls this once its main window exists, and stop() before it goes. */
    void start (juce::Component* mainWindow)
    {
        main = mainWindow;
        if (running) return;
        running = true;
        juce::Desktop::getInstance().addGlobalMouseListener (this);
        juce::Desktop::getInstance().addFocusChangeListener (this);
        markExisting();                                                  // (what is open now - the main window, the splash - is not new)
        startTimerHz (20);
    }
    void stop()
    {
        if (! running) return;
        running = false;
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener (this);
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        main = nullptr; last = nullptr;
    }

    /** The window a new box or window opens over (see the top of the file). 'notThis': a window that cannot be the answer (the net asks for
        the anchor of a window that is already on the screen - and may itself be the active one by then). */
    juce::Component* anchor (juce::Component* hint = nullptr, const juce::Component* notThis = nullptr) const
    {
        if (hint != nullptr)
            if (auto* t = windowOf (hint); usable (t) && t != notThis) return t;
        if (auto* l = last.getComponent(); usable (l) && l != notThis) return l;
        if (auto* a = juce::TopLevelWindow::getActiveTopLevelWindow(); usable (a) && a != notThis && ! a->getProperties().contains (kOwnPlace)) return a;
        if (auto* m = main.getComponent(); usable (m) && m != notThis) return m;
        return nullptr;
    }
    juce::Component* mainWindow() const { return main.getComponent(); }

private:
    Tracker() = default;
    ~Tracker() override { stop(); }

    /** The window a component is in, as an anchor: the menu bar stands for the main window; a menu, a call-out or a Take Display screen for nothing. */
    juce::Component* windowOf (juce::Component* c) const
    {
        if (c == nullptr) return nullptr;
        auto* top = c->getTopLevelComponent();
        if (top == nullptr || top->getProperties().contains (kOwnPlace)) return nullptr;
        if (top->getProperties().contains (kPartOfMain)) return main.getComponent();
        return dynamic_cast<juce::TopLevelWindow*> (top) != nullptr ? top : nullptr;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (auto* w = windowOf (e.eventComponent)) last = w;
    }
    void globalFocusChanged (juce::Component*) override { check(); }    // (a new box takes the keyboard: looked at straight away, not at the next tick)
    void timerCallback() override { check(); }

    void markExisting()
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i) if (auto* c = d.getComponent (i)) c->getProperties().set (kSeen, true);
    }

    /** Every TopLevelWindow on the desktop not looked at before: moved over the anchor when it opened on another screen. */
    void check()
    {
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
        {
            auto* c = d.getComponent (i);
            if (c == nullptr || c->getProperties().contains (kSeen)) continue;
            if (! c->isVisible() || c->getPeer() == nullptr) continue;          // (not shown yet: looked at again next time)
            c->getProperties().set (kSeen, true);
            auto* tlw = dynamic_cast<juce::TopLevelWindow*> (c);
            if (tlw == nullptr || c == main.getComponent() || c->getProperties().contains (kOwnPlace)) continue;
            if (auto* dw = dynamic_cast<juce::DocumentWindow*> (c); dw != nullptr && (dw->isFullScreen() || dw->isMinimised())) continue;
            // (3.67.0: the anchor is never the new window itself. With no window clicked in yet - fermata worked by the keyboard since it started,
            // or the window last clicked in closed - the anchor was the active window, which by now is the new box: it was "over itself", and
            // was left on the other screen. Now the next in line is taken: the main window.)
            auto* anchorNow = anchor (nullptr, c);
            if (anchorNow == nullptr) continue;
            const auto* here = displayOf (c->getScreenBounds());
            const auto* there = displayOf (anchorNow->getScreenBounds());
            if (here == nullptr || there == nullptr || here->userArea == there->userArea) continue;
            over (*c, anchorNow);
            juce::Logger::writeToLog ("Window placement: '" + c->getName() + "' opened on another screen - moved over '" + anchorNow->getName() + "'");
        }
    }

public:
    /** Centres 'w' (a window on the desktop) over the part of 'anchor' that is on the anchor's screen, all of it on that screen (as far as it fits),
        its title bar too. */
    static void over (juce::Component& w, juce::Component* anchorWindow)
    {
        if (anchorWindow == nullptr) return;
        const auto ab = anchorWindow->getScreenBounds();
        const auto* disp = displayOf (ab);
        if (disp == nullptr) return;
        const auto area = disp->userArea;
        auto target = ab.getIntersection (area);
        if (target.isEmpty()) target = area;
        int frameTop = 0, frameSide = 0, frameBottom = 0;
        if (auto* peer = w.getPeer())
            if (const auto f = peer->getFrameSizeIfPresent()) { frameTop = f->getTop(); frameSide = f->getLeft(); frameBottom = f->getBottom(); }
        if (frameTop == 0)
            if (auto* dw = dynamic_cast<juce::DocumentWindow*> (&w); dw != nullptr && dw->isUsingNativeTitleBar()) frameTop = 32;     // (not on screen yet: Windows' title bar)
        auto b = w.getBounds().withCentre (target.getCentre());
        const auto room = area.withTrimmedTop (frameTop).withTrimmedBottom (frameBottom).reduced (frameSide, 0);
        b.setX (juce::jlimit (room.getX(), juce::jmax (room.getX(), room.getRight() - b.getWidth()), b.getX()));
        b.setY (juce::jlimit (room.getY(), juce::jmax (room.getY(), room.getBottom() - b.getHeight()), b.getY()));
        w.setTopLeftPosition (b.getPosition());
    }

private:
    juce::Component::SafePointer<juce::Component> main, last;
    bool running = false;
};

inline juce::Component* anchor (juce::Component* hint = nullptr) { return Tracker::get().anchor (hint); }
inline void over (juce::Component& w, juce::Component* anchorWindow) { Tracker::over (w, anchorWindow); }

/** Is a remembered place on the same screen as the anchor (so a window may go back to it)? With no anchor: on any screen. */
inline bool sameScreen (juce::Rectangle<int> remembered, juce::Component* anchorWindow)
{
    if (remembered.isEmpty()) return false;
    if (anchorWindow == nullptr) return true;
    const auto* a = displayOf (anchorWindow->getScreenBounds());
    const auto* r = displayOf (remembered);
    return a != nullptr && r != nullptr && a->userArea == r->userArea;
}

/** The usable area of the anchor's screen (the main screen when there is no anchor). */
inline juce::Rectangle<int> screenOf (juce::Component* anchorWindow)
{
    const auto& ds = juce::Desktop::getInstance().getDisplays();
    const auto* d = anchorWindow != nullptr ? ds.getDisplayForRect (anchorWindow->getScreenBounds()) : ds.getPrimaryDisplay();
    return d != nullptr ? d->userArea : juce::Rectangle<int> (0, 0, 1280, 800);
}
} // namespace td::place
