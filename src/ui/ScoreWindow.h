#pragma once
#include "AppContext.h"
#include "Uikit.h"
#include "PdfPages.h"
#include "../core/ScoreBars.h"
#include <map>
#include <set>

namespace td
{
/** 3.51.0 (asked for on 10 November 2026: "a PDF reader. I want to be able to attach scores to each Edit window. It just needs to be one, that I
    can just tick open and closed as I need"): the score of an edit, in a window of its own.

    The Edit window's PDF key opens and closes it. The first time, the key asks for the file and COPIES it into the project's "PDFs" folder
    (Project::addScore), so the score is moved, copied and archived with the project; the edit keeps the file's name (EditDef::score) and where
    this window was looking (scoreTop, scoreZoom). The window is one of the program's own (it can be pinned, it remembers its place in the
    project, and Space still plays and stops the edit while it is in front).

    The pages come from pdfpages (PdfPages.h): one worker thread opens the file and draws each page that is in view, at the size it is shown
    at, as a picture. Only the pages near the ones in view are kept (a 300-page score would otherwise be gigabytes); the rest are drawn again
    when they come back into view. While a page is being drawn at a new size the old picture is shown stretched.

    3.54.0 (asked for on 11 November 2026, with a photograph of the user's own pen marks): THE TAKES MARKED ON THE SCORE. The same worker finds
    the bars of every page once (scorebars::findSystems, on a picture of the page drawn for it; kept in "<the PDF's name>.bars.json" beside the
    PDF, so the next opening has them at once), the bars are counted through the score, and for every take of the edit's take windows that has
    its bars a bracket is drawn above the system at the barline where it started and one where it stopped - ONE start bracket and ONE end
    bracket at a barline, with the numbers of all the takes that began or stopped there, in order (scorebars::brackets). A take with only one
    of its bars typed, or none, gets the missing one from its sound when take matching is on for its window (scorebars::guessBars: laid over
    a take whose bars were typed); those numbers carry a "?". The Takes key shows and hides the brackets; the Bars key shows the bars that
    were found, numbered, for checking - a right-click on a bar puts its number right, takes a barline off or adds one. A click on a take's
    number plays that take (from its start at a start bracket, its last seconds at an end bracket); a right-click sets its bars. */
namespace score
{
inline juce::String windowKey (const juce::Uuid& editId) { return "score:" + editId.toString(); }

/** Asks for a PDF, copies it into the project's PDFs folder and makes it the edit's score; 'done' is called when the edit has it. */
inline void choosePdf (AppContext& app, const juce::Uuid& editId, juce::Component* over, std::function<void()> done)
{
    auto* st = app.props.getUserSettings();
    juce::File start (st != nullptr ? st->getValue ("scoreFolder") : juce::String());
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    auto chooser = std::make_shared<juce::FileChooser> ("Choose the score for this edit (a PDF). A copy of it is kept in the project's PDFs folder.", start, "*.pdf");
    auto* a = &app;
    juce::Component::SafePointer<juce::Component> safeOver (over);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [chooser, a, editId, safeOver, done] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (auto* st2 = a->props.getUserSettings()) { st2->setValue ("scoreFolder", f.getParentDirectory().getFullPathName()); a->props.saveIfNeeded(); }
        auto* e = a->project.findEdit (editId);
        if (e == nullptr) return;
        const auto err = a->project.addScore (*e, f);
        if (err.isNotEmpty())
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Score").withMessage (err).withButton ("OK").withAssociatedComponent (safeOver.getComponent()), nullptr);
            return;
        }
        if (done) done();
    });
}
} // namespace score

/** A key of the score window that also has a menu on a right-click. */
struct ScoreMenuKey : public juce::TextButton
{
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { menuPress = true; if (onMenu) onMenu(); return; } menuPress = false; juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (menuPress) { menuPress = false; return; } juce::TextButton::mouseUp (e); }
    bool menuPress = false;
};

class ScoreComponent : public juce::Component, private juce::Thread, private juce::Timer
{
public:
    ScoreComponent (AppContext& a, const juce::Uuid& eid) : juce::Thread ("Score pages"), app (a), editId (eid), pages (*this)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        for (auto* b : { &prevButton, &nextButton, &zoomOutButton, &zoomInButton, &fitButton, &barsButton, static_cast<juce::TextButton*> (&takesButton), &fileButton }) { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
        barsButton.setButtonText ("Bars"); takesButton.setButtonText ("Takes");
        barsButton.setTooltip ("Shows the bars fermata found on the pages, with the numbers it counted - to check them. Right-click a bar to put its number right (the bars after it count on from there), to take a barline off or to add one.");
        takesButton.setTooltip ("Shows where every take started and stopped: a bracket above the music at the barline, with the takes' numbers. They come from the bars typed for each take (and, with take matching on, from the sound when a bar was not typed: those have a ?). Click a number to hear that take; right-click it to set its bars. Right-click this key: how many takes are placed, listening to takes that have no bars, and writing the bars found from the sound onto the takes.");
        takesButton.onMenu = [this] { takesMenu(); };
        barsButton.setClickingTogglesState (true); takesButton.setClickingTogglesState (true);
        if (auto* st = app.props.getUserSettings()) { barsButton.setToggleState (st->getBoolValue ("scoreShowBars", false), juce::dontSendNotification); takesButton.setToggleState (st->getBoolValue ("scoreShowTakes", true), juce::dontSendNotification); }
        else takesButton.setToggleState (true, juce::dontSendNotification);
        barsButton.onClick = [this] { rememberKeys(); pages.repaint(); };
        takesButton.onClick = [this] { rememberKeys(); marksKey = "?"; refreshMarks(); pages.repaint(); };
        prevButton.setButtonText (juce::String::fromUTF8 ("\xE2\x97\x80")); nextButton.setButtonText (juce::String::fromUTF8 ("\xE2\x96\xB6"));
        prevButton.setTooltip ("The page before (Page Up)."); nextButton.setTooltip ("The next page (Page Down).");
        zoomOutButton.setButtonText ("-"); zoomInButton.setButtonText ("+");
        zoomOutButton.setTooltip ("Smaller (the - key, or Ctrl and the mouse wheel)."); zoomInButton.setTooltip ("Bigger (the + key, or Ctrl and the mouse wheel).");
        fitButton.setButtonText ("Fit width"); fitButton.setTooltip ("The page as wide as the window (the 0 key). It stays that way when the window is resized.");
        fileButton.setButtonText ("PDF..."); fileButton.setTooltip ("Choose another PDF for this edit, open this one in the computer's own PDF program, or take the score off the edit.");
        prevButton.onClick = [this] { goToPage (topPage() - (atPageTop() ? 1 : 0)); };
        nextButton.onClick = [this] { goToPage (topPage() + 1); };
        zoomOutButton.onClick = [this] { zoomBy (1.0 / 1.2); };
        zoomInButton.onClick = [this] { zoomBy (1.2); };
        fitButton.onClick = [this] { setZoom (0.0); };
        fileButton.onClick = [this] { fileMenu(); };
        addAndMakeVisible (pageLabel);
        pageLabel.setJustificationType (juce::Justification::centredLeft); pageLabel.setColour (juce::Label::textColourId, theme::text);
        pageLabel.setFont (theme::figureFont (14.0f, juce::Font::plain));
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&pages, false);
        viewport.setScrollBarsShown (true, true);
        viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);              // the page can be dragged about with the mouse
        viewport.setWantsKeyboardFocus (false); pages.setWantsKeyboardFocus (false);
        addChildComponent (message);
        message.setJustificationType (juce::Justification::centred); message.setColour (juce::Label::textColourId, theme::text);
        addChildComponent (chooseButton); addChildComponent (outsideButton);
        chooseButton.setButtonText ("Choose the PDF..."); outsideButton.setButtonText ("Open it in the computer's PDF program");
        chooseButton.onClick = [this] { chooseAnother(); };
        outsideButton.onClick = [this] { openOutside(); };
        setSize (760, 900);
        startThread();
        startTimerHz (10);
        loadFromEdit();
    }
    ~ScoreComponent() override
    {
        stopTimer();
        rememberView();
        if (barsDirty) saveBars();                                                        // (what was found so far: the rest is found the next time)
        signalThreadShouldExit(); notify();
        stopThread (8000);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        g.setColour (theme::panel); g.fillRect (0, 0, getWidth(), kBarH);
        g.setColour (theme::border); g.fillRect (0, kBarH - 1, getWidth(), 1);
    }
    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (kBarH).reduced (6, 5);
        bar.removeFromRight (grid::cornerReserve);                                          // (the pin and the sun / moon sit there)
        auto put = [&bar] (juce::Component& c, int w, int gapAfter) { c.setBounds (bar.removeFromLeft (w)); bar.removeFromLeft (gapAfter); };
        put (prevButton, 30, 2); put (nextButton, 30, 8);
        put (pageLabel, 104, 8);
        put (zoomOutButton, 28, 2); put (zoomInButton, 28, 6); put (fitButton, 74, 10);
        put (barsButton, 84, 2); put (takesButton, 56, 10);
        fileButton.setBounds (bar.removeFromRight (juce::jmin (64, juce::jmax (0, bar.getWidth()))));
        viewport.setBounds (getLocalBounds().withTrimmedTop (kBarH));
        auto mid = getLocalBounds().withTrimmedTop (kBarH).reduced (30);
        message.setBounds (mid.withHeight (juce::jmin (mid.getHeight(), 120)).withCentre ({ mid.getCentreX(), mid.getCentreY() - 50 }));
        chooseButton.setBounds (mid.getCentreX() - 150, message.getBottom() + 8, 300, 26);
        outsideButton.setBounds (mid.getCentreX() - 150, message.getBottom() + 40, 300, 26);
        const double keep = topNow();
        layoutPages();
        if (fitWidth()) setTop (keep);
    }
    /** Page Up / Page Down, Home / End, + and -, 0 (fit the width). Every other key is the edit's (Space plays and stops it: the window passes them on). */
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
        const int c = k.getKeyCode();
        if (c == juce::KeyPress::pageDownKey) { goToPage (topPage() + 1); return true; }
        if (c == juce::KeyPress::pageUpKey)   { goToPage (topPage() - (atPageTop() ? 1 : 0)); return true; }
        if (c == juce::KeyPress::homeKey)     { goToPage (0); return true; }
        if (c == juce::KeyPress::endKey)      { goToPage ((int) sizes.size() - 1); return true; }
        if (c == juce::KeyPress::downKey)     { viewport.setViewPosition (viewport.getViewPositionX(), viewport.getViewPositionY() + 60); return true; }
        if (c == juce::KeyPress::upKey)       { viewport.setViewPosition (viewport.getViewPositionX(), juce::jmax (0, viewport.getViewPositionY() - 60)); return true; }
        const auto ch = k.getTextCharacter();
        if (ch == '+' || ch == '=') { zoomBy (1.2); return true; }
        if (ch == '-')              { zoomBy (1.0 / 1.2); return true; }
        if (ch == '0')              { setZoom (0.0); return true; }
        return false;
    }

private:
    static constexpr int kBarH = 34, kGap = 10, kEdge = 10;
    struct Hit { juce::Rectangle<int> r; int span = 0; bool start = true; };              // a take's number as drawn on a page: which take, and at which kind of bracket

    // ------------------------------------------------------------------------------------------------ the pages (inside the viewport)
    struct Pages : public juce::Component, public juce::TooltipClient
    {
        explicit Pages (ScoreComponent& o) : owner (o) { setOpaque (true); }
        juce::String getTooltip() override { return owner.tipAt (getMouseXYRelative()); }
        void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (owner.hitAt (e.getPosition()) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown()) owner.pageClicked (e); }      // (a drag moves the page about)
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::manuscript() ? juce::Colour (0xff0b1220) : juce::Colour (0xff2b333d));
            const auto clip = g.getClipBounds();
            for (int i = 0; i < (int) owner.rects.size(); ++i)
            {
                const auto r = owner.rects[(size_t) i];
                if (r.getBottom() < clip.getY() || r.getY() > clip.getBottom()) continue;
                g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (r.translated (2, 3));
                g.setColour (juce::Colours::white); g.fillRect (r);
                const auto it = owner.pictures.find (i);
                if (it != owner.pictures.end() && it->second.isValid())
                {
                    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
                    g.drawImage (it->second, r.toFloat());
                }
                else
                {
                    g.setColour (juce::Colour (0xff8a8f98)); g.setFont (juce::FontOptions (15.0f));
                    g.drawText ("Page " + juce::String (i + 1), r, juce::Justification::centred, false);
                }
                if (owner.barsButton.getToggleState()) owner.drawBars (g, i, r);
                if (owner.takesButton.getToggleState()) owner.drawTakes (g, i, r); else owner.hits.erase (i);
            }
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { owner.zoomBy (w.deltaY > 0 ? 1.1 : 1.0 / 1.1); return; }
            juce::Component::mouseWheelMove (e, w);                                         // (the viewport scrolls)
        }
        void mouseDown (const juce::MouseEvent&) override { owner.grabKeyboardFocus(); }
        ScoreComponent& owner;
    };

    // ------------------------------------------------------------------------------------------------ the layout
    EditDef* edit() const { return app.project.findEdit (editId); }
    bool fitWidth() const { return zoom <= 0.0; }
    /** Pixels for each of the page's own units (PdfPages.h: 1/96 inch). Fit width: the widest page fills the window. */
    double scaleNow() const
    {
        if (! fitWidth()) return zoom;
        double widest = 1.0; for (auto& s : sizes) widest = juce::jmax (widest, s.x);
        const int room = viewport.getWidth() - viewport.getScrollBarThickness() - 2 * kEdge;
        return juce::jlimit (0.05, 40.0, (double) juce::jmax (80, room) / widest);
    }
    void layoutPages()
    {
        rects.clear();
        const double sc = scaleNow();
        int widest = 0; for (auto& s : sizes) widest = juce::jmax (widest, juce::roundToInt (s.x * sc));
        const int fullW = juce::jmax (viewport.getWidth() - viewport.getScrollBarThickness(), widest + 2 * kEdge);
        int y = kEdge;
        for (auto& s : sizes)
        {
            const int w = juce::jmax (1, juce::roundToInt (s.x * sc)), h = juce::jmax (1, juce::roundToInt (s.y * sc));
            rects.push_back ({ (fullW - w) / 2, y, w, h });
            y += h + kGap;
        }
        pages.setSize (fullW, juce::jmax (viewport.getHeight(), y - kGap + kEdge));
        pages.repaint();
    }
    /** The page at the top of the window and how far down it: 2.25 = a quarter of the way down the third page. */
    double topNow() const
    {
        if (rects.empty()) return top;
        const int y = viewport.getViewPositionY();
        for (int i = 0; i < (int) rects.size(); ++i)
            if (y < rects[(size_t) i].getBottom() + kGap / 2) return (double) i + juce::jlimit (0.0, 0.999, (double) (y - rects[(size_t) i].getY()) / (double) juce::jmax (1, rects[(size_t) i].getHeight()));
        return (double) rects.size() - 0.001;
    }
    int topPage() const { return (int) topNow(); }
    bool atPageTop() const { const double t = topNow(); return t - std::floor (t) < 0.02; }
    void setTop (double t)
    {
        top = juce::jmax (0.0, t);
        if (rects.empty()) return;
        const int i = juce::jlimit (0, (int) rects.size() - 1, (int) top);
        const auto r = rects[(size_t) i];
        viewport.setViewPosition (viewport.getViewPositionX(), juce::jmax (0, r.getY() + juce::roundToInt ((top - (double) i) * r.getHeight()) - (top - (double) i < 0.001 ? kGap / 2 : 0)));       // (a page's top: with a little of the gap above it)
    }
    void goToPage (int i) { if (! rects.empty()) { setTop ((double) juce::jlimit (0, (int) rects.size() - 1, i)); wantPictures(); } }
    void setZoom (double z)
    {
        const double keep = topNow();
        zoom = z <= 0.0 ? 0.0 : juce::jlimit (0.1, 12.0, z);
        layoutPages(); setTop (keep); wantPictures();
    }
    void zoomBy (double f) { if (! sizes.empty()) setZoom (scaleNow() * f); }

    // ------------------------------------------------------------------------------------------------ the edit's score
    void loadFromEdit()
    {
        auto* e = edit();
        shown = e != nullptr ? e->score : juce::String();
        if (barsDirty) saveBars();
        sizes.clear(); rects.clear(); pictures.clear(); pictureWidth.clear(); failed.clear();
        zoom = e != nullptr ? e->scoreZoom : 0.0; top = e != nullptr ? e->scoreTop : 0.0; restoreTop = true;
        layoutPages();
        const auto file = e != nullptr ? app.project.scoreFileFor (*e) : juce::File();
        bars = {}; barList.clear(); spans.clear(); marks.clear(); hits.clear(); heard.clear(); marksKey.clear(); barsDirty = false;
        barsFile = file != juce::File() ? file.getSiblingFile (file.getFileName() + ".bars.json") : juce::File();
        pdfBytes = file.existsAsFile() ? file.getSize() : 0;
        { const juce::ScopedLock sl (lock); barPages.clear(); }
        if (shown.isEmpty()) { say ("This edit has no score yet.", true, false); return; }
        if (! file.existsAsFile()) { say ("The score \"" + shown + "\" is not in the project's PDFs folder any more.\n(" + app.project.pdfsFolder().getFullPathName() + ")", true, false); return; }
        if (! pdfpages::available())
        {
            say ("This build of fermata has no PDF reader in it, so the pages cannot be shown here.\n(The reader is part of Windows; it did not build when this copy of fermata was made - the build's log says why, under \"PDF pages\".)", false, true);
            return;
        }
        say ("Opening " + shown + "...", false, false);
        const juce::ScopedLock sl (lock);
        openFile = file; openWanted = true; ++generation; wanted.clear();
        notify();
    }
    void say (const juce::String& text, bool offerChoose, bool offerOutside)
    {
        message.setText (text, juce::dontSendNotification);
        message.setVisible (text.isNotEmpty()); chooseButton.setVisible (offerChoose); outsideButton.setVisible (offerOutside);
        viewport.setVisible (text.isEmpty());
        updateLabel();
    }
    void updateLabel()
    {
        const bool any = ! sizes.empty();
        // the page that fills most of the window's top half is "the page" (a page whose last lines are still at the top is not)
        int page = topPage();
        if (any && page + 1 < (int) rects.size() && rects[(size_t) page].getBottom() < viewport.getViewPositionY() + viewport.getHeight() / 3) ++page;
        const auto text = any ? "Page " + juce::String (page + 1) + " of " + juce::String ((int) sizes.size()) : juce::String();
        if (pageLabel.getText() != text) pageLabel.setText (text, juce::dontSendNotification);
        for (auto* b : { &prevButton, &nextButton, &zoomOutButton, &zoomInButton, &fitButton }) if (b->isEnabled() != any) b->setEnabled (any);
        if (fitButton.getToggleState() != (any && fitWidth())) fitButton.setToggleState (any && fitWidth(), juce::dontSendNotification);
    }
    void rememberView()
    {
        auto* e = edit();
        if (e == nullptr || sizes.empty() || e->score != shown) return;
        e->scoreTop = topNow(); e->scoreZoom = zoom;                                       // (a view: saved with the project the next time it is saved, never a change of its own)
    }
    void chooseAnother()
    {
        juce::Component::SafePointer<ScoreComponent> safe (this);
        score::choosePdf (app, editId, getTopLevelComponent(), [safe] { if (safe != nullptr) safe->loadFromEdit(); });
    }
    void openOutside()
    {
        if (auto* e = edit()) { const auto f = app.project.scoreFileFor (*e); if (f.existsAsFile()) f.startAsProcess(); }
    }
    void fileMenu()
    {
        auto* e = edit();
        const bool has = e != nullptr && e->score.isNotEmpty();
        juce::PopupMenu m;
        if (has) m.addSectionHeader (e->score);
        m.addItem (1, has ? "Choose another PDF for this edit..." : "Choose the PDF for this edit...");
        m.addItem (2, "Open it in the computer's own PDF program", has);
        m.addItem (3, "Show the project's PDFs folder");
        m.addSeparator();
        m.addItem (4, "Take the score off this edit  (the file stays in the PDFs folder)", has);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->chooseAnother();
            else if (r == 2) safe->openOutside();
            else if (r == 3) { auto dir = safe->app.project.pdfsFolder(); dir.createDirectory(); dir.revealToUser(); }
            else if (r == 4) { if (auto* ed = safe->edit()) { ed->score.clear(); ed->scoreTop = 0.0; ed->scoreZoom = 0.0; safe->app.project.markDirty(); safe->loadFromEdit(); } }
        });
    }

    // ------------------------------------------------------------------------------------------------ which pictures are wanted
    struct Want { int page = 0, w = 0, h = 0; };
    void wantPictures()
    {
        if (rects.empty()) return;
        const int y0 = viewport.getViewPositionY(), y1 = y0 + viewport.getHeight();
        int first = -1, last = -1;
        for (int i = 0; i < (int) rects.size(); ++i)
            if (rects[(size_t) i].getBottom() >= y0 && rects[(size_t) i].getY() <= y1) { if (first < 0) first = i; last = i; }
        if (first < 0) return;
        // the pictures far from the pages in view are let go (they are big); the page before and the page after are drawn ahead
        for (auto it = pictures.begin(); it != pictures.end();)
            if (it->first < first - 3 || it->first > last + 3) { pictureWidth.erase (it->first); it = pictures.erase (it); } else ++it;
        double px = 1.0;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) px = d->scale;
        std::vector<Want> list;
        auto add = [&] (int i)
        {
            if (i < 0 || i >= (int) rects.size() || failed.count (i) != 0) return;
            const auto r = rects[(size_t) i];
            double w = r.getWidth() * px, h = r.getHeight() * px;
            const double most = 3600.0; if (w > most || h > most) { const double f = most / juce::jmax (w, h); w *= f; h *= f; }      // (never a picture bigger than this: zoomed right in, it is stretched)
            const int wi = juce::jmax (1, juce::roundToInt (w)), hi = juce::jmax (1, juce::roundToInt (h));
            const auto have = pictureWidth.find (i);
            if (have != pictureWidth.end() && std::abs (have->second - wi) <= 2) return;
            list.push_back ({ i, wi, hi });
        };
        for (int i = first; i <= last; ++i) add (i);
        add (last + 1); add (first - 1);
        const juce::ScopedLock sl (lock);
        list.erase (std::remove_if (list.begin(), list.end(), [this] (const Want& w) { return w.page == busyPage && std::abs (w.w - busyW) <= 2; }), list.end());     // (the one being drawn now)
        wanted = std::move (list);
        if (! wanted.empty()) notify();
    }
    void timerCallback() override
    {
        if (auto* e = edit(); e != nullptr && e->score != shown) { loadFromEdit(); return; }      // (another PDF was chosen from the Edit window's key)
        if (sizes.empty()) return;
        const double t = topNow();
        if (! restoreTop) top = t;
        updateLabel();
        if (++ticks % 2 == 0) wantPictures();
        if (ticks % 20 == 0) rememberView();
        if (ticks % 5 == 0)
        {
            const auto label = bars.pages.empty() || bars.complete() ? juce::String ("Bars") : "Bars " + juce::String (bars.pagesDone()) + "/" + juce::String ((int) bars.pages.size());
            if (barsButton.getButtonText() != label) barsButton.setButtonText (label);
            hearBars();
            refreshMarks();
        }
    }

    // ------------------------------------------------------------------------------------------------ the worker
    void run() override
    {
        pdfpages::Doc* doc = nullptr;
        int docGeneration = -1;
        while (! threadShouldExit())
        {
            juce::File toOpen; bool doOpen = false; Want job; bool haveJob = false; int gen = 0, barPage = -1;
            {
                const juce::ScopedLock sl (lock);
                gen = generation;
                if (openWanted) { openWanted = false; doOpen = true; toOpen = openFile; }
                else if (! wanted.empty() && docGeneration == generation && busyPage < 0) { job = wanted.front(); wanted.erase (wanted.begin()); haveJob = true; busyPage = job.page; busyW = job.w; }
                else if (wanted.empty() && ! barPages.empty() && docGeneration == generation && ! app.engine.isRecording()) { barPage = barPages.front(); barPages.erase (barPages.begin()); }    // (the pages to look at come first; and never while a take records - it waits)
            }
            juce::Component::SafePointer<ScoreComponent> safe (this);
            if (doOpen)
            {
                if (doc != nullptr) { pdfpages::close (doc); doc = nullptr; }
                std::string err;
                doc = pdfpages::open (toOpen.getFullPathName().toWideCharPointer(), err);
                docGeneration = gen;
                std::vector<juce::Point<double>> sz;
                if (doc != nullptr)
                {
                    const int n = pdfpages::pageCount (doc);
                    double lw = 595.0 * 96.0 / 72.0, lh = 842.0 * 96.0 / 72.0;              // (a page whose size cannot be read is taken to be like the one before; the first, A4)
                    for (int i = 0; i < n && ! threadShouldExit(); ++i) { double w = 0, h = 0; if (pdfpages::pageSize (doc, i, w, h)) { lw = w; lh = h; } sz.push_back ({ lw, lh }); }
                    if (n <= 0) err = "The PDF has no pages.";
                }
                const auto text = juce::String::fromUTF8 (err.c_str());
                juce::MessageManager::callAsync ([safe, gen, sz, text] { if (safe != nullptr) safe->opened (gen, sz, text); });
                continue;
            }
            if (haveJob && doc == nullptr) { const juce::ScopedLock sl (lock); busyPage = -1; busyW = 0; }
            if (haveJob && doc != nullptr)
            {
                std::vector<unsigned char> png; std::string err;
                juce::Image img;
                if (pdfpages::render (doc, job.page, job.w, job.h, png, err) && ! png.empty()) img = juce::ImageFileFormat::loadFrom (png.data(), png.size());
                const int page = job.page, w = job.w;
                juce::MessageManager::callAsync ([safe, gen, page, w, img] { if (safe != nullptr) safe->drawn (gen, page, w, img); });
                continue;
            }
            if (barPage >= 0)
            {
                // 3.54.0: this page's bars, from a picture of it drawn for the purpose (always the same size, whatever the zoom)
                std::vector<scorebars::System> systems;
                double uw = 0.0, uh = 0.0;
                if (doc != nullptr && pdfpages::pageSize (doc, barPage, uw, uh))
                {
                    int w = 0, h = 0; scorebars::analysisSize (uw, uh, w, h);
                    std::vector<unsigned char> png, grey; std::string err; int gw = 0, gh = 0;
                    if (pdfpages::render (doc, barPage, w, h, png, err) && ! png.empty() && greyOf (juce::ImageFileFormat::loadFrom (png.data(), png.size()), grey, gw, gh))
                        systems = scorebars::findSystems (grey.data(), gw, gh, gw);
                }
                const int page = barPage;
                juce::MessageManager::callAsync ([safe, gen, page, systems] { if (safe != nullptr) safe->barsFound (gen, page, systems); });
                wait (20);                                                 // (a breath between pages: this is never urgent)
                continue;
            }
            wait (500);
        }
        if (doc != nullptr) pdfpages::close (doc);
    }
    void opened (int gen, const std::vector<juce::Point<double>>& sz, const juce::String& err)
    {
        if (gen != generation) return;
        if (sz.empty())
        {
            say ("The score \"" + shown + "\" could not be opened here" + (err.isNotEmpty() ? ":\n" + err : juce::String (".")), true, true);
            return;
        }
        sizes = sz;
        say ({}, false, false);
        layoutPages();
        setTop (top); restoreTop = false;
        updateLabel(); wantPictures();
        loadBars();
    }
    void drawn (int gen, int page, int w, const juce::Image& img)
    {
        { const juce::ScopedLock sl (lock); busyPage = -1; busyW = 0; }
        notify();                                                          // (the next page that is wanted)
        if (gen != generation || page < 0 || page >= (int) rects.size()) return;
        if (! img.isValid()) { failed.insert (page); return; }
        pictures[page] = img; pictureWidth[page] = w;
        pages.repaint (rects[(size_t) page]);
    }

    // ------------------------------------------------------------------------------------------------ 3.54.0: the bars
    /** A page's picture as plain greys (0 black ... 255 white), on white paper. (Worker thread.) */
    static bool greyOf (const juce::Image& img, std::vector<unsigned char>& out, int& w, int& h)
    {
        if (! img.isValid()) return false;
        w = img.getWidth(); h = img.getHeight();
        out.resize ((size_t) w * (size_t) h);
        const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
        {
            auto* o = out.data() + (size_t) y * (size_t) w;
            if (d.pixelFormat == juce::Image::RGB)
                for (int x = 0; x < w; ++x) { const auto c = ((const juce::PixelRGB*) d.getPixelPointer (x, y)); o[x] = (unsigned char) ((c->getRed() * 77 + c->getGreen() * 150 + c->getBlue() * 29) >> 8); }
            else if (d.pixelFormat == juce::Image::ARGB)
                for (int x = 0; x < w; ++x)
                {
                    const auto c = ((const juce::PixelARGB*) d.getPixelPointer (x, y));                // (its colours are already times its alpha: what is see-through is paper)
                    o[x] = (unsigned char) juce::jmin (255, ((c->getRed() * 77 + c->getGreen() * 150 + c->getBlue() * 29) >> 8) + (255 - c->getAlpha()));
                }
            else
                for (int x = 0; x < w; ++x) o[x] = (unsigned char) (255 - *d.getPixelPointer (x, y));
        }
        return true;
    }
    void rememberKeys()
    {
        if (auto* st = app.props.getUserSettings()) { st->setValue ("scoreShowBars", barsButton.getToggleState()); st->setValue ("scoreShowTakes", takesButton.getToggleState()); app.props.saveIfNeeded(); }
    }
    /** The bars kept beside the PDF (if they are this PDF's), else every page is to be looked at. */
    void loadBars()
    {
        const int n = (int) sizes.size();
        bars = {};
        if (! (barsFile.existsAsFile() && scorebars::fromVar (bars, juce::JSON::parse (barsFile), pdfBytes, n))) { bars = {}; bars.pages.resize ((size_t) n); }
        barsDirty = false;
        recount();
        queueBarPages();
        givePieceBars();
        if (barsDirty) saveBars();
    }
    void queueBarPages()
    {
        std::vector<int> todo;
        for (int i = 0; i < (int) bars.pages.size(); ++i) if (! bars.pages[(size_t) i].done) todo.push_back (i);
        const int at = topPage();
        std::stable_sort (todo.begin(), todo.end(), [at] (int a, int b) { return std::abs (a - at) < std::abs (b - at); });      // (the pages being looked at first)
        { const juce::ScopedLock sl (lock); barPages = todo; }
        if (! todo.empty()) notify();
    }
    void saveBars()
    {
        barsDirty = false;
        if (barsFile == juce::File() || bars.pages.empty() || ! barsFile.getParentDirectory().isDirectory()) return;
        if (! barsFile.replaceWithText (juce::JSON::toString (scorebars::toVar (bars, pdfBytes), true)))
            juce::Logger::writeToLog ("Score: the bars could not be kept in " + barsFile.getFullPathName() + " (they are found again the next time)");
    }
    void recount() { barList = bars.bars(); marksKey = "?"; hits.clear(); pages.repaint(); }
    void barsChanged() { barsDirty = true; recount(); givePieceBars(); saveBars(); refreshMarks(); }
    /** "Fermata could also find the number of bars in the PDF and automatically tell the take window how many bars there are in the piece
        (the user should be able to overwrite it)" (11 November 2026): the score's count goes to the take windows tied to this edit - to
        one that has no number yet, or still has the number fermata gave it before (scorebars::pieceBarsToGive). A number the user typed in
        the take window is never replaced. Done when the bars have just been found, when they are put right here, and when the score is
        opened - never on a timer (it is an undo step in the project, like typing the number). */
    void givePieceBars()
    {
        auto* e = edit();
        if (e == nullptr || ! bars.complete() || barList.empty()) return;
        const int count = scorebars::pieceLength (barList, scorebars::startOrdinal (bars, barList, editId.toString()));
        bool any = false;
        for (auto* w : tiedWindows (*e))
        {
            const auto id = w->id.toString();
            const auto it = bars.piecesGiven.find (id);
            const int given = it != bars.piecesGiven.end() ? it->second : 0;
            const int to = juce::jlimit (0, 99999, scorebars::pieceBarsToGive (count, w->pieceBars, given));
            if (to != w->pieceBars)
            {
                juce::Logger::writeToLog ("Score: \"" + shown + "\" has " + juce::String (count) + " bars - given to the take window \"" + w->name + "\" as the bars in the piece" + (w->pieceBars > 0 ? " (it had " + juce::String (w->pieceBars) + ", from this score)" : juce::String()));
                w->pieceBars = to; any = true;
            }
            if (to == count && given != count) { bars.piecesGiven[id] = count; barsDirty = true; }
        }
        if (any) app.project.changed();
        barsButton.setTooltip ("Shows the bars fermata found on the pages, with the numbers it counted - to check them. Right-click a bar to put its number right (the bars after it count on from there), to take a barline off or to add one. This score: "
                               + juce::String (count) + " bars, which a take window that had no number of its own is given as the bars in the piece (type another there if it is wrong: yours stays).");
    }
    void barsFound (int gen, int page, const std::vector<scorebars::System>& systems)
    {
        if (gen != generation || page < 0 || page >= (int) bars.pages.size()) return;
        bars.pages[(size_t) page].systems = systems; bars.pages[(size_t) page].done = true;
        barsDirty = true;
        recount();
        if (bars.complete())
        {
            givePieceBars();
            saveBars();
            int systemsN = 0; for (auto& p : bars.pages) systemsN += (int) p.systems.size();
            juce::Logger::writeToLog ("Score: the bars of \"" + shown + "\" found - " + juce::String ((int) bars.pages.size()) + " pages, " + juce::String (systemsN) + " systems, " + juce::String ((int) barList.size()) + " bars");
        }
    }
    /** The page under a point of 'pages' (-1: none), and where on it (0..1 across and down). */
    int pageAt (juce::Point<int> p, double& fx, double& fy) const
    {
        for (int i = 0; i < (int) rects.size(); ++i)
            if (rects[(size_t) i].contains (p))
            {
                fx = (double) (p.x - rects[(size_t) i].getX()) / (double) rects[(size_t) i].getWidth(); fy = (double) (p.y - rects[(size_t) i].getY()) / (double) rects[(size_t) i].getHeight();
                return i;
            }
        return -1;
    }
    const scorebars::Bar* barOf (int page, int system, int index) const
    {
        for (auto& b : barList) if (b.page == page && b.system == system && b.index == index) return &b;
        return nullptr;
    }
    /** The bars that were found, each in a box with its number (the Bars key). */
    void drawBars (juce::Graphics& g, int page, juce::Rectangle<int> r)
    {
        if (page >= (int) bars.pages.size()) return;
        const auto ink = juce::Colour (0xff1c6fd6);
        if (! bars.pages[(size_t) page].done)
        {
            g.setColour (ink); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("Finding this page's bars...", r.reduced (8).removeFromTop (20), juce::Justification::centredLeft, false);
            return;
        }
        const float fh = juce::jlimit (9.0f, 16.0f, (float) r.getHeight() * 0.0115f);
        const juce::Font font (juce::FontOptions (fh, juce::Font::bold));
        g.setFont (font);
        for (auto& b : barList)
        {
            if (b.page != page) continue;
            const auto* s = bars.systemAt (page, b.system); if (s == nullptr) continue;
            const float sp = (float) (s->space * r.getHeight());
            const juce::Rectangle<float> box ((float) r.getX() + (float) (b.x0 * r.getWidth()), (float) r.getY() + (float) (s->top * r.getHeight()) - sp,
                                              (float) ((b.x1 - b.x0) * r.getWidth()), (float) ((s->bottom - s->top) * r.getHeight()) + 2.0f * sp);
            g.setColour (ink.withAlpha ((b.number & 1) != 0 ? 0.11f : 0.04f)); g.fillRect (box);       // (every other bar a shade darker, so that two are never taken for one)
            g.setColour (ink.withAlpha (0.75f)); g.drawRect (box, 1.0f);
            const auto text = juce::String (b.number);
            const juce::Rectangle<float> tab (box.getX(), box.getY() - fh - 2.0f, juce::GlyphArrangement::getStringWidth (font, text) + 8.0f, fh + 2.0f);
            g.setColour (ink); g.fillRect (tab);
            g.setColour (juce::Colours::white); g.drawText (text, tab, juce::Justification::centred, false);
        }
        if (bars.pages[(size_t) page].systems.empty())
        {
            g.setColour (ink); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText ("No bars were found on this page.", r.reduced (8).removeFromTop (20), juce::Justification::centredLeft, false);
        }
    }

    // ------------------------------------------------------------------------------------------------ 3.54.0: the takes' marks
    /** The take windows whose takes are this edit's: the ones that send to it, the one it was made from, the ones its pieces came from - and,
        when none is tied to it yet (a session's first takes), all of them. */
    std::vector<TakeWindowDef*> windowsOf (const EditDef& e) const
    {
        auto out = tiedWindows (e);
        if (out.empty()) for (auto& w : app.project.takeWindows) out.push_back (w.get());
        return out;
    }
    /** ... the ones really tied to it (and, in a project with one take window and one edit, that window): the piece's length is given to these only. */
    std::vector<TakeWindowDef*> tiedWindows (const EditDef& e) const
    {
        std::vector<TakeWindowDef*> out;
        auto add = [&] (TakeWindowDef* w) { if (w != nullptr && std::find (out.begin(), out.end(), w) == out.end()) out.push_back (w); };
        for (auto& w : app.project.takeWindows) if (w->targetEdit == e.id || w->id == e.windowId) add (w.get());
        for (auto& reg : e.regions) add (app.project.findTakeWindow (reg.windowId));
        if (out.empty() && app.project.takeWindows.size() == 1 && app.project.edits.size() == 1) add (app.project.takeWindows.front().get());
        return out;
    }
    /** The takes that have a bar (typed, or from the sound), and their brackets - made again only when something about them has changed. */
    void refreshMarks()
    {
        auto* e = edit();
        std::vector<scorebars::TakeSpan> list;
        juce::String key;
        if (e != nullptr && takesButton.getToggleState() && ! barList.empty())
        {
            for (auto* w : windowsOf (*e))
                for (auto& g : w->groups)
                {
                    if (g.lengthSamples <= 0 || g.roomTone) continue;                       // (still being recorded; the room on its own)
                    scorebars::TakeSpan s;
                    s.window = w->id; s.take = g.id; s.number = g.number; s.dud = g.dud;
                    s.barIn = juce::jmax (0, g.barIn); s.barOut = g.barOut > 0 ? juce::jmax (g.barOut, s.barIn) : 0;
                    if (s.barIn <= 0 || s.barOut <= 0)
                        if (const auto it = heard.find (g.id); it != heard.end())
                        {
                            const auto& b = it->second.best;
                            if (s.barIn <= 0 && b.barIn > 0 && (s.barOut <= 0 || b.barIn <= s.barOut)) { s.barIn = b.barIn; s.inGuessed = true; }
                            if (s.barOut <= 0 && b.barOut > 0 && b.barOut >= s.barIn) { s.barOut = b.barOut; s.outGuessed = true; }
                        }
                    if (s.barIn <= 0 && s.barOut <= 0) continue;
                    key << g.id.toString().substring (0, 10) << ":" << s.number << ":" << s.barIn << (s.inGuessed ? "?" : "") << ":" << s.barOut << (s.outGuessed ? "?" : "") << (s.dud ? "d" : "") << ";";
                    list.push_back (s);
                }
            key << "from" << scorebars::startOrdinal (bars, barList, editId.toString());
        }
        if (key == marksKey) return;
        marksKey = key; spans = std::move (list);
        marks = scorebars::brackets (barList, spans, scorebars::startOrdinal (bars, barList, editId.toString()));
        hits.clear(); pages.repaint();
    }
    /** A bar that was not typed, from the take's sound: a few layings-over of two takes each time this is called (each is a few hundredths
        of a second for long takes; no more than a fortieth of a second is spent at a time), kept. Only for take windows with take matching
        switched on - the others' takes have not been listened to. This is what places fifty takes brought in from another program once
        the bars of a few have been typed: every take is laid over the takes with typed bars, the ones with the most of the piece first
        (six of them; more, up to sixteen, for a take that those do not reach). */
    void hearBars()
    {
        auto* e = edit();
        if (e == nullptr || barList.empty() || ! takesButton.getToggleState() || app.takeMatch.isHeld() || app.engine.isRecording()) return;      // (not while a take records)
        using State = TakeMatchService::State;
        struct Ref { juce::Uuid id; int in = 0, out = 0; };
        std::vector<Ref> refs;
        const auto wins = windowsOf (*e);
        for (auto* w : wins)
            if (w->takeMatching)
                for (auto& g : w->groups)
                    if (g.barIn > 0 && g.barOut > g.barIn && g.lengthSamples > 0 && app.takeMatch.statusOf (g.id).state == State::Ready) refs.push_back ({ g.id, g.barIn, g.barOut });
        if (refs.empty()) return;
        std::stable_sort (refs.begin(), refs.end(), [] (const Ref& a, const Ref& b) { return a.out - a.in > b.out - b.in; });        // (the ones with the most of the piece in them)
        if (refs.size() > 16) refs.resize (16);
        const double began = juce::Time::getMillisecondCounterHiRes();
        juce::String refKey; for (auto& r : refs) refKey << r.id.toString().substring (0, 8) << r.in << "-" << r.out << ";";
        for (auto* w : wins)
        {
            if (! w->takeMatching) continue;
            for (auto& g : w->groups)
            {
                if (g.lengthSamples <= 0 || g.roomTone || (g.barIn > 0 && g.barOut > 0)) continue;
                if (app.takeMatch.statusOf (g.id).state != State::Ready) continue;
                auto& h = heard[g.id];
                if (h.key != refKey) { h = {}; h.key = refKey; }
                for (size_t ri = 0; ri < refs.size(); ++ri)
                {
                    const auto& r = refs[ri];
                    if (ri >= 6 && (g.barIn > 0 || h.best.barIn > 0) && (g.barOut > 0 || h.best.barOut > 0)) break;       // (placed at both ends by the first six: enough)
                    if (r.id == g.id || std::find (h.tried.begin(), h.tried.end(), r.id) != h.tried.end()) continue;
                    h.tried.push_back (r.id);
                    const auto own = app.takeMatch.fingerprintFor (g.id), other = app.takeMatch.fingerprintFor (r.id);
                    const auto al = app.takeMatch.alignmentFor (g.id, r.id);
                    if (own != nullptr && other != nullptr && al != nullptr)
                    {
                        const auto guess = scorebars::guessBars (*own, *other, r.in, r.out, *al);
                        auto worth = [] (const scorebars::Guess& q) { return (q.barIn > 0 ? 1.0 : 0.0) + (q.barOut > 0 ? 1.0 : 0.0) + 0.5 * q.confidence; };
                        if (worth (guess) > worth (h.best)) h.best = guess;
                    }
                    if (juce::Time::getMillisecondCounterHiRes() - began > 25.0) return;
                }
            }
        }
    }
    /** The brackets of one page, and where each take's number is (for the mouse). */
    void drawTakes (juce::Graphics& g, int page, juce::Rectangle<int> r)
    {
        auto& pageHits = hits[page];
        pageHits.clear();
        const auto pen = juce::Colour (0xffd3202f), dudInk = juce::Colour (0xff7f8894);       // a red pen on the print; a Dud take's number in grey
        std::vector<std::pair<int, juce::Rectangle<float>>> placed;                          // the numbers written so far on this page, by system: the next ones are kept clear of them
        for (auto& m : marks)
        {
            if (m.page != page) continue;
            const auto* s = bars.systemAt (page, m.system); if (s == nullptr) continue;
            // the marks are sized by the staff - but never smaller than they would be on a staff of the usual size (a full score's staves are small)
            const float pageH = (float) r.getHeight(), sp = juce::jmax (3.0f, (float) (s->space * pageH), 0.0062f * pageH);
            const float sysTop = (float) r.getY() + (float) (s->top * pageH);
            const float above = m.system > 0 ? (float) r.getY() + (float) (bars.pages[(size_t) page].systems[(size_t) m.system - 1].bottom * pageH) : (float) r.getY();
            const float gap = juce::jmax (4.0f * sp, sysTop - above);
            bool both = false;                                                             // an end and a start at the same barline stand side by side
            for (auto& o : marks) if (o.page == m.page && o.system == m.system && o.start != m.start && std::abs (o.x - m.x) * r.getWidth() < 1.5 * sp) both = true;
            const float x = (float) r.getX() + (float) (m.x * r.getWidth()) + (both ? (m.start ? 0.5f : -0.5f) * sp : 0.0f);
            const float fh = 3.0f * sp, rowH = 3.2f * sp, thick = juce::jmax (1.5f, 0.3f * sp);
            const juce::Font font (juce::FontOptions (fh, juce::Font::bold));

            // the numbers, in the takes' order, in as many rows as they need
            struct Item { juce::String text; float w = 0.0f; int span = 0; };
            std::vector<std::vector<Item>> rows (1);
            const float room = juce::jlimit (10.0f * sp, 42.0f * sp, (m.start ? (float) r.getRight() - x : x - (float) r.getX()) - 2.0f * sp);
            float used = 0.0f;
            for (size_t i = 0; i < m.takes.size(); ++i)
            {
                const auto& t = spans[(size_t) m.takes[i]];
                Item it; it.span = m.takes[i];
                it.text = juce::String (t.number) + ((m.start ? t.inGuessed : t.outGuessed) ? "?" : "") + (i + 1 < m.takes.size() ? "," : "");
                it.w = juce::GlyphArrangement::getStringWidth (font, it.text) + 0.5f * sp;
                if (used + it.w > room && ! rows.back().empty()) { rows.emplace_back(); used = 0.0f; }
                rows.back().push_back (it); used += it.w;
            }
            const float bottom = sysTop - juce::jmin (1.6f * sp, gap * 0.25f);                // just above the system it belongs to
            float widest = 0.0f;
            for (auto& row : rows) { float w = 0.0f; for (auto& it : row) w += it.w; widest = juce::jmax (widest, w); }
            // where another bracket's numbers are already (many short takes of the same few bars), this one's go above them, on a longer upright
            float top = bottom - ((float) rows.size() * rowH + 1.1f * sp);
            auto blockAt = [&] (float t) { return juce::Rectangle<float> (m.start ? x + 0.4f * sp : x - 0.8f * sp - widest, t, widest + 0.4f * sp, (float) rows.size() * rowH + 0.9f * sp); };
            for (int tries = 0; tries < 8; ++tries)
            {
                bool moved = false;
                for (auto& pl : placed)
                    if (pl.first == m.system && pl.second.expanded (0.3f * sp).intersects (blockAt (top))) { top = pl.second.getY() - 0.4f * sp - blockAt (top).getHeight(); moved = true; }
                if (! moved) break;
            }
            placed.push_back ({ m.system, blockAt (top) });
            g.setColour (pen);
            g.fillRect (juce::Rectangle<float> (x - thick * 0.5f, top, thick, bottom - top));
            const float arm = 3.8f * sp;
            g.fillRect (juce::Rectangle<float> (m.start ? x - thick * 0.5f : x + thick * 0.5f - arm, top, arm, thick));
            g.setFont (font);
            for (size_t k = 0; k < rows.size(); ++k)
            {
                float w = 0.0f; for (auto& it : rows[k]) w += it.w;
                float ix = m.start ? x + 0.8f * sp : x - 0.8f * sp - w;
                const float iy = top + 0.9f * sp + (float) k * rowH;
                g.setColour (juce::Colours::white.withAlpha (0.78f));                         // (whatever is printed there stays readable under the number, but faint)
                g.fillRoundedRectangle (juce::Rectangle<float> (ix - 0.2f * sp, iy, w + 0.2f * sp, rowH - 0.1f * sp), 0.4f * sp);
                for (auto& it : rows[k])
                {
                    const juce::Rectangle<float> box (ix, iy, it.w, rowH - 0.1f * sp);
                    g.setColour (spans[(size_t) it.span].dud ? dudInk : pen);
                    g.drawText (it.text, box, juce::Justification::centredLeft, false);
                    pageHits.push_back ({ box.getSmallestIntegerContainer(), it.span, m.start });
                    ix += it.w;
                }
            }
        }
    }
    const Hit* hitAt (juce::Point<int> p) const
    {
        for (auto& kv : hits) for (auto& h : kv.second) if (h.r.contains (p)) return &h;
        return nullptr;
    }
    TakeGroup* takeOf (const scorebars::TakeSpan& s) const { return app.project.findTake (s.window, s.take); }
    juce::String tipAt (juce::Point<int> p) const
    {
        if (const auto* h = hitAt (p); h != nullptr && h->span < (int) spans.size())
        {
            const auto& s = spans[(size_t) h->span];
            juce::String t = "Take " + juce::String (s.number) + (s.dud ? " (Dud)" : "");
            if (s.barIn > 0) t << ": bar " << s.barIn << (s.inGuessed ? " (from its sound)" : "");
            if (s.barOut > 0) t << (s.barIn > 0 ? " to bar " : ": to bar ") << s.barOut << (s.outGuessed ? " (from its sound)" : "");
            t << (h->start ? ". Click to hear it from its start" : ". Click to hear how it ended") << "; right-click to set its bars.";
            return t;
        }
        return {};
    }
    void pageClicked (const juce::MouseEvent& e)
    {
        if (const auto* h = hitAt (e.getPosition()))
        {
            const auto hit = *h;
            if (e.mods.isPopupMenu()) takeMenu (hit); else playTake (hit);
            return;
        }
        if (e.mods.isPopupMenu()) barMenu (e.getPosition());
    }
    void playTake (const Hit& h)
    {
        if (h.span >= (int) spans.size()) return;
        const auto s = spans[(size_t) h.span];
        auto* g = takeOf (s); if (g == nullptr) return;
        const double from = h.start ? 0.0 : juce::jmax (0.0, g->lengthSeconds() - 8.0);
        const auto err = app.playTake (s.window, s.take, from);
        if (err.isNotEmpty()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle ("Take " + juce::String (s.number)).withMessage (err).withButton ("OK").withAssociatedComponent (getTopLevelComponent()), nullptr);
    }
    /** A take's bars, set from the score (as the take window's own Bars box sets them: the edits' pieces from this take follow). */
    void setTakeBars (const juce::Uuid& windowId, const juce::Uuid& takeId, int in, int out)
    {
        if (putTakeBars (windowId, takeId, in, out)) { app.project.changed(); refreshMarks(); }
    }
    bool putTakeBars (const juce::Uuid& windowId, const juce::Uuid& takeId, int in, int out)
    {
        auto* g = app.project.findTake (windowId, takeId); if (g == nullptr) return false;
        in = juce::jmax (0, in); out = in > 0 && out > 0 ? juce::jmax (in, out) : 0;
        if (g->barIn == in && g->barOut == out) return false;
        g->barIn = in; g->barOut = out;
        for (auto& ed : app.project.edits)
        {
            for (auto& reg : ed->regions)  if (reg.takeId == takeId) { reg.barIn = in; reg.barOut = out; }
            for (auto& reg : ed->overdubs) if (reg.takeId == takeId) { reg.barIn = in; reg.barOut = out; }
        }
        return true;
    }
    /** The Takes key's right-click: how the edit's takes stand, listening to the ones with no bars, and making the sound's bars the takes' own. */
    void takesMenu()
    {
        auto* e = edit(); if (e == nullptr) return;
        int all = 0, typed = 0, bySound = 0, notPlaced = 0, offNoBars = 0;
        std::vector<TakeWindowDef*> off;
        for (auto* w : windowsOf (*e))
        {
            int missing = 0;
            for (auto& g : w->groups)
            {
                if (g.lengthSamples <= 0 || g.roomTone) continue;
                ++all;
                if (g.barIn > 0 && g.barOut > 0) { ++typed; continue; }
                ++missing;
                const auto it = heard.find (g.id);
                const bool placed = it != heard.end() && (g.barIn > 0 || it->second.best.barIn > 0) && (g.barOut > 0 || it->second.best.barOut > 0);
                if (placed) ++bySound; else ++notPlaced;
            }
            if (! w->takeMatching && missing > 0) { off.push_back (w); offNoBars += missing; }
        }
        int toWrite = 0; for (auto& s : spans) if (s.inGuessed || s.outGuessed) ++toWrite;
        juce::PopupMenu m;
        m.addSectionHeader (juce::String (all) + (all == 1 ? " take: " : " takes: ") + juce::String (typed) + " with both bars typed, " + juce::String (bySound) + " placed from their sound, " + juce::String (notPlaced) + " not placed");
        for (size_t i = 0; i < off.size() && i < 8; ++i)
            m.addItem (100 + (int) i, "Listen to the takes of \"" + off[i]->name + "\"   (switches take matching on for it: its takes with no bars typed are then placed from their sound)");
        m.addItem (1, "Write the bars found from the sound onto their takes   (" + juce::String (toWrite) + (toWrite == 1 ? " take" : " takes") + "; only bars that are empty are filled)", toWrite > 0);
        if (notPlaced > 0 && off.empty())
        {
            m.addSeparator();
            m.addItem (2, typed == 0 ? "No take has both its bars typed yet: type the bars of one that has the whole piece (or of a few that cover it) and the rest are placed from their sound"
                                     : "A take not placed is still being listened to, has music that no take with typed bars has, or is too short to place surely: type its bars", false);
        }
        std::vector<juce::Uuid> offIds; for (auto* w : off) offIds.push_back (w->id);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&takesButton), [safe, offIds] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r >= 100 && r - 100 < (int) offIds.size()) { safe->app.setTakeMatching (offIds[(size_t) (r - 100)], true); return; }
            if (r == 1)
            {
                bool any = false;
                const auto list = safe->spans;
                for (auto& s : list) if (s.inGuessed || s.outGuessed) any = safe->putTakeBars (s.window, s.take, s.barIn, s.barOut) || any;
                if (any) { safe->app.project.changed(); safe->refreshMarks(); }
            }
        });
    }
    void takeMenu (const Hit& h)
    {
        if (h.span >= (int) spans.size()) return;
        const auto s = spans[(size_t) h.span];
        const bool guessed = s.inGuessed || s.outGuessed;
        juce::PopupMenu m;
        m.addSectionHeader ("Take " + juce::String (s.number) + (s.barIn > 0 ? "  -  " + barsLabel (s.barIn, s.barOut) : juce::String()) + (guessed ? "  (? = from its sound)" : ""));
        m.addItem (1, h.start ? "Play it from its start" : "Play its last seconds");
        m.addItem (2, "Set its bars...");
        if (guessed) m.addItem (3, "Keep these bars for the take  (" + barsLabel (s.barIn, s.barOut) + ")", s.barIn > 0);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, h, s] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->playTake (h);
            else if (r == 3) safe->setTakeBars (s.window, s.take, s.barIn, s.barOut);
            else if (r == 2)
            {
                auto* aw = new juce::AlertWindow ("Take " + juce::String (s.number), "The bars this take starts and ends in. (An empty end: not known.)", juce::MessageBoxIconType::NoIcon, safe->getTopLevelComponent());
                aw->addTextEditor ("in", s.barIn > 0 ? juce::String (s.barIn) : juce::String(), "From bar");
                aw->addTextEditor ("out", s.barOut > 0 ? juce::String (s.barOut) : juce::String(), "To bar");
                aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, s] (int res)
                {
                    if (res == 1 && safe != nullptr) safe->setTakeBars (s.window, s.take, aw->getTextEditorContents ("in").getIntValue(), aw->getTextEditorContents ("out").getIntValue());
                    delete aw;
                }));
            }
        });
    }
    /** Right-click on the music: the bar there - its number, its barlines, where this edit's music begins. */
    void barMenu (juce::Point<int> p)
    {
        double fx = 0.0, fy = 0.0;
        const int page = pageAt (p, fx, fy);
        if (page < 0 || page >= (int) bars.pages.size() || ! bars.pages[(size_t) page].done) return;
        const int system = bars.systemNear (page, fy);
        const auto* sys = bars.systemAt (page, system);
        const int index = sys != nullptr && fx >= sys->x0 - 0.01 && fx <= sys->x1 + 0.01 ? scorebars::Map::indexAt (*sys, fx) : -1;
        const auto* bar = barOf (page, system, index);
        juce::PopupMenu m;
        if (bar != nullptr)
        {
            // the barline nearest the mouse (not the system's two ends)
            int line = -1; double best = 1.0e9;
            for (int i = 1; i < sys->bars(); ++i) if (const double d = std::abs (sys->lines[(size_t) i] - fx); d < best) { best = d; line = i; }
            const double spaceX = sys->space * (double) rects[(size_t) page].getHeight() / (double) juce::jmax (1, rects[(size_t) page].getWidth());       // a staff space, as a part of the page's width
            const bool nearLine = line > 0 && best < 2.5 * spaceX;
            const auto startsHere = [&] { const auto it = bars.editStarts.find (editId.toString()); return it != bars.editStarts.end() && it->second.page == page && it->second.system == system && scorebars::Map::indexAt (*sys, it->second.x) == index; }();
            m.addSectionHeader ("Bar " + juce::String (bar->number) + "  (page " + juce::String (page + 1) + ")");
            m.addItem (1, "This bar's number is...   (the bars after it count on from it)");
            m.addItem (2, "Take off the barline here   (it is not one)", nearLine);
            m.addItem (3, "Add a barline here", ! nearLine && fx > sys->lines[(size_t) index] + 1.2 * spaceX && fx < sys->lines[(size_t) index + 1] - 1.2 * spaceX);
            m.addSeparator();
            m.addItem (4, "This edit's music begins at this bar   (its takes' bar numbers are looked for from here on)", true, startsHere);
            m.addSeparator();
            juce::Component::SafePointer<ScoreComponent> safe (this);
            const int number = bar->number;
            m.addItem (5, "Count the bars from 1 again   (take off the numbers given by hand)", ! bars.numbers.empty());
            m.addItem (6, "Find the bars of the whole score again   (what was put right by hand is lost)");
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, page, system, index, line, fx, number, startsHere] (int r)
            {
                if (safe == nullptr || r == 0) return;
                auto& b = safe->bars;
                if (r == 1)
                {
                    auto* aw = new juce::AlertWindow ("Bar number", "The number printed for this bar in the score. The bars after it count on from it.", juce::MessageBoxIconType::NoIcon, safe->getTopLevelComponent());
                    aw->addTextEditor ("n", juce::String (number));
                    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
                    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
                    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, page, system, index] (int res)
                    {
                        const auto text = aw->getTextEditorContents ("n").trim();
                        if (res == 1 && safe != nullptr && text.containsOnly ("0123456789") && text.isNotEmpty()) { safe->bars.setNumber (page, system, index, text.getIntValue()); safe->barsChanged(); }
                        delete aw;
                    }));
                }
                else if (r == 2) { if (b.joinToBefore (page, system, line)) safe->barsChanged(); }
                else if (r == 3) { if (b.splitAt (page, system, fx)) safe->barsChanged(); }
                else if (r == 4) { if (startsHere) b.editStarts.erase (safe->editId.toString()); else b.editStarts[safe->editId.toString()] = b.anchorOf (page, system, index); safe->barsChanged(); }
                else if (r == 5) { b.numbers.clear(); safe->barsChanged(); }
                else if (r == 6) safe->findBarsAgain();
            });
            return;
        }
        m.addItem (6, "Find the bars of the whole score again   (what was put right by hand is lost)");
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe] (int r) { if (safe != nullptr && r == 6) safe->findBarsAgain(); });
    }
    void findBarsAgain()
    {
        const auto n = bars.pages.size();
        bars = {}; bars.pages.resize (n);
        barsDirty = true;
        recount();
        queueBarPages();
    }

    AppContext& app;
    juce::Uuid editId;
    juce::String shown;                                                    // the score's name as loaded (the edit's may change under the window)
    std::vector<juce::Point<double>> sizes;                                // the pages' sizes (PdfPages.h's units)
    std::vector<juce::Rectangle<int>> rects;                               // where each page is in 'pages'
    std::map<int, juce::Image> pictures;                                   // the pages drawn so far (the ones near the view)
    std::map<int, int> pictureWidth;                                       // ... and the width in pixels each was drawn for
    std::set<int> failed;                                                  // pages the reader could not draw (not asked for again)
    double zoom = 0.0, top = 0.0;                                          // see EditDef::scoreZoom / scoreTop
    bool restoreTop = true;
    int ticks = 0;
    // shared with the worker
    juce::CriticalSection lock;
    juce::File openFile; bool openWanted = false; int generation = 0;
    std::vector<Want> wanted;
    int busyPage = -1, busyW = 0;                                          // the page the worker is drawing (until its picture has arrived here)

    // 3.54.0: the bars and the takes' marks (message thread, but for barPages)
    scorebars::Map bars;                                                   // the score's bars as found and put right
    std::vector<scorebars::Bar> barList;                                   // ... counted (bars.bars())
    juce::File barsFile; juce::int64 pdfBytes = 0; bool barsDirty = false;
    std::vector<scorebars::TakeSpan> spans;                                // the edit's takes that have a bar
    std::vector<scorebars::Bracket> marks;                                 // ... and their brackets
    juce::String marksKey;                                                 // what 'marks' was made from (made again only when it changes)
    std::map<int, std::vector<Hit>> hits;                                  // by page: where each take's number was drawn
    struct Heard { juce::String key; std::vector<juce::Uuid> tried; scorebars::Guess best; };
    std::map<juce::Uuid, Heard> heard;                                     // by take: its bars as the sound gives them
    std::vector<int> barPages;                                             // (shared with the worker) the pages whose bars are still to be found

    Pages pages;
    juce::Viewport viewport;
    juce::TextButton prevButton, nextButton, zoomOutButton, zoomInButton, fitButton, barsButton, fileButton, chooseButton, outsideButton;
    ScoreMenuKey takesButton;
    juce::Label pageLabel, message;
};

/** The Edit window's PDF key: a click opens / closes the score (and asks for the file when the edit has none); a right-click is its menu. */
class ScoreButton : public juce::TextButton
{
public:
    ScoreButton() : juce::TextButton ("PDF") {}
    std::function<void()> onMenu;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { menuPress = true; if (onMenu) onMenu(); return; } menuPress = false; juce::TextButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (menuPress) { menuPress = false; return; } juce::TextButton::mouseUp (e); }
private:
    bool menuPress = false;
};
} // namespace td
