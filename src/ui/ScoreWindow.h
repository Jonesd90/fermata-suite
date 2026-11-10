#pragma once
#include "AppContext.h"
#include "Uikit.h"
#include "PdfPages.h"
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
    when they come back into view. While a page is being drawn at a new size the old picture is shown stretched. */
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

class ScoreComponent : public juce::Component, private juce::Thread, private juce::Timer
{
public:
    ScoreComponent (AppContext& a, const juce::Uuid& eid) : juce::Thread ("Score pages"), app (a), editId (eid), pages (*this)
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        for (auto* b : { &prevButton, &nextButton, &zoomOutButton, &zoomInButton, &fitButton, &fileButton }) { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
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

    // ------------------------------------------------------------------------------------------------ the pages (inside the viewport)
    struct Pages : public juce::Component
    {
        explicit Pages (ScoreComponent& o) : owner (o) { setOpaque (true); }
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
        sizes.clear(); rects.clear(); pictures.clear(); pictureWidth.clear(); failed.clear();
        zoom = e != nullptr ? e->scoreZoom : 0.0; top = e != nullptr ? e->scoreTop : 0.0; restoreTop = true;
        layoutPages();
        const auto file = e != nullptr ? app.project.scoreFileFor (*e) : juce::File();
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
    }

    // ------------------------------------------------------------------------------------------------ the worker
    void run() override
    {
        pdfpages::Doc* doc = nullptr;
        int docGeneration = -1;
        while (! threadShouldExit())
        {
            juce::File toOpen; bool doOpen = false; Want job; bool haveJob = false; int gen = 0;
            {
                const juce::ScopedLock sl (lock);
                gen = generation;
                if (openWanted) { openWanted = false; doOpen = true; toOpen = openFile; }
                else if (! wanted.empty() && docGeneration == generation && busyPage < 0) { job = wanted.front(); wanted.erase (wanted.begin()); haveJob = true; busyPage = job.page; busyW = job.w; }
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

    Pages pages;
    juce::Viewport viewport;
    juce::TextButton prevButton, nextButton, zoomOutButton, zoomInButton, fitButton, fileButton, chooseButton, outsideButton;
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
