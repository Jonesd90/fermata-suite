#pragma once
/** fermata 3.71.0 / fermata:Live 0.10.18 (asked for on 26 November 2026, with a picture of the Mastering window's Listening copy page on a smaller
    screen, its "Your name" box a sliver at the bottom): "fermata should be able to detect the size of the screen, realize that that box won't be
    properly visible and either change the relative sizes to make it fit OR make a scroll bar. That should be program wide".

    Why the box was a sliver: a page lays its rows out from the top (r.removeFromTop (h) for each), and JUCE gives a row only what is left when the
    page runs out, so the last rows came out squashed - here about 10 pixels - with nothing to say so.

    What this does, for every window of both programs (the program's windows, its dialogs, fermata:Live's):
    - WindowFit: after a window lays its contents out, it looks for controls that came out squashed (shorter than they were on a roomy page, and too
      short to read or use) or cut off by the panel they are in. If there are any, the contents are laid out as tall as they need to be and the
      window gets a scroll bar (the mouse wheel and the keyboard's Tab move it, and it follows the control that has the keyboard). When the window is
      big enough again, the scroll bar goes. A page that already fits never changes. The contents stay the window's own contents (nothing is put
      round them), so everything that finds a window's contents still does.
    - Rows: how a page that stacks rows asks for them; when the page is short, the gaps between the rows shrink first (down to a least gap), and only
      then does the page need its scroll bar.
    - keepOnScreen: a window is never bigger than the screen it is on, and its title bar stays on it.
    - launchAsync: a dialog (JUCE's DialogWindow::LaunchOptions) opened as JUCE does it, but fitted as above.
    - audit: the small-screen check's eye (tools/fit-check.sh): what does not fit in a window as it is now.

    Contents that look after a short or narrow window themselves (the main window's strips, a take or edit window's timeline, a mixer's strips, a
    picture that simply gets smaller) carry kSelfFitting: the window leaves them alone, and inside other contents they count as one piece.

    Shared (both programs): it includes only JUCE. */
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include <functional>
#include <vector>
#include <typeinfo>

namespace td::fit
{
//==============================================================================================================================
// what a control needs
//==============================================================================================================================
inline const juce::Identifier kSelfFitting { "tdFitSelf" };
/** A button whose text is not drawn (it shows a picture; the text is its name): the check does not measure its text. */
inline const juce::Identifier kNoText { "tdFitNoText" };
/** A label that may be as narrow as the page leaves it (a hint or a status line, cut off on a narrow page on purpose): never a reason to scroll across. */
inline const juce::Identifier kMayShrink { "tdFitMayShrink" };
inline void markMayShrink (juce::Component& c) { c.getProperties().set (kMayShrink, true); }
inline void markSelfFitting (juce::Component& c, bool on = true) { c.getProperties().set (kSelfFitting, on); }
inline bool isSelfFitting (const juce::Component& c) { return (bool) c.getProperties().getWithDefault (kSelfFitting, false); }

/** A component checked as one piece: a control, something that scrolls its own insides, or a component with nothing inside it. */
inline bool isPiece (juce::Component& c)
{
    return c.getNumChildComponents() == 0 || isSelfFitting (c)
        || dynamic_cast<juce::Label*> (&c) != nullptr || dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr
        || dynamic_cast<juce::TextEditor*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::Viewport*> (&c) != nullptr
        || dynamic_cast<juce::ListBox*> (&c) != nullptr || dynamic_cast<juce::TreeView*> (&c) != nullptr || dynamic_cast<juce::ScrollBar*> (&c) != nullptr
        || dynamic_cast<juce::ProgressBar*> (&c) != nullptr;
}

/** The least height a piece can have and still be read and used - never more than it has on a roomy page ('roomy'). */
inline int leastHeight (juce::Component& c, int roomy)
{
    int need = 16;
    if (auto* l = dynamic_cast<juce::Label*> (&c)) need = l->getText().trim().isEmpty() ? 0 : juce::roundToInt (l->getFont().getHeight() * 0.85f);
    else if (auto* t = dynamic_cast<juce::TextEditor*> (&c)) need = juce::roundToInt (t->getFont().getHeight() * 0.9f);
    else if (dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr) need = 14;
    else if (dynamic_cast<juce::Slider*> (&c) != nullptr) need = 10;
    else if (dynamic_cast<juce::ProgressBar*> (&c) != nullptr || dynamic_cast<juce::ScrollBar*> (&c) != nullptr) need = 4;
    else if (isSelfFitting (c)) need = 60;
    else if (dynamic_cast<juce::Viewport*> (&c) != nullptr || dynamic_cast<juce::ListBox*> (&c) != nullptr || dynamic_cast<juce::TreeView*> (&c) != nullptr) need = 40;
    return juce::jmax (0, juce::jmin (need, roomy));
}

/** The least width a piece can have and still be read and used (its text squeezed as far as JUCE squeezes text - at most 100 pixels of a label's text,
    120 of a button's: a long line in a narrower box, a hint, is cut off on purpose) - never more than it has on a roomy page. */
/** The width of a line of text in a font (JUCE's own measure), remembered: measuring is slow, and a layout that counts the lines of its paragraphs
    measures the same text again at every size it is laid out at. (The message thread only.) */
inline float textWidth (const juce::Font& f, const juce::String& t)
{
    static std::unordered_map<juce::String, float> known;
    const auto key = f.toString() + "|" + t;
    if (auto it = known.find (key); it != known.end()) return it->second;
    if (known.size() > 2000) known.clear();
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (f, t);
    known[key] = w;
    return w;
}

struct TextWidthCache
{
    struct Entry { juce::String text; float height = 0.0f; float width = 0.0f; };
    std::unordered_map<const juce::Component*, Entry> at;
    /** The cache in use while a window is being fitted (measuring text is slow, and the same text is measured at every trial size); null otherwise. */
    static TextWidthCache*& current() { static TextWidthCache* c = nullptr; return c; }
};

inline int leastWidth (juce::Component& c, int roomy)
{
    auto widthOf = [&c] (const juce::Font& f, const juce::String& t)
    {
        auto* cache = TextWidthCache::current();
        if (cache != nullptr)
            if (auto it = cache->at.find (&c); it != cache->at.end() && it->second.text == t && it->second.height == f.getHeight()) return it->second.width;
        const float w = textWidth (f, t);
        if (cache != nullptr) cache->at[&c] = { t, f.getHeight(), w };
        return w;
    };
    int need = 16;
    if ((bool) c.getProperties().getWithDefault (kMayShrink, false)) need = 0;
    else if (auto* l = dynamic_cast<juce::Label*> (&c))
    {
        const auto t = l->getText().trim();
        const bool lines = (float) l->getHeight() >= l->getFont().getHeight() * 1.9f;
        need = t.isEmpty() ? 0 : lines ? 60 : juce::jmin (100, juce::roundToInt (widthOf (l->getFont(), t) * l->getMinimumHorizontalScale()) + l->getBorderSize().getLeftAndRight());
    }
    else if (auto* tb = dynamic_cast<juce::ToggleButton*> (&c))
        need = juce::jmin (120, juce::jmin (24, c.getHeight()) + juce::roundToInt (widthOf (juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) c.getHeight() * 0.75f))), tb->getButtonText()) * 0.7f) + 4);
    else if (auto* b = dynamic_cast<juce::TextButton*> (&c))
    {
        if (! (bool) b->getProperties().getWithDefault (kNoText, false))
            if (auto* lf = dynamic_cast<juce::TextButton::LookAndFeelMethods*> (&b->getLookAndFeel()))
                need = juce::jmin (120, juce::roundToInt (widthOf (lf->getTextButtonFont (*b, juce::jmax (1, b->getHeight())), b->getButtonText()) * 0.7f) + 6);
    }
    else if (dynamic_cast<juce::ComboBox*> (&c) != nullptr) need = 48;
    else if (dynamic_cast<juce::TextEditor*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr) need = 36;
    else if (dynamic_cast<juce::ProgressBar*> (&c) != nullptr || dynamic_cast<juce::ScrollBar*> (&c) != nullptr) need = 8;
    else if (isSelfFitting (c)) need = 80;
    else if (dynamic_cast<juce::Viewport*> (&c) != nullptr || dynamic_cast<juce::ListBox*> (&c) != nullptr || dynamic_cast<juce::TreeView*> (&c) != nullptr) need = 60;
    return juce::jmax (0, juce::jmin (need, roomy));
}

//==============================================================================================================================
// squashed: a page laid out roomy, then as it is
//==============================================================================================================================
/** Where everything was on a roomy page (each component's bounds in its parent). */
struct Roomy
{
    std::unordered_map<const juce::Component*, juce::Rectangle<int>> at;
    void take (juce::Component& root) { at.clear(); at[&root] = root.getBounds(); walk (root); }
private:
    void walk (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (! ch->isVisible()) continue;
            at[ch] = ch->getBounds();
            if (! isPiece (*ch)) walk (*ch);
        }
    }
};

/** Is anything on the page as it is now squashed, compared with the roomy page - down the page ('across' false: shorter than it was there and too
    short to use) or across it ('across' true: narrower and too narrow) - or cut off by the panel it is in, though all of it was inside on the roomy
    page. 'found' (optional) collects them. */
inline bool squashed (juce::Component& c, const Roomy& roomy, juce::Array<juce::Component*>* found = nullptr, bool across = false)
{
    auto len = [across] (juce::Rectangle<int> r) { return across ? r.getWidth() : r.getHeight(); };
    auto lo  = [across] (juce::Rectangle<int> r) { return across ? r.getX() : r.getY(); };
    auto hi  = [across] (juce::Rectangle<int> r) { return across ? r.getRight() : r.getBottom(); };
    bool any = false;
    const int parentLen = len (c.getLocalBounds());
    const auto pit = roomy.at.find (&c);
    const int roomyParentLen = pit != roomy.at.end() ? len (pit->second) : parentLen;
    for (auto* ch : c.getChildren())
    {
        if (! ch->isVisible()) continue;
        const auto it = roomy.at.find (ch);
        if (it == roomy.at.end()) continue;                              // (not on the roomy page - it came and went: nothing to go by)
        const auto was = it->second, now = ch->getBounds();
        const int least = across ? leastWidth (*ch, was.getWidth()) : leastHeight (*ch, was.getHeight());
        bool bad = false;
        if (isPiece (*ch) && len (now) + 1 < len (was) && len (now) < least) bad = true;
        // cut off by its panel: it was all inside it on the roomy page, and less of it than it needs shows now
        const bool wasInside = lo (was) >= -1 && hi (was) <= roomyParentLen + 1;
        const int shows = juce::jmin (hi (now), parentLen) - juce::jmax (lo (now), 0);
        if (wasInside && least > 0 && shows < juce::jmin (len (now), least)) bad = true;
        if (bad) { any = true; if (found != nullptr) found->add (ch); if (found == nullptr) return true; }
        if (! isPiece (*ch) && squashed (*ch, roomy, found, across)) { any = true; if (found == nullptr) return true; }
    }
    return any;
}

//==============================================================================================================================
// keepOnScreen
//==============================================================================================================================
/** The part of the screen a window's own bounds may use: the screen's usable area (no taskbar) less the window's frame when it has one round it
    (a native title bar). The screen is the one 'around' is on (else the one the window is on). */
inline juce::Rectangle<int> roomFor (juce::Component& w, juce::Component* around = nullptr)
{
    const auto& ds = juce::Desktop::getInstance().getDisplays();
    const auto* d = ds.getDisplayForRect (around != nullptr ? around->getScreenBounds() : w.getScreenBounds());
    if (d == nullptr) d = ds.getPrimaryDisplay();
    if (d == nullptr) return {};
    juce::BorderSize<int> frame;
    if (auto* tlw = dynamic_cast<juce::TopLevelWindow*> (&w); tlw != nullptr && tlw->isUsingNativeTitleBar())
    {
        frame = juce::BorderSize<int> (32, 8, 8, 8);                    // (before it is on the screen: about what Windows puts round it)
        if (auto* p = w.getPeer()) if (auto f = p->getFrameSizeIfPresent()) frame = *f;
    }
    return frame.subtractedFrom (d->userArea);
}

/** Makes a window no bigger than the screen it is on (or the screen 'around' is on), and moves it so that all of it is on that screen. */
inline void keepOnScreen (juce::Component& w, juce::Component* around = nullptr)
{
    if (auto* rw = dynamic_cast<juce::ResizableWindow*> (&w)) if (rw->isFullScreen() || rw->isKioskMode() || rw->isMinimised()) return;
    const auto room = roomFor (w, around);
    if (room.isEmpty()) return;
    auto b = w.getBounds();
    b.setSize (juce::jmin (b.getWidth(), room.getWidth()), juce::jmin (b.getHeight(), room.getHeight()));
    b.setPosition (juce::jlimit (room.getX(), room.getRight() - b.getWidth(), b.getX()), juce::jlimit (room.getY(), room.getBottom() - b.getHeight(), b.getY()));
    if (b != w.getBounds()) w.setBounds (b);
}

//==============================================================================================================================
// WindowFit
//==============================================================================================================================
/** Fits a window's contents to the window (see the top of this file). The window owns one and calls it from three places:
        void resized() override                     { Base::resized(); fitter.windowResized(); }
        void childBoundsChanged (Component* c) override { if (! fitter.adjusting()) Base::childBoundsChanged (c); }
        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override { if (! fitter.wheel (w)) Base::mouseWheelMove (e, w); }
    (the second: while the contents are being laid out at trial sizes, a window that follows its contents' size must not follow them).
    Down the page and across it alike: the contents are laid out as tall and as wide as they need to be, with a scroll bar for each way that does not
    fit (the one across along the bottom). */
class WindowFit : private juce::ScrollBar::Listener, private juce::Timer, private juce::FocusChangeListener
{
public:
    explicit WindowFit (juce::ResizableWindow& w) : win (w)
    {
        for (auto* b : { &vbar, &hbar }) { b->setAutoHide (false); b->addListener (this); b->setWantsKeyboardFocus (false); }
        for (auto* c : { &coverTop, &coverBottom, &coverLeft, &coverRight, &corner }) { c->owner = &win; c->setInterceptsMouseClicks (true, false); }
        corner.plain = true;
        juce::Desktop::getInstance().addFocusChangeListener (this);
        all()[&win] = this;
    }
    ~WindowFit() override { all().erase (&win); juce::Desktop::getInstance().removeFocusChangeListener (this); vbar.removeListener (this); hbar.removeListener (this); }
    /** The WindowFit of a window, or null. */
    static WindowFit* of (juce::Component* w) { auto it = all().find (w); return it != all().end() ? it->second : nullptr; }
    /** Told when the contents start or stop scrolling, or their size changes (the window may have things to move: its corner buttons). */
    std::function<void()> onChange;

    /** The window's contents were laid out to its size (its resized()): fit them. */
    void windowResized()
    {
        if (busy) return;
        if (! win.isFullScreen() && ! win.isKioskMode() && win.isOnDesktop() && win.isVisible() && ! shrinking)
        {
            const auto room = roomFor (win);
            if (! room.isEmpty() && (win.getWidth() > room.getWidth() || win.getHeight() > room.getHeight()))
            {   // contents that asked for more than the screen (a dialog that grew): the window is made to fit it, then this runs again
                juce::Component::SafePointer<juce::ResizableWindow> safe (&win);
                shrinking = true;
                juce::MessageManager::callAsync ([safe, this] { if (safe != nullptr) { keepOnScreen (*safe); shrinking = false; } });
            }
        }
        refit();
        startTimerHz (3);
    }
    bool adjusting() const noexcept { return busy; }
    /** The mouse wheel over the window, where nothing inside it used it: scrolls the contents when they scroll. */
    bool wheel (const juce::MouseWheelDetails& d)
    {
        if (! scrolling()) return false;
        const float sign = d.isReversed ? -1.0f : 1.0f;
        float dy = d.deltaY * sign, dx = d.deltaX * sign;
        if (! scrollsDown() && std::abs (dx) < 1.0e-6f) { dx = dy; dy = 0.0f; }               // (only across: the wheel scrolls across)
        setOffsets (offX - juce::roundToInt (dx * 120.0f), offY - juce::roundToInt (dy * 120.0f));
        return true;
    }
    bool scrolling() const noexcept { return scrollsDown() || scrollsAcross(); }
    bool scrollsDown() const noexcept { return needH > 0; }
    bool scrollsAcross() const noexcept { return needW > 0; }
    /** How tall / wide the contents are laid out when they scroll that way (0: they fit). */
    int neededHeight() const noexcept { return needH; }
    int neededWidth() const noexcept { return needW; }
    /** What was still squashed at the largest size tried (normally nothing): for the small-screen check. */
    const juce::Array<juce::Component::SafePointer<juce::Component>>& stillSquashed() const noexcept { return left; }
    /** What made the contents scroll down / across at the last fitting (the controls squashed at the window's own size): for the small-screen check. */
    const juce::Array<juce::Component::SafePointer<juce::Component>>& whyItScrollsDown() const noexcept { return whyDown; }
    const juce::Array<juce::Component::SafePointer<juce::Component>>& whyItScrollsAcross() const noexcept { return whyAcross; }
    /** How long the last fitting took (milliseconds): for the small-screen check. */
    double lastFitMs() const noexcept { return fitMs; }

    /** Looks again now (the window may call this when its contents change what they show, say a tab; a timer looks three times a second anyway). */
    void refit()
    {
        auto* c = win.getContentComponent();
        if (c == nullptr || busy) return;
        const auto area = contentArea();
        if (! c->isVisible() || area.getWidth() < 40 || area.getHeight() < 40 || isSelfFitting (*c)) { stop (c, area); return; }
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        busy = true;
        left.clear(); whyDown.clear(); whyAcross.clear();
        textWidths.at.clear();
        TextWidthCache::current() = &textWidths;
        const int W = area.getWidth(), H = area.getHeight();
        const int bigH = juce::jlimit (1600, 6000, juce::jmax (H * 2, H + 1000)), bigW = juce::jlimit (1400, 4000, juce::jmax (W * 3 / 2, W + 600));
        auto layoutAt = [&] (int w, int h) { c->setBounds (area.getX(), area.getY(), w, h); };
        auto note = [] (juce::Array<juce::Component::SafePointer<juce::Component>>& to, const juce::Array<juce::Component*>& from) { for (auto* x : from) to.add (x); };
        // (every trial size is a full layout of the contents, so as few as will do: three when the window fits - the roomy page each way, then
        // the window's own size, checked both ways)
        layoutAt (W, bigH); tall.take (*c);
        layoutAt (bigW, H); wide.take (*c);
        layoutAt (W, H);
        juce::Array<juce::Component*> fDown, fAcross;
        bool down = squashed (*c, tall, &fDown), across = squashed (*c, wide, &fAcross, true);
        note (whyDown, fDown); note (whyAcross, fAcross);
        int cw = W, ch = H;
        if (across)
        {   // as wide as the contents need (at the height left beside a scroll bar along the bottom); a few pixels short is let go
            const int visH = H - kBarW;
            int lo = W, hi = bigW;
            while (hi - lo > kStep) { const int mid = (lo + hi) / 2; layoutAt (mid, visH); if (squashed (*c, wide, nullptr, true)) lo = mid; else hi = mid; }
            cw = hi + 4;
            if (cw - (W - (down ? kBarW : 0)) <= kSlackW) { across = false; cw = W; }
        }
        const int visW = W - (down ? kBarW : 0);
        if (! across) cw = visW;                                       // (a scroll bar down takes its width from the contents: within the slack across)
        const int visH = H - (across ? kBarW : 0);
        if (across && ! down)
        {   // the bar along the bottom takes its height from the contents: does that squash them?
            layoutAt (cw, visH);
            if (squashed (*c, tall)) down = true;
        }
        if (down)
        {   // as tall as the contents need, at the width they have now
            if (cw != W) { layoutAt (cw, bigH); tall.take (*c); }
            int lo = visH, hi = bigH;
            while (hi - lo > kStep) { const int mid = (lo + hi) / 2; layoutAt (cw, mid); if (squashed (*c, tall)) lo = mid; else hi = mid; }
            ch = hi + 6;
        }
        TextWidthCache::current() = nullptr;
        if (! down && ! across)
        {
            layoutAt (W, H);
            busy = false; stop (c, area); fitMs = juce::Time::getMillisecondCounterHiRes() - t0; remember(); return;
        }
        needH = down ? ch : 0; needW = across ? cw : 0;
        if (! down) ch = visH;
        layoutAt (cw, ch);
        {   // (what is still squashed even now - normally nothing)
            juce::Array<juce::Component*> f;
            squashed (*c, tall, &f);
            note (left, f);
        }
        offX = juce::jlimit (0, juce::jmax (0, cw - (W - (down ? kBarW : 0))), offX);
        offY = juce::jlimit (0, juce::jmax (0, ch - visH), offY);
        c->setTopLeftPosition (area.getX() - offX, area.getY() - offY);
        placeBars (area, W - (down ? kBarW : 0), visH, cw, ch);
        busy = false;
        fitMs = juce::Time::getMillisecondCounterHiRes() - t0;
        remember();
        if (onChange) onChange();
    }

    static constexpr int kBarW = 14;
    static constexpr int kSlackW = 24;      // how far short across the contents may be before they scroll across
    static constexpr int kStep = 8;         // how closely the size the contents need is found (then a few pixels more)

private:
    struct Cover : public juce::Component
    {
        juce::ResizableWindow* owner = nullptr;
        bool plain = false;
        void paint (juce::Graphics& g) override
        {
            if (owner == nullptr) return;
            auto* dw = dynamic_cast<juce::DocumentWindow*> (owner);
            if (dw != nullptr && ! plain) { g.setOrigin (-getX(), -getY()); dw->paint (g); }     // (as the window draws itself there)
            else g.fillAll (owner->getBackgroundColour());
        }
    };
    juce::ResizableWindow& win;
    juce::ScrollBar vbar { true }, hbar { false };
    Cover coverTop, coverBottom, coverLeft, coverRight, corner;
    int needH = 0, needW = 0, offX = 0, offY = 0;
    bool busy = false, shrinking = false;
    juce::int64 sigWhich = 0, sigWhere = 0;
    double fitMs = 0.0;
    Roomy tall, wide;                       // the roomy pages of the last fitting (down: as wide, much taller; across: as tall, much wider)
    TextWidthCache textWidths;
    juce::Array<juce::Component::SafePointer<juce::Component>> left, whyDown, whyAcross;

    juce::Rectangle<int> contentArea() const { return win.getContentComponentBorder().subtractedFrom (win.getLocalBounds()); }
    static std::unordered_map<const juce::Component*, WindowFit*>& all() { static std::unordered_map<const juce::Component*, WindowFit*> m; return m; }
    int visibleW() const { return contentArea().getWidth() - (scrollsDown() ? kBarW : 0); }
    int visibleH() const { return contentArea().getHeight() - (scrollsAcross() ? kBarW : 0); }

    void stop (juce::Component* c, juce::Rectangle<int> area)
    {
        const bool was = scrolling();
        needH = needW = 0; offX = offY = 0;
        for (auto* x : std::initializer_list<juce::Component*> { &vbar, &hbar, &coverTop, &coverBottom, &coverLeft, &coverRight, &corner }) x->setVisible (false);
        if (was && c != nullptr && c->getBounds() != area) { busy = true; c->setBounds (area); busy = false; }
        if (was && onChange) onChange();
    }
    void placeBars (juce::Rectangle<int> area, int visW, int visH, int cw, int ch)
    {
        auto show = [this] (juce::Component& x, juce::Rectangle<int> r, bool on)
        {
            if (x.getParentComponent() != &win) win.addChildComponent (x);
            x.setBounds (r); x.setVisible (on && ! r.isEmpty()); x.toFront (false);
        };
        // the contents now reach past the content area, under the window's own edges: covered there (drawn as the window draws them)
        show (coverTop, juce::Rectangle<int> (0, 0, win.getWidth(), area.getY()), true);
        show (coverBottom, juce::Rectangle<int> (0, area.getBottom(), win.getWidth(), win.getHeight() - area.getBottom()), true);
        show (coverLeft, juce::Rectangle<int> (0, area.getY(), area.getX(), area.getHeight()), true);
        show (coverRight, juce::Rectangle<int> (area.getRight(), area.getY(), win.getWidth() - area.getRight(), area.getHeight()), true);
        show (vbar, juce::Rectangle<int> (area.getRight() - kBarW, area.getY(), kBarW, visH), scrollsDown());
        show (hbar, juce::Rectangle<int> (area.getX(), area.getBottom() - kBarW, visW, kBarW), scrollsAcross());
        show (corner, juce::Rectangle<int> (area.getRight() - kBarW, area.getBottom() - kBarW, kBarW, kBarW), scrollsDown() && scrollsAcross());
        vbar.setRangeLimits (0.0, (double) ch, juce::dontSendNotification); vbar.setCurrentRange ((double) offY, (double) visH, juce::dontSendNotification); vbar.setSingleStepSize (40.0);
        hbar.setRangeLimits (0.0, (double) cw, juce::dontSendNotification); hbar.setCurrentRange ((double) offX, (double) visW, juce::dontSendNotification); hbar.setSingleStepSize (40.0);
        if (auto* dw = dynamic_cast<juce::DocumentWindow*> (&win))       // (a title bar drawn by the window: its buttons stay on top)
            for (auto* b : { dw->getCloseButton(), dw->getMinimiseButton(), dw->getMaximiseButton() }) if (b != nullptr) b->toFront (false);
    }
    void setOffsets (int x, int y)
    {
        auto* c = win.getContentComponent();
        if (c == nullptr || ! scrolling()) return;
        const auto area = contentArea();
        x = scrollsAcross() ? juce::jlimit (0, juce::jmax (0, needW - visibleW()), x) : 0;
        y = scrollsDown() ? juce::jlimit (0, juce::jmax (0, needH - visibleH()), y) : 0;
        if (x == offX && y == offY) return;
        offX = x; offY = y;
        busy = true; c->setTopLeftPosition (area.getX() - offX, area.getY() - offY); busy = false;
        vbar.setCurrentRange ((double) offY, (double) visibleH(), juce::dontSendNotification);
        hbar.setCurrentRange ((double) offX, (double) visibleW(), juce::dontSendNotification);
    }
    void scrollBarMoved (juce::ScrollBar* b, double start) override
    {
        if (b == &vbar) setOffsets (offX, juce::roundToInt (start)); else setOffsets (juce::roundToInt (start), offY);
    }
    void globalFocusChanged (juce::Component* f) override
    {   // the control that has the keyboard (Tab) is scrolled into view
        auto* c = win.getContentComponent();
        if (f == nullptr || c == nullptr || ! scrolling() || ! c->isParentOf (f)) return;
        const auto r = c->getLocalArea (f, f->getLocalBounds());
        // all of it into view when it fits in the view; a bigger one (a picture that has the keyboard) only so that its top / left shows
        auto into = [] (int off, int start, int length, int view)
        {
            if (length > view - 16) return (start < off || start > off + view - 40) ? start - 8 : off;
            if (start < off) return start - 8;
            if (start + length > off + view) return start + length - view + 8;
            return off;
        };
        setOffsets (into (offX, r.getX(), r.getWidth(), visibleW()), into (offY, r.getY(), r.getHeight(), visibleH()));
    }
    /** What is showing inside the contents, and where (so a change is seen and the contents fitted again): which components are visible - a tab
        changed, a row appeared: fitted again at once - and where they are - something moved or grew: fitted again only if that squashed anything,
        checked against the roomy pages of the last fitting without laying anything out (a playhead that moves does not cost a layout). */
    void signatures (juce::int64& which, juce::int64& where) const
    {
        which = where = 0;
        auto* c = win.getContentComponent();
        if (c == nullptr) return;
        where = (juce::int64) c->getWidth() * 7919 + c->getHeight();
        std::function<void (juce::Component&)> walk = [&] (juce::Component& x)
        {
            for (auto* ch : x.getChildren())
            {
                if (! ch->isVisible()) continue;
                which = which * 1000003 + (juce::int64) (juce::pointer_sized_int) ch;
                const auto b = ch->getBounds();
                where = where * 31 + b.getX(); where = where * 31 + b.getY(); where = where * 31 + b.getWidth(); where = where * 31 + b.getHeight();
                if (! isPiece (*ch)) walk (*ch);
            }
        };
        walk (*c);
    }
    void remember() { signatures (sigWhich, sigWhere); }
    void timerCallback() override
    {
        if (! win.isShowing() || busy) return;
        if (juce::Desktop::getInstance().getMainMouseSource().isDragging()) return;     // (not in the middle of a drag: when it ends)
        juce::int64 which = 0, where = 0;
        signatures (which, where);
        if (which != sigWhich) { refit(); return; }
        if (where == sigWhere) return;
        auto* c = win.getContentComponent();
        if (c == nullptr) return;
        TextWidthCache::current() = &textWidths;
        const bool worse = squashed (*c, tall) || (! scrollsAcross() && squashed (*c, wide, nullptr, true));
        TextWidthCache::current() = nullptr;
        if (worse) refit(); else sigWhere = where;
    }
};

//==============================================================================================================================
// Rows: a page that stacks rows
//==============================================================================================================================
/** Rows down a page: rows (h) takes the next row of height h, with the gap below it. Made by stack(), which lays the page out twice: first to see how
    tall the rows want to be, then for real - with the gaps made smaller (down to the least gap) when that is more than the page has. */
class Rows
{
public:
    Rows (juce::Rectangle<int> a, int g) : area (a), gap (g), top (a.getY()) {}
    juce::Rectangle<int> operator() (int h) { auto x = area.removeFromTop (h); area.removeFromTop (gap); ++count; return x; }
    /** Extra space between two rows (a gap that is itself made smaller, in proportion, on a short page). */
    void space (int px) { area.removeFromTop (juce::roundToInt (px * spaceScale)); }
    /** Everything left below the rows taken so far. */
    juce::Rectangle<int> rest() const { return area; }
    int used() const { return area.getY() - top - (count > 0 ? gap : 0); }
    int rows() const { return count; }
    float spaceScale = 1.0f;
private:
    juce::Rectangle<int> area;
    int gap, top, count = 0;
};

/** Lays out a page of rows ('place' is called with a Rows, once or twice; it sets the bounds of the page's controls from it). */
template <typename Place>
inline void stack (juce::Rectangle<int> page, int gap, int leastGap, Place&& place)
{
    Rows m (page.withHeight (1000000), gap);
    place (m);
    const int want = m.used();
    if (want <= page.getHeight() || m.rows() < 2) { Rows r (page, gap); place (r); return; }
    const int over = want - page.getHeight();
    const int g = juce::jmax (leastGap, gap - (over + m.rows() - 2) / (m.rows() - 1));
    Rows r (page, g);
    r.spaceScale = gap > 0 ? (float) g / (float) gap : 1.0f;
    place (r);
}

//==============================================================================================================================
// dialogs
//==============================================================================================================================
/** JUCE's own dialog window (as DialogWindow::LaunchOptions makes it), fitted to the screen and to its contents (see the top of this file). */
class FittedDialog : public juce::DialogWindow
{
public:
    explicit FittedDialog (juce::DialogWindow::LaunchOptions& o)
        : juce::DialogWindow (o.dialogTitle, o.dialogBackgroundColour, o.escapeKeyTriggersCloseButton, true,
                              o.componentToCentreAround != nullptr ? juce::Component::getApproximateScaleFactorForComponent (o.componentToCentreAround) : 1.0f)
    {
        if (o.content.willDeleteObject()) setContentOwned (o.content.release(), true);
        else setContentNonOwned (o.content.release(), true);
        centreAroundComponent (o.componentToCentreAround, getWidth(), getHeight());
        setResizable (o.resizable, o.useBottomRightCornerResizer);
        setUsingNativeTitleBar (o.useNativeTitleBar);
        setAlwaysOnTop (juce::WindowUtils::areThereAnyAlwaysOnTopWindows());
        keepOnScreen (*this, o.componentToCentreAround);
    }
    void closeButtonPressed() override { setVisible (false); }
    void resized() override { juce::DialogWindow::resized(); fitter.windowResized(); }
    void childBoundsChanged (juce::Component* c) override { if (! fitter.adjusting()) juce::DialogWindow::childBoundsChanged (c); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (! fitter.wheel (w)) juce::DialogWindow::mouseWheelMove (e, w); }
    WindowFit fitter { *this };
};

/** As LaunchOptions::launchAsync: opens the dialog (modal, deleted when it closes), fitted. */
inline juce::DialogWindow* launchAsync (juce::DialogWindow::LaunchOptions& o)
{
    auto* d = new FittedDialog (o);
    d->enterModalState (true, nullptr, true);
    return d;
}

/** The WindowFit of a window, when it has one (the program's windows and dialogs, and fermata:Live's). */
inline WindowFit* fitterOf (juce::Component* w) { return WindowFit::of (w); }

//==============================================================================================================================
// audit: the small-screen check's eye
//==============================================================================================================================
struct Problem
{
    juce::String kind;          // "squashed", "text", "overlap", "off"
    juce::String what;          // the control (its kind and its text)
    juce::Rectangle<int> where; // in the window
};

inline juce::String describe (juce::Component& c)
{
    juce::String kind = "component", text;
    if (auto* l = dynamic_cast<juce::Label*> (&c)) { kind = "label"; text = l->getText(); }
    else if (auto* tb = dynamic_cast<juce::ToggleButton*> (&c)) { kind = "tick box"; text = tb->getButtonText(); }
    else if (auto* b = dynamic_cast<juce::Button*> (&c)) { kind = "button"; text = b->getButtonText(); }
    else if (auto* cb = dynamic_cast<juce::ComboBox*> (&c)) { kind = "list box"; text = cb->getText(); }
    else if (auto* t = dynamic_cast<juce::TextEditor*> (&c)) { kind = "text box"; text = t->getText().upToFirstOccurrenceOf ("\n", false, false); }
    else if (dynamic_cast<juce::Slider*> (&c) != nullptr) kind = "slider";
    else if (dynamic_cast<juce::Viewport*> (&c) != nullptr || dynamic_cast<juce::ListBox*> (&c) != nullptr) kind = "scrolling list";
    if (text.isEmpty()) text = c.getName().isNotEmpty() ? c.getName() : c.getComponentID();
    if (text.isEmpty()) text = typeid (c).name();
    return kind + " \"" + text.substring (0, 60) + "\"";
}

/** What does not fit in a window as it is now: squashed controls (from its WindowFit), text that does not fit its control, controls that overlap,
    and controls that are off the window where it cannot scroll to them. */
inline std::vector<Problem> audit (juce::Component& window)
{
    std::vector<Problem> out;
    if (dynamic_cast<juce::AlertWindow*> (&window) != nullptr) return out;                 // (JUCE lays its message boxes out itself)
    auto inWin = [&] (juce::Component& c) { return window.getLocalArea (&c, c.getLocalBounds()); };
    if (auto* f = fitterOf (&window))
        for (auto& p : f->stillSquashed()) if (auto* c = p.getComponent()) out.push_back ({ "squashed", describe (*c), inWin (*c) });
    std::vector<juce::Component*> pieces;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (! ch->isVisible() || ch->getWidth() <= 0 || ch->getHeight() <= 0) continue;
            if (dynamic_cast<juce::ScrollBar*> (ch) != nullptr) continue;
            if (isPiece (*ch)) pieces.push_back (ch); else walk (*ch);
        }
    };
    if (auto* rw = dynamic_cast<juce::ResizableWindow*> (&window); rw != nullptr && rw->getContentComponent() != nullptr) walk (*rw->getContentComponent());
    else walk (window);
    const auto* fit = fitterOf (&window);
    const auto visibleArea = [&]
    {
        if (auto* rw = dynamic_cast<juce::ResizableWindow*> (&window)) return rw->getContentComponentBorder().subtractedFrom (window.getLocalBounds());
        return window.getLocalBounds();
    }();
    for (auto* c : pieces)
    {
        const auto r = inWin (*c);
        // text that does not fit
        juce::String text; float textW = 0.0f, room = 0.0f; int lines = 1;
        if (auto* l = dynamic_cast<juce::Label*> (c))
        {
            text = l->getText();
            const auto inner = l->getBorderSize().subtractedFrom (l->getLocalBounds());
            const auto font = l->getFont();
            lines = juce::jmax (1, (int) ((float) inner.getHeight() / juce::jmax (1.0f, font.getHeight())));
            textW = (float) juce::GlyphArrangement::getStringWidthInt (font, text);
            room = (float) inner.getWidth() * (float) lines * (lines > 1 ? 1.0f : 1.0f / juce::jmax (0.5f, l->getMinimumHorizontalScale()));
        }
        else if (auto* tb = dynamic_cast<juce::ToggleButton*> (c))
        {
            text = tb->getButtonText();
            textW = (float) juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) c->getHeight() * 0.75f))), text);
            room = (float) c->getWidth() - juce::jmin ((float) c->getHeight(), 24.0f) - 4.0f;
        }
        else if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && ! (bool) b->getProperties().getWithDefault (kNoText, false))
        {
            text = b->getButtonText();
            if (auto* lf = dynamic_cast<juce::TextButton::LookAndFeelMethods*> (&b->getLookAndFeel()))
                textW = (float) juce::GlyphArrangement::getStringWidthInt (lf->getTextButtonFont (*b, b->getHeight()), text);
            room = (float) c->getWidth() - 6.0f;
        }
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
        {
            text = cb->getText();
            if (auto* lf = dynamic_cast<juce::ComboBox::LookAndFeelMethods*> (&cb->getLookAndFeel()))
                textW = (float) juce::GlyphArrangement::getStringWidthInt (lf->getComboBoxFont (*cb), text);
            room = (float) c->getWidth() - (float) juce::jmin (c->getHeight(), 24) - 8.0f;
        }
        if (text.trim().isNotEmpty() && textW > room + 1.0f && ! (bool) c->getProperties().getWithDefault (kMayShrink, false))
            out.push_back ({ "text", describe (*c) + juce::String::formatted (" needs %d px, has %d", (int) textW, (int) room), r });
        // off the window, where it cannot be scrolled to
        if (fit == nullptr || ! fit->scrolling())
            if (r.getBottom() > visibleArea.getBottom() + 2 || r.getRight() > visibleArea.getRight() + 2 || r.getY() < visibleArea.getY() - 2 || r.getX() < visibleArea.getX() - 2)
                out.push_back ({ "off", describe (*c), r });
    }
    // controls over one another (only controls: labels with text, buttons, list boxes, text boxes, sliders)
    auto isControl = [] (juce::Component* c)
    {
        if (auto* l = dynamic_cast<juce::Label*> (c)) return l->getText().trim().isNotEmpty();
        return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr || dynamic_cast<juce::TextEditor*> (c) != nullptr
            || dynamic_cast<juce::Slider*> (c) != nullptr;
    };
    // (a label counts where its text is, not its whole box: a label across a row with a button at its far end is fine)
    auto inkOf = [&] (juce::Component* c)
    {
        auto r = inWin (*c);
        if (auto* l = dynamic_cast<juce::Label*> (c))
        {
            const auto inner = l->getBorderSize().subtractedFrom (r);
            const int lines = juce::jmax (1, (int) ((float) inner.getHeight() / juce::jmax (1.0f, l->getFont().getHeight())));
            if (lines > 1) return inner;
            const int w = juce::jmin (inner.getWidth(), juce::GlyphArrangement::getStringWidthInt (l->getFont(), l->getText()));
            const auto j = l->getJustificationType();
            const int x = j.testFlags (juce::Justification::right) ? inner.getRight() - w : j.testFlags (juce::Justification::horizontallyCentred) ? inner.getCentreX() - w / 2 : inner.getX();
            return juce::Rectangle<int> (x, inner.getY(), w, inner.getHeight());
        }
        return r;
    };
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        if (! isControl (pieces[i])) continue;
        const auto a = inkOf (pieces[i]);
        for (size_t j = i + 1; j < pieces.size(); ++j)
        {
            if (! isControl (pieces[j])) continue;
            const auto b = inkOf (pieces[j]);
            const auto x = a.getIntersection (b);
            if (x.getWidth() > 2 && x.getHeight() > 2)
                out.push_back ({ "overlap", describe (*pieces[i]) + " / " + describe (*pieces[j]), x });
        }
    }
    return out;
}
} // namespace td::fit
