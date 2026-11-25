#pragma once
#include "AppContext.h"
#include "Uikit.h"
#include "PdfPages.h"
#include "../core/ScoreBars.h"
#include "../core/PdfWrite.h"
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
    number plays that take (from its start at a start bracket, its last seconds at an end bracket); a right-click sets its bars.

    3.55.0 (asked for on 11 and 12 November 2026): scanned scores, and the bar numbers printed in the score read (both in ScoreBars.h);
    the brackets laid out in the PAGE's own measure (layoutOf: the same at any zoom and in the saved PDF), each put on clear paper above
    its system - never on the staves or the notes, off the words where it can be (scorebars::placeAbove); a bracket is dragged to move it
    and made bigger or smaller from its right-click, the a / A keys size them all, and all of that is kept with the bars; the PDF key's
    menu saves the marked score as a new PDF (saveMarkedPdf: the pages as pictures, the brackets as sharp shapes, written by pdfwrite).

    3.66.0 (asked for on 24 November 2026: "the PDF I have contains four movements in one single document but the four movements are split over
    4 separate take and edit windows. Can we make a function to tell fermata which pages of the PDF that edit and take window is referring to"):
    THIS EDIT'S PAGES. The Pages key (and a right-click on a page) sets the edit's first and last page (EditDef::scoreFirstPage / scoreLastPage).
    Only those pages are shown (the others take no room) and saved in the marked PDF; the edit's music is taken to begin at the first bar of its
    first page - unless a bar is marked "This edit's music begins at this bar" - and the piece's length handed to its take windows ends at its
    last page. The bars of the whole PDF are still found once and kept beside it, shared by every edit that has that PDF. "Bar 1 is the first
    bar on that page" (ticked unless the movement starts part-way down the page) gives that bar the number 1, as "This bar's number is..." does.
    The first time a PDF that another edit already has is opened here, the window asks for this edit's pages, starting after the other's. */
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
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Score").withMessage (err).withButton ("OK").withAssociatedComponent (place::anchor (safeOver.getComponent())), nullptr);
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
        for (auto* b : { &prevButton, &nextButton, &pagesButton, &zoomOutButton, &zoomInButton, &fitButton, &barsButton, static_cast<juce::TextButton*> (&takesButton), &smallerButton, &biggerButton, &fileButton }) { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
        pagesButton.setButtonText ("All pages");
        pagesButton.setTooltip ("Which pages of the PDF are this edit's music - for a PDF with several movements, each in an edit of its own. Only those pages are shown, "
                                "and this edit's bars are counted from the first bar of its first page. (Also from a right-click on a page.)");
        pagesButton.onClick = [this] { askPages (false); };
        smallerButton.setButtonText ("a"); biggerButton.setButtonText ("A");
        smallerButton.setTooltip ("The takes' brackets and numbers smaller, all of them. (One bracket on its own: right-click it. To move one: drag it.)");
        biggerButton.setTooltip ("The takes' brackets and numbers bigger, all of them. (One bracket on its own: right-click it. To move one: drag it.)");
        smallerButton.onClick = [this] { setMarkScale (bars.markScale / 1.12); };
        biggerButton.onClick = [this] { setMarkScale (bars.markScale * 1.12); };
        barsButton.setButtonText ("Bars"); takesButton.setButtonText ("Takes");
        barsButton.setTooltip (barsTip());
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
        put (pagesButton, 80, 8);
        put (zoomOutButton, 28, 2); put (zoomInButton, 28, 6); put (fitButton, 74, 10);
        put (barsButton, 84, 2); put (takesButton, 56, 2); put (smallerButton, 24, 2); put (biggerButton, 24, 10);
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
        if (c == juce::KeyPress::homeKey)     { goToPage (firstPage()); return true; }
        if (c == juce::KeyPress::endKey)      { goToPage (lastPage()); return true; }
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
    /** A take bracket as it is drawn on a page (3.55.0): everything in parts of the page - x of its width, y of its height - so that it is
        the same at any zoom and in the PDF that is saved. Made by layoutOf from the brackets, the paper (scorebars::placeAbove) and what
        the user has moved or resized. */
    struct MarkItem { juce::Rectangle<double> box; juce::String text; int span = 0; bool dud = false; };
    struct MarkDrawn
    {
        int mark = 0;                                       // which of 'marks'
        int page = 0, system = 0, index = 0; bool start = true;      // its bar (for the look the user gives it)
        juce::Rectangle<double> upright, arm, bounds;
        std::vector<juce::Rectangle<double>> backs;         // the pale patch behind each row of numbers
        std::vector<MarkItem> items;                        // the takes' numbers
        double fontH = 0.0;                                 // the numbers' height
        double baseX = 0.0, sysTop = 0.0;                   // where fermata's own place for it is measured from
    };
    struct MarkHit { const MarkDrawn* mark = nullptr; const MarkItem* item = nullptr; };
    /** The marked score being written as a PDF: per page, the shapes to draw on it (in points, from the page's top left). */
    struct ExportFill { juce::Path path; juce::Colour colour; float opacity = 1.0f; };
    struct ExportJob { juce::File to; std::vector<std::vector<ExportFill>> pages; int first = 0, last = -1; };      // (3.66.0: first / last: the edit's pages, counted from 0)

    // ------------------------------------------------------------------------------------------------ the pages (inside the viewport)
    struct Pages : public juce::Component, public juce::TooltipClient
    {
        explicit Pages (ScoreComponent& o) : owner (o) { setOpaque (true); }
        juce::String getTooltip() override { return owner.tipAt (getMouseXYRelative()); }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto hit = owner.hitAt (e.getPosition());
            setMouseCursor (hit.item != nullptr ? juce::MouseCursor::PointingHandCursor : (hit.mark != nullptr ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor));
        }
        void mouseDrag (const juce::MouseEvent& e) override { owner.pageDragged (e); }
        void mouseUp (const juce::MouseEvent& e) override { owner.pageReleased (e); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::manuscript() ? juce::Colour (0xff0b1220) : juce::Colour (0xff2b333d));
            const auto clip = g.getClipBounds();
            for (int i = 0; i < (int) owner.rects.size(); ++i)
            {
                const auto r = owner.rects[(size_t) i];
                if (r.isEmpty() || r.getBottom() < clip.getY() || r.getY() > clip.getBottom()) continue;      // (3.66.0: a page that is not this edit's is not there)
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
                if (owner.takesButton.getToggleState()) owner.drawTakes (g, i, r);
            }
        }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { owner.zoomBy (w.deltaY > 0 ? 1.1 : 1.0 / 1.1); return; }
            juce::Component::mouseWheelMove (e, w);                                         // (the viewport scrolls)
        }
        void mouseDown (const juce::MouseEvent& e) override { owner.grabKeyboardFocus(); owner.pagePressed (e); }
        ScoreComponent& owner;
    };

    // ------------------------------------------------------------------------------------------------ the layout
    EditDef* edit() const { return app.project.findEdit (editId); }
    bool fitWidth() const { return zoom <= 0.0; }
    /** Pixels for each of the page's own units (PdfPages.h: 1/96 inch). Fit width: the widest page fills the window. */
    double scaleNow() const
    {
        if (! fitWidth()) return zoom;
        double widest = 1.0; for (int i = 0; i < (int) sizes.size(); ++i) if (pageShown (i)) widest = juce::jmax (widest, sizes[(size_t) i].x);
        const int room = viewport.getWidth() - viewport.getScrollBarThickness() - 2 * kEdge;
        return juce::jlimit (0.05, 40.0, (double) juce::jmax (80, room) / widest);
    }
    void layoutPages()
    {
        rects.clear();
        const double sc = scaleNow();
        int widest = 0; for (int i = 0; i < (int) sizes.size(); ++i) if (pageShown (i)) widest = juce::jmax (widest, juce::roundToInt (sizes[(size_t) i].x * sc));
        const int fullW = juce::jmax (viewport.getWidth() - viewport.getScrollBarThickness(), widest + 2 * kEdge);
        int y = kEdge;
        const int first = firstPage(), last = lastPage();
        for (int i = 0; i < (int) sizes.size(); ++i)
        {
            const auto& s = sizes[(size_t) i];
            if (i < first || i > last) { rects.push_back ({ fullW / 2, y, 0, 0 }); continue; }      // 3.66.0: not this edit's page - no room, never drawn or found by a click
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
            if (! rects[(size_t) i].isEmpty() && y < rects[(size_t) i].getBottom() + kGap / 2) return (double) i + juce::jlimit (0.0, 0.999, (double) (y - rects[(size_t) i].getY()) / (double) juce::jmax (1, rects[(size_t) i].getHeight()));
        return (double) juce::jmax (0, lastPage()) + 0.999;
    }
    int topPage() const { return (int) topNow(); }
    bool atPageTop() const { const double t = topNow(); return t - std::floor (t) < 0.02; }
    void setTop (double t)
    {
        top = juce::jmax (0.0, t);
        if (rects.empty()) return;
        if (top < (double) firstPage() || top >= (double) lastPage() + 1.0) top = (double) firstPage();      // (3.66.0: only this edit's pages)
        const int i = juce::jlimit (0, (int) rects.size() - 1, (int) top);
        const auto r = rects[(size_t) i];
        viewport.setViewPosition (viewport.getViewPositionX(), juce::jmax (0, r.getY() + juce::roundToInt ((top - (double) i) * r.getHeight()) - (top - (double) i < 0.001 ? kGap / 2 : 0)));       // (a page's top: with a little of the gap above it)
    }
    void goToPage (int i) { if (! rects.empty()) { setTop ((double) juce::jlimit (firstPage(), juce::jmax (firstPage(), lastPage()), i)); wantPictures(); } }
    void setZoom (double z)
    {
        const double keep = topNow();
        zoom = z <= 0.0 ? 0.0 : juce::jlimit (0.1, 12.0, z);
        layoutPages(); setTop (keep); wantPictures();
    }
    void zoomBy (double f) { if (! sizes.empty()) setZoom (scaleNow() * f); }

    // ------------------------------------------------------------------------------------------------ 3.66.0: this edit's pages of the score
    /** The first and the last page shown, counted from 0 and both included: the edit's pages (EditDef::scoreFirstPage / scoreLastPage), or all. */
    int firstPage() const
    {
        const auto* e = edit(); const int n = (int) sizes.size();
        return n == 0 || e == nullptr || e->scoreFirstPage <= 0 ? 0 : juce::jlimit (0, n - 1, e->scoreFirstPage - 1);
    }
    int lastPage() const
    {
        const auto* e = edit(); const int n = (int) sizes.size();
        if (n == 0) return -1;
        return juce::jmax (firstPage(), e == nullptr || e->scoreLastPage <= 0 ? n - 1 : juce::jlimit (0, n - 1, e->scoreLastPage - 1));
    }
    bool pageShown (int i) const { return i >= firstPage() && i <= lastPage(); }
    bool hasPageRange() const { const auto* e = edit(); return e != nullptr && (e->scoreFirstPage > 0 || e->scoreLastPage > 0); }
    juce::String rangeKey() const { const auto* e = edit(); return e == nullptr ? juce::String() : juce::String (e->scoreFirstPage) + "-" + juce::String (e->scoreLastPage); }
    /** Where this edit's music begins in barList: the bar marked "This edit's music begins at this bar" (scorebars::startOrdinal), else the first
        bar of its first page. */
    int startFrom() const
    {
        if (bars.editStarts.count (editId.toString()) > 0) return scorebars::startOrdinal (bars, barList, editId.toString());
        const int first = firstPage();
        for (int i = 0; i < (int) barList.size(); ++i) if (barList[(size_t) i].page >= first) return i;
        return 0;
    }
    /** The bars up to the end of this edit's last page: the piece's length is not counted past it. */
    std::vector<scorebars::Bar> barsToLastPage() const
    {
        std::vector<scorebars::Bar> out;
        const int last = lastPage();
        for (auto& b : barList) { if (last >= 0 && b.page > last) break; out.push_back (b); }
        return out;
    }
    /** The other edits that have this PDF as their score, and their pages ("'Mvt 1' (pages 1-4)"). */
    juce::StringArray otherEditsWithThisPdf() const
    {
        juce::StringArray out;
        const auto* me = edit();
        if (me == nullptr || me->score.isEmpty()) return out;
        for (auto& e : app.project.edits)
            if (e.get() != me && e->score == me->score)
                out.add ("'" + e->name + "'" + (e->scoreFirstPage > 0 || e->scoreLastPage > 0
                                                  ? " (pages " + juce::String (juce::jmax (1, e->scoreFirstPage)) + "-" + (e->scoreLastPage > 0 ? juce::String (e->scoreLastPage) : juce::String ("the end")) + ")"
                                                  : juce::String (" (all its pages)")));
        return out;
    }
    /** The Pages key (and, the first time, a PDF another edit already has): which pages are this edit's. 'first' says why it is asked. */
    void askPages (bool firstTime)
    {
        auto* e = edit();
        if (e == nullptr || sizes.empty()) return;
        askedPages = true;
        const int n = (int) sizes.size();
        int from = firstPage() + 1, to = lastPage() + 1;
        if (! hasPageRange())                                             // (a suggestion: after the last page another edit of this PDF has)
        {
            int after = 0;
            for (auto& o : app.project.edits) if (o.get() != e && o->score == e->score && o->scoreLastPage > 0) after = juce::jmax (after, o->scoreLastPage);
            from = after > 0 && after < n ? after + 1 : 1; to = n;
        }
        const auto others = otherEditsWithThisPdf();
        juce::String msg;
        if (firstTime) msg << "This PDF is also the score of " << others.joinIntoString (", ") << ". Which of its pages are this edit's music?\n\n";
        else msg << "Which pages of \"" << e->score << "\" are this edit's music?" << (others.isEmpty() ? juce::String() : "  (It is also the score of " + others.joinIntoString (", ") + ".)") << "\n\n";
        msg << "Only those pages are shown here, and this edit's bars are counted from the first bar of the first one. The PDF has " << n << " pages.\n\n"
            << "When this edit's music starts part-way down a page, untick \"Bar 1...\" and then right-click its first bar: \"This edit's music begins at this bar\".";
        auto* aw = new juce::AlertWindow ("This edit's pages", msg, juce::MessageBoxIconType::QuestionIcon, place::anchor (getTopLevelComponent()));
        aw->addTextEditor ("from", juce::String (from), "From page");
        aw->addTextEditor ("to", juce::String (to), "To page");
        auto* one = new juce::ToggleButton ("Bar 1 is the first bar on the first of these pages");
        one->setName ({});                                                 // (the box writes a custom part's name above it: the tick box says it already)
        one->setToggleState (true, juce::dontSendNotification);
        one->setSize (420, 26);
        aw->addCustomComponent (one);
        aw->addButton ("Use these pages", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("All the pages", 2);
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ScoreComponent> safe (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw, one, n] (int res)
        {
            const int a = aw->getTextEditorContents ("from").trim().getIntValue(), b = aw->getTextEditorContents ("to").trim().getIntValue();
            const bool barOne = one->getToggleState();
            delete aw; delete one;                                         // (the box first: it lays out the tick box while it lives)
            if (safe == nullptr || res == 0) return;
            if (res == 2) { safe->setPages (0, 0, false); return; }
            if (a < 1 || b < 1 || a > n || b > n)
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("This edit's pages")
                                                  .withMessage ("The pages go from 1 to " + juce::String (n) + ". Nothing was changed.").withButton ("OK")
                                                  .withAssociatedComponent (place::anchor (safe->getTopLevelComponent())), nullptr);
                return;
            }
            safe->setPages (juce::jmin (a, b), juce::jmax (a, b), barOne);
        }));
    }
    /** Sets the edit's pages (counted from 1; 0, 0 = all of them). A marked start of the edit's music outside them is let go. */
    void setPages (int from, int to, bool barOne)
    {
        auto* e = edit();
        if (e == nullptr) return;
        const int n = (int) sizes.size();
        if (from <= 1 && to >= n) { from = 0; to = 0; }                    // (all of them)
        e->scoreFirstPage = from; e->scoreLastPage = to;
        app.project.markDirty();
        if (const auto it = bars.editStarts.find (editId.toString()); it != bars.editStarts.end() && ! pageShown (it->second.page))
        {
            bars.editStarts.erase (it); barsDirty = true;
            juce::Logger::writeToLog ("Score: the start marked for \"" + e->name + "\" was outside its pages " + juce::String (from) + "-" + juce::String (to) + " - let go");
        }
        juce::Logger::writeToLog ("Score: \"" + e->name + "\" - " + (from > 0 || to > 0 ? "pages " + juce::String (juce::jmax (1, from)) + "-" + juce::String (to > 0 ? to : n) : juce::String ("all the pages"))
                                  + " of \"" + shown + "\"");
        barOneOnPage = -1;
        if (barOne && from > 1) { barOneOnPage = from - 1; if (barOneOnPage < (int) bars.pages.size() && bars.pages[(size_t) barOneOnPage].done) barOneHere (barOneOnPage); }
        pagesChanged();
        goToPage (firstPage());
        if (barsDirty) barsChanged();
    }
    /** "Bar 1 is the first bar on that page": that bar is given the number 1 (as "This bar's number is..." does), unless it is 1 already. */
    void barOneHere (int page)
    {
        barOneOnPage = -1;
        for (auto& b : barList)
            if (b.page == page)
            {
                if (b.number != 1) { bars.setNumber (b.page, b.system, b.index, 1); juce::Logger::writeToLog ("Score: the first bar of page " + juce::String (page + 1) + " is bar 1 (it was counted " + juce::String (b.number) + ")"); barsChanged(); }
                return;
            }
    }
    /** The edit's pages changed (here, or by Ctrl + Z, or in another window): laid out again, and the edit's start and length worked out again. */
    void pagesChanged()
    {
        rangeShown = rangeKey();
        const double keep = topNow();
        layoutPages(); setTop (keep); updateLabel(); wantPictures();
        marksKey = "?"; laid.clear(); refreshMarks(); givePieceBars();
        pages.repaint();
    }

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
        bars = {}; barList.clear(); spans.clear(); marks.clear(); laid.clear(); heard.clear(); marksKey.clear(); barsDirty = false; dragging = {};
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
        if (any && page >= 0 && page + 1 < (int) rects.size() && page + 1 <= lastPage() && rects[(size_t) page].getBottom() < viewport.getViewPositionY() + viewport.getHeight() / 3) ++page;     // (not before the pages are laid out)
        const auto text = any ? "Page " + juce::String (page + 1) + " of " + juce::String ((int) sizes.size()) : juce::String();
        if (pageLabel.getText() != text) pageLabel.setText (text, juce::dontSendNotification);
        for (auto* b : { &prevButton, &nextButton, &zoomOutButton, &zoomInButton, &fitButton, &pagesButton }) if (b->isEnabled() != any) b->setEnabled (any);
        const auto keyText = ! any || ! hasPageRange() ? juce::String ("All pages") : "Pages " + juce::String (firstPage() + 1) + (lastPage() > firstPage() ? "-" + juce::String (lastPage() + 1) : juce::String());
        if (pagesButton.getButtonText() != keyText) pagesButton.setButtonText (keyText);
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
        m.addItem (5, exporting ? "Saving the marked score..." : "Save the marked score as a new PDF...   (" + juce::String (hasPageRange() ? "this edit's pages" : "the pages") + " with the takes' brackets on them; this PDF is not touched)", has && ! sizes.empty() && ! exporting);
        m.addSeparator();
        m.addItem (4, "Take the score off this edit  (the file stays in the PDFs folder)", has);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 1) safe->chooseAnother();
            else if (r == 2) safe->openOutside();
            else if (r == 3) { auto dir = safe->app.project.pdfsFolder(); dir.createDirectory(); dir.revealToUser(); }
            else if (r == 5) safe->saveMarkedPdf();
            else if (r == 4) { if (auto* ed = safe->edit()) { ed->score.clear(); ed->scoreTop = 0.0; ed->scoreZoom = 0.0; ed->scoreFirstPage = ed->scoreLastPage = 0; safe->app.project.markDirty(); safe->loadFromEdit(); } }
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
            if (! rects[(size_t) i].isEmpty() && rects[(size_t) i].getBottom() >= y0 && rects[(size_t) i].getY() <= y1) { if (first < 0) first = i; last = i; }
        if (first < 0) return;
        // the pictures far from the pages in view are let go (they are big); the page before and the page after are drawn ahead
        for (auto it = pictures.begin(); it != pictures.end();)
            if (it->first < first - 3 || it->first > last + 3) { pictureWidth.erase (it->first); it = pictures.erase (it); } else ++it;
        double px = 1.0;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) px = d->scale;
        std::vector<Want> list;
        auto add = [&] (int i)
        {
            if (i < 0 || i >= (int) rects.size() || failed.count (i) != 0 || rects[(size_t) i].isEmpty()) return;
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
        if (rangeKey() != rangeShown) pagesChanged();                     // (3.66.0)
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
            std::shared_ptr<ExportJob> exportNow;
            {
                const juce::ScopedLock sl (lock);
                gen = generation;
                if (openWanted) { openWanted = false; doOpen = true; toOpen = openFile; }
                else if (exportWanted != nullptr && docGeneration == generation) { exportNow = std::move (exportWanted); exportWanted = nullptr; }
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
            if (exportNow != nullptr)
            {
                runExport (doc, *exportNow, gen);
                continue;
            }
            if (barPage >= 0)
            {
                // 3.54.0: this page's bars, from a picture of it drawn for the purpose (always the same size, whatever the zoom); 3.55.0:
                // and the numbers printed on it, and the paper above each system
                scorebars::PageFound found;
                double uw = 0.0, uh = 0.0;
                if (doc != nullptr && pdfpages::pageSize (doc, barPage, uw, uh))
                {
                    int w = 0, h = 0; scorebars::analysisSize (uw, uh, w, h);
                    std::vector<unsigned char> png, grey; std::string err; int gw = 0, gh = 0;
                    if (pdfpages::render (doc, barPage, w, h, png, err) && ! png.empty() && greyOf (juce::ImageFileFormat::loadFrom (png.data(), png.size()), grey, gw, gh))
                        found = scorebars::analysePage (grey.data(), gw, gh, gw);
                }
                const int page = barPage;
                auto shared = std::make_shared<scorebars::PageFound> (std::move (found));
                juce::MessageManager::callAsync ([safe, gen, page, shared] { if (safe != nullptr) safe->barsFound (gen, page, *shared); });
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
        rangeShown = rangeKey();
        layoutPages();
        setTop (top); restoreTop = false;
        updateLabel(); wantPictures();
        loadBars();
        // 3.66.0: a PDF that another edit already has - the movements of one PDF in edits of their own - asks for this edit's pages once
        if (auto* e = edit(); e != nullptr && ! askedPages && ! hasPageRange() && sizes.size() > 1 && ! otherEditsWithThisPdf().isEmpty())
        {
            juce::Component::SafePointer<ScoreComponent> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->askPages (true); });
        }
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
    static juce::String barsTip()
    {
        return "Shows the bars fermata found on the pages and their numbers - to check them. A green number was read from the print and agrees; a blue one is counted on from there; "
               "an orange one with a ! is where the print and the count disagreed (look at the bars just before it for a barline too many or too few). "
               "Right-click a bar to give it its number (the bars after it count on), to take a barline off or to add one.";
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
    void recount() { barList = bars.bars(); marksKey = "?"; laid.clear(); pages.repaint(); }
    void looksChanged() { barsDirty = true; laid.clear(); pages.repaint(); }
    void setMarkScale (double v) { bars.markScale = juce::jlimit (0.4, 3.0, v); looksChanged(); saveBars(); }
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
        const int count = scorebars::pieceLength (barsToLastPage(), startFrom());      // (3.66.0: from the edit's first page, or its marked start, to its last page)
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
        barsButton.setTooltip (barsTip() + " This score: " + juce::String (count) + " bars, which a take window that had no number of its own is given as the bars in the piece (type another there if it is wrong: yours stays).");
    }
    void barsFound (int gen, int page, const scorebars::PageFound& found)
    {
        if (gen != generation || page < 0 || page >= (int) bars.pages.size()) return;
        bars.pages[(size_t) page].systems = found.systems; bars.pages[(size_t) page].reads = found.reads; bars.pages[(size_t) page].done = true;
        if (page == barOneOnPage) { recount(); barOneHere (page); }       // (3.66.0: it waited for this page's bars)
        if (found.levelled) juce::Logger::writeToLog ("Score: page " + juce::String (page + 1) + " of \"" + shown + "\" is a scanned or photographed page (turned " + juce::String (found.tilt, 2) + " degrees; its staves laid level before the bars were looked for)");
        barsDirty = true;
        recount();
        if (bars.complete())
        {
            givePieceBars();
            saveBars();
            int systemsN = 0; for (auto& p : bars.pages) systemsN += (int) p.systems.size();
            int seen = 0, jumps = 0; for (auto& b : barList) { seen += b.read ? 1 : 0; jumps += b.jump ? 1 : 0; }
            juce::Logger::writeToLog ("Score: the bars of \"" + shown + "\" found - " + juce::String ((int) bars.pages.size()) + " pages, " + juce::String (systemsN) + " systems, " + juce::String ((int) barList.size()) + " bars; "
                                      + juce::String (seen) + " printed bar numbers read" + (jumps > 0 ? ", " + juce::String (jumps) + " places where the print and the count disagree" : juce::String()));
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
            // the number's tab: blue = counted; green = the number printed there was read and agrees; orange with a ! = the print and the
            // count parted company on the way here (look at the bars before it)
            const auto text = juce::String (b.number) + (b.jump ? " !" : "");
            const juce::Rectangle<float> tab (box.getX(), box.getY() - fh - 2.0f, juce::GlyphArrangement::getStringWidth (font, text) + 8.0f, fh + 2.0f);
            g.setColour (b.jump ? juce::Colour (0xffd9730d) : (b.read ? juce::Colour (0xff1d8a4a) : ink)); g.fillRect (tab);
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
            key << "from" << startFrom();
        }
        if (key == marksKey) return;
        marksKey = key; spans = std::move (list);
        marks = scorebars::brackets (barList, spans, startFrom());
        laid.clear(); pages.repaint();
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
    /** A text's width at a letter height of 1 (bold, as the numbers are drawn). */
    static double widthOf (const juce::String& text)
    {
        static const juce::Font font (juce::FontOptions (100.0f, juce::Font::bold));
        return (double) juce::GlyphArrangement::getStringWidth (font, text) / 100.0;
    }
    /** The brackets of one page, laid out (kept until the brackets, the bars or a look change). The brackets the user has moved take
        their places first; the others are then each put on clear paper above their system, clear of those and of each other. */
    const std::vector<MarkDrawn>& layoutOf (int page)
    {
        if (const auto it = laid.find (page); it != laid.end()) return it->second;
        auto& out = laid[page];
        if (page < 0 || page >= (int) sizes.size() || page >= (int) bars.pages.size()) return out;
        const double aspect = sizes[(size_t) page].x / juce::jmax (1.0, sizes[(size_t) page].y);       // the page's width over its height
        std::vector<scorebars::PageRect> taken;
        for (int pass = 0; pass < 2; ++pass)
            for (int mi = 0; mi < (int) marks.size(); ++mi)
            {
                const auto& m = marks[(size_t) mi];
                if (m.page != page || m.ordinal < 0 || m.ordinal >= (int) barList.size()) continue;
                const auto* sys = bars.systemAt (page, m.system); if (sys == nullptr) continue;
                const int index = barList[(size_t) m.ordinal].index;
                const auto* look = bars.lookOf (page, m.system, index, m.start);
                const bool moved = look != nullptr && look->moved;
                if (moved != (pass == 0)) continue;
                // sized by the staff - but never smaller than on a staff of the usual size (a full score's staves are small) - times the
                // size chosen for all the marks, times this one's own
                const double uY = juce::jmax (sys->space, 0.0062) * bars.markScale * (look != nullptr ? look->scale : 1.0), uX = uY / aspect;
                const double fh = 3.0 * uY, rowH = 3.2 * uY, thickX = 0.3 * uX, thickY = 0.3 * uY, arm = 3.8 * uX;
                bool both = false;                                                         // an end and a start at the same barline stand side by side
                for (auto& o : marks) if (o.page == m.page && o.system == m.system && o.start != m.start && std::abs (o.x - m.x) < 1.5 * uX) both = true;
                const double baseX = m.x + (both ? (m.start ? 0.5 : -0.5) * uX : 0.0);
                const double x = baseX + (moved ? look->dx : 0.0);
                // the numbers, in the takes' order, in as many rows as they need
                struct Item { juce::String text; double w = 0.0; int span = 0; };
                std::vector<std::vector<Item>> rows (1);
                const double room = juce::jlimit (10.0 * uX, 42.0 * uX, (m.start ? 1.0 - x : x) - 2.0 * uX);
                double used = 0.0, widest = 0.0;
                for (size_t i = 0; i < m.takes.size(); ++i)
                {
                    const auto& t = spans[(size_t) m.takes[i]];
                    Item it; it.span = m.takes[i];
                    it.text = juce::String (t.number) + ((m.start ? t.inGuessed : t.outGuessed) ? "?" : "") + (i + 1 < m.takes.size() ? "," : "");
                    it.w = widthOf (it.text) * fh / aspect + 0.5 * uX;
                    if (used + it.w > room && ! rows.back().empty()) { rows.emplace_back(); used = 0.0; }
                    rows.back().push_back (it); used += it.w; widest = juce::jmax (widest, used);
                }
                const double blockW = juce::jmax (arm, widest + 1.0 * uX), blockH = (double) rows.size() * rowH + 1.1 * uY;
                const double left = m.start ? x - thickX * 0.5 : x + thickX * 0.5 - blockW;
                const double top = moved ? sys->top + look->dy : scorebars::placeAbove (*sys, left, blockW, blockH, taken);
                taken.push_back ({ left, top, blockW, blockH });
                MarkDrawn d;
                d.mark = mi; d.page = page; d.system = m.system; d.index = index; d.start = m.start; d.fontH = fh; d.baseX = baseX; d.sysTop = sys->top;
                d.bounds = { left, top, blockW, blockH };
                d.upright = { x - thickX * 0.5, top, thickX, blockH };
                d.arm = { m.start ? x - thickX * 0.5 : x + thickX * 0.5 - arm, top, arm, thickY };
                for (size_t k = 0; k < rows.size(); ++k)
                {
                    double w = 0.0; for (auto& it : rows[k]) w += it.w;
                    double ix = m.start ? x + 0.8 * uX : x - 0.8 * uX - w;
                    const double iy = top + 0.9 * uY + (double) k * rowH;
                    d.backs.push_back ({ ix - 0.2 * uX, iy, w + 0.2 * uX, rowH - 0.1 * uY });
                    for (auto& it : rows[k]) { d.items.push_back ({ { ix, iy, it.w, rowH - 0.1 * uY }, it.text, it.span, spans[(size_t) it.span].dud }); ix += it.w; }
                }
                out.push_back (std::move (d));
            }
        return out;
    }
    static juce::Rectangle<float> onPage (const juce::Rectangle<double>& part, juce::Rectangle<float> page)
    {
        return { page.getX() + (float) part.getX() * page.getWidth(), page.getY() + (float) part.getY() * page.getHeight(), (float) part.getWidth() * page.getWidth(), (float) part.getHeight() * page.getHeight() };
    }
    static juce::Colour penColour() { return juce::Colour (0xffd3202f); }                    // a red pen on the print
    static juce::Colour dudColour() { return juce::Colour (0xff7f8894); }                    // a Dud take's number
    void drawTakes (juce::Graphics& g, int page, juce::Rectangle<int> r)
    {
        const auto pageRect = r.toFloat();
        for (auto& d : layoutOf (page))
        {
            auto up = onPage (d.upright, pageRect), arm = onPage (d.arm, pageRect);
            if (up.getWidth() < 1.5f) up = up.withSizeKeepingCentre (1.5f, up.getHeight());
            if (arm.getHeight() < 1.5f) arm.setHeight (1.5f);
            g.setColour (penColour()); g.fillRect (up); g.fillRect (arm);
            const float fh = (float) d.fontH * pageRect.getHeight();
            for (auto& b : d.backs)
            {
                const auto back = onPage (b, pageRect);
                g.setColour (juce::Colours::white.withAlpha (0.78f));                         // (whatever is printed there stays readable under the number, but faint)
                g.fillRoundedRectangle (back, back.getHeight() * 0.12f);
            }
            g.setFont (juce::Font (juce::FontOptions (fh, juce::Font::bold)));
            for (auto& it : d.items)
            {
                g.setColour (it.dud ? dudColour() : penColour());
                g.drawText (it.text, onPage (it.box, pageRect).expanded (2.0f, 0.0f).translated (2.0f, 0.0f), juce::Justification::centredLeft, false);
            }
            if (dragging.active && dragging.page == d.page && dragging.system == d.system && dragging.index == d.index && dragging.start == d.start)
            { g.setColour (penColour().withAlpha (0.5f)); g.drawRect (onPage (d.bounds, pageRect).expanded (3.0f), 1.0f); }
        }
    }
    /** The bracket (and the take's number, if any) under a point of 'pages'. */
    MarkHit hitAt (juce::Point<int> p)
    {
        if (! takesButton.getToggleState()) return {};
        double fx = 0.0, fy = 0.0;
        const int page = pageAt (p, fx, fy);
        if (page < 0) return {};
        const auto& list = layoutOf (page);
        for (auto& d : list) for (auto& it : d.items) if (it.box.contains (fx, fy)) return { &d, &it };
        for (auto& d : list) if (d.bounds.expanded (d.fontH * 0.1, d.fontH * 0.1).contains (fx, fy)) return { &d, nullptr };
        return {};
    }
    TakeGroup* takeOf (const scorebars::TakeSpan& s) const { return app.project.findTake (s.window, s.take); }
    juce::String tipAt (juce::Point<int> p)
    {
        const auto hit = hitAt (p);
        if (hit.item != nullptr && hit.item->span < (int) spans.size())
        {
            const auto& s = spans[(size_t) hit.item->span];
            juce::String t = "Take " + juce::String (s.number) + (s.dud ? " (Dud)" : "");
            if (s.barIn > 0) t << ": bar " << s.barIn << (s.inGuessed ? " (from its sound)" : "");
            if (s.barOut > 0) t << (s.barIn > 0 ? " to bar " : ": to bar ") << s.barOut << (s.outGuessed ? " (from its sound)" : "");
            t << (hit.mark->start ? ". Click to hear it from its start" : ". Click to hear how it ended") << "; right-click to set its bars. Drag to move the bracket.";
            return t;
        }
        if (hit.mark != nullptr) return "Drag to move this bracket; right-click to make it bigger or smaller, or to put it back where fermata had it.";
        return {};
    }
    // ---- the mouse on the pages: a click on a number plays its take, a drag moves a bracket, anything else moves the page about ----
    struct Dragging { bool active = false, moved = false; int page = 0, system = 0, index = 0; bool start = true; double x0 = 0.0, top0 = 0.0, baseX = 0.0, sysTop = 0.0; juce::Point<int> from; };
    void pagePressed (const juce::MouseEvent& e)
    {
        dragging = {};
        const auto hit = hitAt (e.getPosition());
        pages.setViewportIgnoreDragFlag (hit.mark != nullptr);                              // (on a bracket the drag is the bracket's, not the page's)
        if (hit.mark == nullptr || e.mods.isPopupMenu()) return;
        const auto& d = *hit.mark;
        dragging.active = true; dragging.page = d.page; dragging.system = d.system; dragging.index = d.index; dragging.start = d.start;
        dragging.x0 = d.upright.getCentreX(); dragging.top0 = d.bounds.getY(); dragging.baseX = d.baseX; dragging.sysTop = d.sysTop; dragging.from = e.getPosition();
    }
    void pageDragged (const juce::MouseEvent& e)
    {
        if (! dragging.active || dragging.page >= (int) rects.size()) return;
        const auto moved = e.getPosition() - dragging.from;
        if (! dragging.moved && moved.getDistanceFromOrigin() < 4) return;
        dragging.moved = true;
        const auto r = rects[(size_t) dragging.page];
        auto& look = bars.lookToSet (dragging.page, dragging.system, dragging.index, dragging.start);
        look.moved = true;
        look.dx = dragging.x0 + (double) moved.x / juce::jmax (1, r.getWidth()) - dragging.baseX;
        look.dy = dragging.top0 + (double) moved.y / juce::jmax (1, r.getHeight()) - dragging.sysTop;
        looksChanged();
    }
    void pageReleased (const juce::MouseEvent& e)
    {
        pages.setViewportIgnoreDragFlag (false);
        const bool wasMove = dragging.active && dragging.moved;
        dragging = {};
        if (wasMove) { saveBars(); pages.repaint(); return; }
        if (e.mouseWasDraggedSinceMouseDown()) return;                                      // (the page was moved about)
        const auto hit = hitAt (e.getPosition());
        if (hit.item != nullptr)
        {
            const int span = hit.item->span; const auto mark = *hit.mark;
            if (e.mods.isPopupMenu()) takeMenu (span, mark); else playTake (span, mark.start);
            return;
        }
        if (hit.mark != nullptr) { if (e.mods.isPopupMenu()) { const auto mark = *hit.mark; markMenu (mark); } return; }
        if (e.mods.isPopupMenu()) barMenu (e.getPosition());
    }
    /** What a right-click on a bracket offers for the bracket itself (ids from 50). */
    void addMarkItems (juce::PopupMenu& m, const MarkDrawn& d)
    {
        const auto* look = bars.lookOf (d.page, d.system, d.index, d.start);
        m.addItem (50, "This bracket bigger");
        m.addItem (51, "This bracket smaller");
        m.addItem (52, "Put this bracket back where fermata had it, at the usual size", look != nullptr);
    }
    bool doMarkItem (int r, const MarkDrawn& d)
    {
        if (r < 50 || r > 52) return false;
        if (r == 52) bars.forgetLook (d.page, d.system, d.index, d.start);
        else { auto& look = bars.lookToSet (d.page, d.system, d.index, d.start); look.scale = juce::jlimit (0.3, 4.0, look.scale * (r == 50 ? 1.15 : 1.0 / 1.15)); }
        looksChanged(); saveBars();
        return true;
    }
    void markMenu (const MarkDrawn& d)
    {
        juce::PopupMenu m;
        m.addSectionHeader (juce::String (d.start ? "Takes that started at bar " : "Takes that stopped after bar ") + juce::String (d.mark < (int) marks.size() ? marks[(size_t) d.mark].bar : 0) + "   (drag it to move it)");
        addMarkItems (m, d);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, d] (int r) { if (safe != nullptr && r != 0) safe->doMarkItem (r, d); });
    }
    void playTake (int span, bool start)
    {
        if (span < 0 || span >= (int) spans.size()) return;
        const auto s = spans[(size_t) span];
        auto* g = takeOf (s); if (g == nullptr) return;
        const double from = start ? 0.0 : juce::jmax (0.0, g->lengthSeconds() - 8.0);
        const auto err = app.playTake (s.window, s.take, from);
        if (err.isNotEmpty()) juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle ("Take " + juce::String (s.number)).withMessage (err).withButton ("OK").withAssociatedComponent (place::anchor (getTopLevelComponent())), nullptr);
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
    void takeMenu (int span, const MarkDrawn& mark)
    {
        if (span < 0 || span >= (int) spans.size()) return;
        const auto s = spans[(size_t) span];
        const bool guessed = s.inGuessed || s.outGuessed, start = mark.start;
        juce::PopupMenu m;
        m.addSectionHeader ("Take " + juce::String (s.number) + (s.barIn > 0 ? "  -  " + barsLabel (s.barIn, s.barOut) : juce::String()) + (guessed ? "  (? = from its sound)" : ""));
        m.addItem (1, start ? "Play it from its start" : "Play its last seconds");
        m.addItem (2, "Set its bars...");
        if (guessed) m.addItem (3, "Keep these bars for the take  (" + barsLabel (s.barIn, s.barOut) + ")", s.barIn > 0);
        m.addSeparator();
        addMarkItems (m, mark);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, span, start, mark, s] (int r)
        {
            if (safe == nullptr || r == 0 || safe->doMarkItem (r, mark)) return;
            if (r == 1) safe->playTake (span, start);
            else if (r == 3) safe->setTakeBars (s.window, s.take, s.barIn, s.barOut);
            else if (r == 2)
            {
                auto* aw = new juce::AlertWindow ("Take " + juce::String (s.number), "The bars this take starts and ends in. (An empty end: not known.)", juce::MessageBoxIconType::NoIcon, place::anchor (safe->getTopLevelComponent()));
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
        if (page < 0) return;
        if (page >= (int) bars.pages.size() || ! bars.pages[(size_t) page].done)       // (3.66.0: its bars not found yet - the pages can be set all the same)
        {
            juce::PopupMenu pm; addPageItems (pm, page);
            juce::Component::SafePointer<ScoreComponent> safe (this);
            pm.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, page] (int r) { if (safe != nullptr && r >= 20) safe->pageItem (r, page); });
            return;
        }
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
            m.addItem (7, "Use the bar numbers printed in the score   (where fermata could read them: the green tabs)", true, bars.useReads);
            m.addItem (5, "Count the bars from 1 again   (take off the numbers given by hand)", ! bars.numbers.empty());
            m.addItem (6, "Find the bars of the whole score again   (what was put right by hand is lost)");
            m.addSeparator(); addPageItems (m, page);
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, page, system, index, line, fx, number, startsHere] (int r)
            {
                if (safe == nullptr || r == 0) return;
                if (r >= 20) { safe->pageItem (r, page); return; }
                auto& b = safe->bars;
                if (r == 1)
                {
                    auto* aw = new juce::AlertWindow ("Bar number", "The number printed for this bar in the score. The bars after it count on from it.", juce::MessageBoxIconType::NoIcon, place::anchor (safe->getTopLevelComponent()));
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
                else if (r == 7) { b.useReads = ! b.useReads; safe->barsChanged(); }
                else if (r == 6) safe->findBarsAgain();
            });
            return;
        }
        m.addItem (6, "Find the bars of the whole score again   (what was put right by hand is lost)");
        m.addSeparator(); addPageItems (m, page);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, page] (int r) { if (safe == nullptr) return; if (r == 6) safe->findBarsAgain(); else if (r >= 20) safe->pageItem (r, page); });
    }
    /** 3.66.0: a right-click on a page - this edit's pages begin or end here, or all of them again. */
    void addPageItems (juce::PopupMenu& m, int page)
    {
        m.addSectionHeader ("This edit's pages  (page " + juce::String (page + 1) + ")");
        m.addItem (20, "This edit's pages begin at this page", page != firstPage() || ! hasPageRange());
        m.addItem (21, "This edit's pages end at this page", page != lastPage() || ! hasPageRange());
        m.addItem (22, "Which pages...");
        m.addItem (23, "Show all the PDF's pages again", hasPageRange());
    }
    void pageItem (int r, int page)
    {
        const int n = (int) sizes.size();
        if (n == 0) return;
        const int from = firstPage() + 1, to = lastPage() + 1;
        if (r == 20) setPages (page + 1, juce::jmax (page + 1, to), false);
        else if (r == 21) setPages (juce::jmin (from, page + 1), page + 1, false);
        else if (r == 22) askPages (false);
        else if (r == 23) setPages (0, 0, false);
    }
    void findBarsAgain()
    {
        const auto n = bars.pages.size();
        bars = {}; bars.pages.resize (n);
        barsDirty = true;
        recount();
        queueBarPages();
    }

    // ------------------------------------------------------------------------------------------------ 3.55.0: the marked score as a new PDF
    /** "I DO want to be able to print the score to a new PDF" (11 November 2026). Each page of the score drawn as a picture at 300 dots
        to the inch, with the takes' brackets and numbers on top as sharp shapes (the very outlines the window draws, at the places the
        window shows them), written by pdfwrite. The score's own PDF is not touched. The pictures are made by the worker (the one
        thread that may use the PDF reader); the shapes are made here first, on the message thread, because they need the fonts. */
    void saveMarkedPdf()
    {
        auto* e = edit();
        if (e == nullptr || sizes.empty() || exporting) return;
        const auto source = app.project.scoreFileFor (*e);
        const auto suggested = source.getSiblingFile (source.getFileNameWithoutExtension() + " - takes marked.pdf");
        auto chooser = std::make_shared<juce::FileChooser> ("Save the marked score as a new PDF", suggested, "*.pdf");
        juce::Component::SafePointer<ScoreComponent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [chooser, safe, source] (const juce::FileChooser& fc)
        {
            auto to = fc.getResult();
            if (safe == nullptr || to == juce::File()) return;
            to = to.withFileExtension ("pdf");
            if (to == source)
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Marked score")
                                                  .withMessage ("That is the score itself. Choose another name: the marked score is a new PDF, and the score stays as it is.").withButton ("OK").withAssociatedComponent (place::anchor (safe->getTopLevelComponent())), nullptr);
                return;
            }
            safe->startExport (to);
        });
    }
    void startExport (const juce::File& to)
    {
        if (exporting || sizes.empty()) return;
        refreshMarks();
        auto job = std::make_shared<ExportJob>();
        job->to = to; job->first = firstPage(); job->last = lastPage();
        for (int p = 0; p < (int) sizes.size(); ++p)
        {
            std::vector<ExportFill> fills;
            if (! pageShown (p)) { job->pages.push_back ({}); continue; }       // (3.66.0: only this edit's pages are saved)
            const juce::Rectangle<float> pageRect (0.0f, 0.0f, (float) (sizes[(size_t) p].x * 0.75), (float) (sizes[(size_t) p].y * 0.75));      // in points (the pages' own unit is 1/96 inch)
            if (takesButton.getToggleState())
                for (auto& d : layoutOf (p))
                {
                    juce::Path lines;
                    auto up = onPage (d.upright, pageRect), arm = onPage (d.arm, pageRect);
                    if (up.getWidth() < 0.6f) up = up.withSizeKeepingCentre (0.6f, up.getHeight());
                    if (arm.getHeight() < 0.6f) arm.setHeight (0.6f);
                    lines.addRectangle (up); lines.addRectangle (arm);
                    fills.push_back ({ lines, penColour(), 1.0f });
                    for (auto& b : d.backs) { const auto back = onPage (b, pageRect); juce::Path patch; patch.addRoundedRectangle (back, back.getHeight() * 0.12f); fills.push_back ({ patch, juce::Colours::white, 0.78f }); }
                    const juce::Font font (juce::FontOptions ((float) d.fontH * pageRect.getHeight(), juce::Font::bold));
                    for (auto& it : d.items)
                    {
                        const auto box = onPage (it.box, pageRect);
                        juce::GlyphArrangement glyphs;
                        glyphs.addLineOfText (font, it.text, box.getX(), box.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f);
                        juce::Path text; glyphs.createPath (text);
                        fills.push_back ({ text, it.dud ? dudColour() : penColour(), 1.0f });
                    }
                }
            job->pages.push_back (std::move (fills));
        }
        exporting = true; exportDone = 0;
        fileButton.setButtonText ("PDF 0/" + juce::String (lastPage() - firstPage() + 1));
        { const juce::ScopedLock sl (lock); exportWanted = job; }
        notify();
    }
    static void addFill (pdfwrite::Drawing& drawing, const ExportFill& fill)
    {
        if (fill.path.isEmpty()) return;
        drawing.begin (fill.colour.getFloatRed(), fill.colour.getFloatGreen(), fill.colour.getFloatBlue(), fill.opacity);
        juce::Path::Iterator it (fill.path);
        juce::Point<float> at;
        while (it.next())
        {
            if (it.elementType == juce::Path::Iterator::startNewSubPath) { drawing.moveTo (it.x1, it.y1); at = { it.x1, it.y1 }; }
            else if (it.elementType == juce::Path::Iterator::lineTo) { drawing.lineTo (it.x1, it.y1); at = { it.x1, it.y1 }; }
            else if (it.elementType == juce::Path::Iterator::quadraticTo)                    // (a PDF has no such curve: the same curve written as a cubic one)
            {
                const juce::Point<float> c (it.x1, it.y1), end (it.x2, it.y2);
                const auto c1 = at + (c - at) * (2.0f / 3.0f), c2 = end + (c - end) * (2.0f / 3.0f);
                drawing.curveTo (c1.x, c1.y, c2.x, c2.y, end.x, end.y); at = end;
            }
            else if (it.elementType == juce::Path::Iterator::cubicTo) { drawing.curveTo (it.x1, it.y1, it.x2, it.y2, it.x3, it.y3); at = { it.x3, it.y3 }; }
            else if (it.elementType == juce::Path::Iterator::closePath) drawing.close();
        }
        drawing.fill (! fill.path.isUsingNonZeroWinding());
    }
    /** (Worker thread.) Writes the PDF beside its place first and moves it there when it is whole. */
    void runExport (pdfpages::Doc* doc, const ExportJob& job, int gen)
    {
        juce::String error; bool ok = false;
        juce::TemporaryFile temp (job.to);
        juce::Component::SafePointer<ScoreComponent> safe (this);
        {
            juce::FileOutputStream stream (temp.getFile());
            if (doc == nullptr) error = "The score is not open.";
            else if (! stream.openedOk()) error = "A file cannot be written there (" + job.to.getParentDirectory().getFullPathName() + ").";
            else
            {
                stream.setPosition (0); stream.truncate();
                pdfwrite::Writer writer (stream);
                const int n = pdfpages::pageCount (doc);
                const int first = juce::jlimit (0, juce::jmax (0, n - 1), job.first), last = job.last < 0 ? n - 1 : juce::jlimit (first, juce::jmax (first, n - 1), job.last);     // (3.66.0: the edit's pages)
                ok = n > 0;
                for (int p = first; p <= last && ok; ++p)
                {
                    bool stale = threadShouldExit();
                    { const juce::ScopedLock sl (lock); stale = stale || generation != gen; }
                    if (stale) { ok = false; error = "stopped"; break; }
                    double uw = 0.0, uh = 0.0;
                    if (! pdfpages::pageSize (doc, p, uw, uh)) { uw = 595.0 * 96.0 / 72.0; uh = 842.0 * 96.0 / 72.0; }
                    double f = 300.0 / 96.0;                                             // 300 dots to the inch - less for a page so big that its picture would be unwieldy
                    if (juce::jmax (uw, uh) * f > 4200.0) f = 4200.0 / juce::jmax (uw, uh);
                    std::vector<unsigned char> png, grey; std::string err; int gw = 0, gh = 0;
                    if (! (pdfpages::render (doc, p, juce::roundToInt (uw * f), juce::roundToInt (uh * f), png, err) && ! png.empty() && greyOf (juce::ImageFileFormat::loadFrom (png.data(), png.size()), grey, gw, gh)))
                    { ok = false; error = "Page " + juce::String (p + 1) + " could not be drawn" + (err.empty() ? juce::String (".") : ": " + juce::String::fromUTF8 (err.c_str())); break; }
                    pdfwrite::Drawing drawing (uh * 0.75);
                    if (p < (int) job.pages.size()) for (auto& fill : job.pages[(size_t) p]) addFill (drawing, fill);
                    ok = writer.addPage (uw * 0.75, uh * 0.75, grey.data(), gw, gh, gw, &drawing);
                    if (! ok) error = "The drive would not take the file (is it full?).";
                    const int done = p - first + 1, all = last - first + 1;
                    juce::MessageManager::callAsync ([safe, gen, done, all] { if (safe != nullptr && gen == safe->generation && safe->exporting) { safe->exportDone = done; safe->fileButton.setButtonText ("PDF " + juce::String (done) + "/" + juce::String (all)); } });
                }
                if (ok) { ok = writer.finish (job.to.getFileNameWithoutExtension()); if (! ok) error = "The drive would not take the file (is it full?)."; }
                stream.flush();
            }
        }
        if (ok && ! temp.overwriteTargetFileWithTemporary()) { ok = false; error = "The file could not be put in its place (is a PDF of that name open in another program?)."; }
        const auto to = job.to;
        juce::MessageManager::callAsync ([safe, ok, to, error] { if (safe != nullptr) safe->exportFinished (ok, to, error); });
    }
    void exportFinished (bool ok, const juce::File& to, const juce::String& error)
    {
        exporting = false;
        fileButton.setButtonText ("PDF...");
        if (error == "stopped") return;
        juce::Logger::writeToLog (ok ? "Score: the marked score saved as " + to.getFullPathName() + " (" + juce::String (lastPage() - firstPage() + 1) + " pages, " + juce::File::descriptionOfSizeInBytes (to.getSize()) + ")"
                                     : "Score: the marked score could not be saved to " + to.getFullPathName() + ": " + error);
        if (! ok)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::WarningIcon).withTitle ("Marked score").withMessage ("The marked score could not be saved.\n" + error).withButton ("OK").withAssociatedComponent (place::anchor (getTopLevelComponent())), nullptr);
            return;
        }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle ("Marked score")
                                          .withMessage ("Saved: " + to.getFileName() + "\n(" + to.getParentDirectory().getFullPathName() + ")\n\nIts pages are pictures of the score with the takes' brackets on top; the score's own PDF is as it was.")
                                          .withButton ("Open it").withButton ("Show it in its folder").withButton ("Close").withAssociatedComponent (place::anchor (getTopLevelComponent())),
                                      [to] (int r) { if (r == 1) to.startAsProcess(); else if (r == 2) to.revealToUser(); });
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
    std::map<int, std::vector<MarkDrawn>> laid;                            // by page: the brackets as laid out (layoutOf)
    Dragging dragging;                                                     // a bracket being moved with the mouse
    std::shared_ptr<ExportJob> exportWanted;                               // (shared with the worker) the marked score to be written as a PDF
    bool exporting = false; int exportDone = 0;
    struct Heard { juce::String key; std::vector<juce::Uuid> tried; scorebars::Guess best; };
    std::map<juce::Uuid, Heard> heard;                                     // by take: its bars as the sound gives them
    std::vector<int> barPages;                                             // (shared with the worker) the pages whose bars are still to be found

    Pages pages;
    juce::Viewport viewport;
    juce::TextButton prevButton, nextButton, zoomOutButton, zoomInButton, fitButton, barsButton, smallerButton, biggerButton, fileButton, chooseButton, outsideButton;
    juce::TextButton pagesButton;                                          // 3.66.0: this edit's pages
    ScoreMenuKey takesButton;
    juce::String rangeShown = "?";                                         // 3.66.0: the edit's pages as laid out ("5-8", "" for all of them)
    int barOneOnPage = -1;                                                 // ... "Bar 1 is the first bar on that page", waiting for that page's bars to be found
    bool askedPages = false;                                               // ... the window has asked for this edit's pages once
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
