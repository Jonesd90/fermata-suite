#include "LiveTakeView.h"
#include "../core/AudioFormats.h"
#include <set>

namespace td::live
{
static juce::Colour flagRed()    { return juce::Colour (0xffff5a3c); }
static juce::Colour replyGreen() { return juce::Colour (0xff7ee08a); }
static juce::Colour noteGold()   { return juce::Colour (0xffffc94d); }
static juce::Colour pointYellow() { return juce::Colour (0xffffe14d); }
static juce::Colour nowBlue()    { return juce::Colour (0xff8fd0ff); }

LiveTakeView::LiveTakeView()
{
    registerAudioFormats (formats);
    setWantsKeyboardFocus (false);
    setOpaque (true);                                                       // (paint() covers all of it: the window behind is not painted again)
    for (auto* b : { &hbar, &vbar })
    {
        addAndMakeVisible (b);
        b->addListener (this);
        b->setAutoHide (false);
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff8a97a8));
        b->setColour (juce::ScrollBar::trackColourId, theme::panel.darker (0.3f));
        b->setColour (juce::ScrollBar::backgroundColourId, theme::panel.darker (0.3f));
    }
}

LiveTakeView::~LiveTakeView() = default;

void LiveTakeView::repaintIfChanged()
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { h ^= v; h *= 1099511628211ull; };
    auto mixD = [&mix] (double d) { juce::uint64 u = 0; std::memcpy (&u, &d, sizeof (u)); mix (u); };
    auto mixX = [&] (double seconds) { mix (seconds >= 0.0 ? (juce::uint64) (juce::int64) xOf (seconds) : 0x5eedull); };
    mixX (playhead); mixX (liveEnd); mixX (writtenEnd); mixX (mark1); mixX (mark2); mixX (nowLine); mixX (flagPoint);
    mixD (viewStart); mixD (pxPerSec); mix ((juce::uint64) laneH); mixD ((double) vzoom); mix ((juce::uint64) scrollY);
    mix ((juce::uint64) getWidth()); mix ((juce::uint64) getHeight()); mix ((juce::uint64) selectedTake); mix (colourWaves ? 19u : 23u);
    mix ((juce::uint64) (juce::pointer_sized_uint) layout.get());
    mix ((juce::uint64) selectedFlag.hashCode64()); mix ((juce::uint64) selectedNote.hashCode64()); mixD (selA); mixD (selB); mix ((juce::uint64) selTracks.size());
    if (model != nullptr)
    {
        mix ((juce::uint64) model->peaks.changeCount()); mix ((juce::uint64) model->peaks.addCount());
        for (auto& f : model->flags) { mix ((juce::uint64) f.id.hashCode64()); mix ((juce::uint64) f.note.hashCode64()); mix ((juce::uint64) f.replies.size()); mix (f.removed ? 1u : 2u); mix ((juce::uint64) f.sample); }
    }
    for (auto& m : projectFlags) { mixD (m.seconds); mix ((juce::uint64) m.note.hashCode64()); mix ((juce::uint64) m.replies.size()); }
    for (auto& n : notes) { mixD (n.seconds); mix ((juce::uint64) n.text.hashCode64()); }
    if (layout != nullptr)
        for (auto& t : layout->tracks)
        {
            if (isMuteSolo) { mix (isMuteSolo (t.id, false) ? 3u : 5u); mix (isMuteSolo (t.id, true) ? 7u : 11u); }
            if (showArm && isArmed) mix (isArmed (t.id) ? 13u : 17u);
        }
    updateBars();
    const auto now = juce::Time::getMillisecondCounter();
    if (h == lastSignature && now - lastPaintMs < 500) return;
    lastSignature = h; lastPaintMs = now;
    repaint();
}

WaveThumb* LiveTakeView::thumbFor (const juce::File& f)
{
    const auto key = f.getFullPathName();
    auto it = thumbs.find (key);
    if (it != thumbs.end()) return it->second.get();
    if (! f.existsAsFile()) return nullptr;
    auto t = std::make_unique<WaveThumb> (formats, pool);
    t->load (f);
    auto* p = t.get();
    thumbs[key] = std::move (t);
    return p;
}

std::vector<LiveTakeView::FlagShown> LiveTakeView::flagsShown() const
{
    std::vector<FlagShown> out;
    std::set<juce::String> have;
    if (model != nullptr && layout != nullptr)
        for (auto& f : model->flags)
        {
            if (f.isNote) continue;                                         // (0.5.0: a note is drawn in the notes row, by the window's list)
            if (f.removed) { have.insert (f.id); continue; }
            const double s = layout->windowSecondsOf (f.sample);
            if (s < 0.0) continue;                                          // (pinned between takes: not on this window's timeline)
            out.push_back ({ f.id, s, f.note, f.by, f.replies, false, f.pinnedAt });
            have.insert (f.id);
        }
    for (auto& m : projectFlags)                                            // flags already in the project (from an earlier session, or put in)
        if (have.count (m.id.toString()) == 0)
        {
            FlagShown f; f.id = m.id.toString(); f.seconds = layout != nullptr ? layout->shownOfWindow (m.seconds) : m.seconds; f.note = m.note; f.pinnedAt = m.pinnedAt; f.by = m.by;
            for (auto& r : m.replies) f.replies.push_back ({ r.text, r.by, r.at });
            out.push_back (f);
        }
    std::sort (out.begin(), out.end(), [] (const FlagShown& a, const FlagShown& b) { return a.seconds < b.seconds; });
    return out;
}

std::vector<juce::Uuid> LiveTakeView::tracksIn (const ShownTake& t) const
{
    std::vector<juce::Uuid> v;
    if (layout == nullptr) return v;
    for (auto& tr : layout->tracks)
    {
        bool has = t.session && tr.sessionIndex >= 0;
        if (! has) for (auto& f : t.files) if (f.trackId == tr.id) has = true;
        if (has) v.push_back (tr.id);
    }
    return v;
}

juce::Range<double> LiveTakeView::contentRange() const
{
    double a = 0.0, b = 30.0;
    if (layout != nullptr && ! layout->takes.empty())
    {
        a = layout->firstSeconds(); b = layout->endSeconds();
        if (! layout->timeOfDay) a = 0.0;
    }
    else if (nowLine >= 0.0) { a = nowLine; b = nowLine; }
    const bool tod = layout != nullptr && layout->timeOfDay;
    for (double x : { liveEnd, nowLine, playhead, flagPoint })
        if (x >= 0.0)
        {
            b = juce::jmax (b, x);
            if (! tod || x == liveEnd || x == nowLine) a = juce::jmin (a, x);    // (the clock starts at the first take - a playhead left before it does not reach back to midnight)
        }
    if (liveEnd >= 0.0) b = juce::jmax (b, liveEnd + juce::jmax (60.0, (liveEnd - a) * 0.25));    // (while recording: room ahead, so Fit does not page at once)
    const double room = juce::jmax (20.0, (b - a) * 0.04);
    a = layout != nullptr && layout->timeOfDay ? juce::jmax (0.0, a - room) : 0.0;
    return { a, b + room };
}

void LiveTakeView::updateBars()
{
    if (updatingBars) return;
    const juce::ScopedValueSetter<bool> busy (updatingBars, true);
    const auto r = contentRange();
    const double vis = visibleSeconds();
    hbar.setRangeLimits (juce::jmin (r.getStart(), viewStart), juce::jmax (r.getEnd(), viewStart + vis), juce::dontSendNotification);
    hbar.setCurrentRange (viewStart, vis, juce::dontSendNotification);
    hbar.setSingleStepSize (vis * 0.05);
    const int full = layout != nullptr ? (int) layout->tracks.size() * laneH : 0, room = juce::jmax (1, lanesBottom() - lanesTop());
    scrollY = juce::jlimit (0, juce::jmax (0, full - room), scrollY);
    vbar.setRangeLimits (0.0, (double) juce::jmax (full, room), juce::dontSendNotification);
    vbar.setCurrentRange ((double) scrollY, (double) room, juce::dontSendNotification);
    vbar.setSingleStepSize ((double) laneH);
}

void LiveTakeView::scrollBarMoved (juce::ScrollBar* bar, double newStart)
{
    if (updatingBars) return;
    if (bar == &hbar) viewStart = newStart;
    else scrollY = (int) std::lround (newStart);
    if (onUserScrolled) onUserScrolled();
    repaint();
}

void LiveTakeView::resized()
{
    hbar.setBounds (kHeaderW, getHeight() - kBarH, juce::jmax (0, getWidth() - kHeaderW - kBarH), kBarH);
    vbar.setBounds (getWidth() - kBarH, lanesTop(), kBarH, juce::jmax (0, getHeight() - lanesTop() - kBarH));
    updateBars();
}

void LiveTakeView::keepInView (double seconds, bool page)
{
    const double width = visibleSeconds();
    if (page)
    {
        if (seconds > viewStart + width * 0.92) viewStart = seconds - width * 0.15;
        else if (seconds < viewStart) viewStart = juce::jmax (0.0, seconds - width * 0.1);
    }
    else if (seconds < viewStart || seconds > viewStart + width) viewStart = juce::jmax (0.0, seconds - width * 0.5);
}

void LiveTakeView::centreOn (double seconds)
{
    viewStart = juce::jmax (0.0, seconds - visibleSeconds() * 0.5);
    updateBars();
    repaint();
}

void LiveTakeView::zoomBy (double factor, double around)
{
    const double x = (around - viewStart) * pxPerSec;
    pxPerSec = juce::jlimit (0.0005, 4000.0, pxPerSec * factor);
    viewStart = juce::jmax (0.0, around - x / pxPerSec);
    updateBars();
    repaint();
}

void LiveTakeView::zoomCentred (double factor, double anchor)
{
    const double vis = visibleSeconds();
    if (anchor < viewStart || anchor > viewStart + vis) anchor = viewStart + vis * 0.5;      // (off the screen: round the middle of what is shown)
    const auto range = contentRange();
    const double visPx = (double) juce::jmax (100, getWidth() - kHeaderW - kBarH);
    const double outmost = juce::jmin (12.0, visPx / juce::jmax (1.0, range.getLength()));     // (out as far as everything fits)
    pxPerSec = juce::jlimit (juce::jmax (0.0005, outmost), 4000.0, pxPerSec * factor);
    const double low = layout != nullptr && layout->timeOfDay ? juce::jmin (range.getStart(), anchor) : 0.0;
    viewStart = juce::jmax (low, anchor - visibleSeconds() * 0.5);
    updateBars();
    repaint();
}

void LiveTakeView::zoomToFit()
{
    const auto r = contentRange();
    const double a = layout != nullptr && layout->timeOfDay ? r.getStart() : 0.0;
    const double len = juce::jmax (30.0, r.getEnd() - a);
    pxPerSec = juce::jlimit (0.0005, 4000.0, (double) juce::jmax (100, getWidth() - kHeaderW - kBarH - 20) / len);
    viewStart = a;
    updateBars();
    repaint();
}

void LiveTakeView::changeLaneHeight (int direction)
{
    laneH = juce::jlimit (22, 200, laneH + (direction > 0 ? 8 : -8));
    updateBars();
    repaint();
}

int LiveTakeView::trackRowAt (int y) const
{
    if (layout == nullptr || y < lanesTop() || y >= lanesBottom()) return -1;
    const int row = (y - lanesTop() + scrollY) / juce::jmax (8, laneH);
    return juce::isPositiveAndBelow (row, (int) layout->tracks.size()) ? row : -1;
}

juce::Rectangle<int> LiveTakeView::headerButton (int row, int which) const
{
    const int y = lanesTop() + row * laneH - scrollY;
    const int bh = juce::jmin (18, laneH / 2 - 2), top = y + 3;
    if (which == 0) return { 8, top, 18, bh };
    if (which == 1) return { 28, top, 18, bh };
    return { kHeaderW - 44, top, 38, bh };
}

const LiveTakeView::NoteShown* LiveTakeView::noteAt (int x) const
{
    const NoteShown* hit = nullptr;
    for (auto& n : notes) { const int nx = xOf (n.seconds); if (x >= nx - 4 && x <= nx + (n.plain ? 12 : 160)) hit = &n; }
    return hit;
}

void LiveTakeView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    b.removeFromBottom (kBarH);
    g.fillAll (theme::wave);
    const auto shown = flagsShown();
    auto top = b.removeFromTop (kNoteH);
    g.setColour (theme::ruler.darker (0.2f)); g.fillRect (top);
    auto flagRow = b.removeFromTop (kFlagH);
    g.setColour (theme::ruler.darker (0.35f)); g.fillRect (flagRow);
    auto ruler = b.removeFromTop (kRulerH);
    paintRuler (g, ruler);
    auto heads = b.removeFromTop (kTakeHeadH);
    g.setColour (theme::wave.darker (0.3f)); g.fillRect (heads);
    // the take headers ("001 - Symphony No. 5")
    if (layout != nullptr)
    {
        g.saveState(); g.reduceClipRegion (heads.withTrimmedLeft (kHeaderW));
        for (auto& t : layout->takes)
        {
            const double end = t.recording && liveEnd >= 0.0 ? juce::jmax (t.end(), liveEnd) : t.shownEnd();
            const int x0 = xOf (t.start), x1 = juce::jmax (x0 + 2, xOf (end));
            if (x1 < kHeaderW || x0 > getWidth()) continue;
            auto r = juce::Rectangle<int> (x0, heads.getY() + 1, x1 - x0, heads.getHeight() - 2);
            const bool chosen = selectedTake != 0 && t.number == selectedTake;
            g.setColour (t.recording ? juce::Colour (0xffc62828) : chosen ? theme::accent.brighter (0.45f) : theme::accent); theme::fillBox (g, r);     // (fermata 3.73.0's corners)
            if (chosen) { g.setColour (juce::Colours::white); theme::drawBox (g, r); }
            g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            auto title = t.title (layout->windowName);
            if (t.recording) title << "  (recording)";
            else if (! t.inProject && t.session) title << (t.merged ? "  (merged)" : "  (being merged)");
            g.drawText (title, r.withTrimmedLeft (5).withX (juce::jmax (r.getX() + 5, kHeaderW + 4)), juce::Justification::centredLeft, true);
        }
        g.restoreState();
    }
    paintLanes (g, b);
    paintTrackHeaders (g, b.withWidth (kHeaderW));
    // the strips' captions (in the header column)
    g.setColour (theme::panel); g.fillRect (0, 0, kHeaderW, lanesTop());
    g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("NOTES", 8, 0, kHeaderW - 10, kNoteH, juce::Justification::centredLeft);
    g.drawText (flagPoint >= 0.0 ? "FLAGS  (flag point set)" : "FLAGS", 8, kNoteH, kHeaderW - 10, kFlagH, juce::Justification::centredLeft);
    g.drawText (layout != nullptr && layout->timeOfDay ? "TIME OF DAY" : "TIME", 8, kNoteH + kFlagH, kHeaderW - 10, kRulerH, juce::Justification::centredLeft);
    g.drawText ("TAKES", 8, kNoteH + kFlagH + kRulerH, kHeaderW - 10, kTakeHeadH, juce::Justification::centredLeft);
    // notes, flags (over the lanes: a faint line down from each)
    g.saveState(); g.reduceClipRegion (getLocalBounds().withTrimmedLeft (kHeaderW).withTrimmedBottom (kBarH));
    paintNotes (g, top);
    paintFlags (g, flagRow, shown);
    for (int k = 0; k < 2; ++k)                                             // marks 1 and 2 (blue, at the top of the flag band, with a line down)
    {
        const double at = k == 0 ? mark1 : mark2;
        if (at < 0.0) continue;
        const int x = xOf (at);
        g.setColour (juce::Colour (0xff4aa3ff).withAlpha (0.55f)); g.fillRect (x, kNoteH, 1, lanesBottom() - kNoteH);
        auto f = juce::Rectangle<int> (x - (k == 0 ? 0 : 16), kNoteH + kFlagH - 15, 16, 14);
        g.setColour (juce::Colour (0xff4aa3ff)); g.fillRect (f);
        g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (k == 0 ? "1" : "2", f, juce::Justification::centred, false);
    }
    // the edge of what is on the disk (the Fixer), the record line, now, the flag point, the playhead
    if (writtenEnd >= 0.0 && liveEnd > writtenEnd)
    {
        const int x = xOf (writtenEnd);
        g.setColour (juce::Colour (0xffffc94d).withAlpha (0.85f)); g.fillRect (x, kNoteH + kFlagH, 2, lanesBottom() - kNoteH - kFlagH);
    }
    if (liveEnd >= 0.0)
    {
        const int x = xOf (liveEnd);
        g.setColour (juce::Colour (0xffff3b30)); g.fillRect (x, kNoteH + kFlagH, 2, lanesBottom() - kNoteH - kFlagH);
    }
    else if (nowLine >= 0.0)
    {
        const int x = xOf (nowLine);
        g.setColour (nowBlue().withAlpha (0.75f)); g.fillRect (x, kNoteH + kFlagH, 1, lanesBottom() - kNoteH - kFlagH);
        g.setColour (nowBlue()); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("NOW", x + 3, kNoteH + kFlagH + kRulerH, 40, kTakeHeadH, juce::Justification::centredLeft, false);
    }
    if (flagPoint >= 0.0)
    {
        const int x = xOf (flagPoint);
        g.setColour (pointYellow().withAlpha (0.45f));
        for (int y = kNoteH + kFlagH; y < lanesBottom(); y += 8) g.fillRect (x, y, 1, 4);
        juce::Path p; p.addTriangle ((float) x - 7.0f, (float) kNoteH + 1.0f, (float) x + 7.0f, (float) kNoteH + 1.0f, (float) x, (float) kNoteH + 11.0f);
        g.setColour (pointYellow()); g.fillPath (p); g.fillRect (x, kNoteH + 8, 1, kFlagH - 8);
    }
    if (playhead >= 0.0) theme::paintPlayhead (g, xOf (playhead), kNoteH + kFlagH, lanesBottom() - kNoteH - kFlagH);
    g.restoreState();
    g.setColour (theme::border); g.fillRect (kHeaderW - 1, 0, 1, getHeight());
    g.setColour (theme::panel); g.fillRect (0, getHeight() - kBarH, kHeaderW, kBarH);
}

void LiveTakeView::paintRuler (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::ruler); g.fillRect (r);
    g.setColour (theme::grid); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    static const double steps[] { 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600 };
    double step = 21600;
    const bool tod = layout != nullptr && layout->timeOfDay;
    const double wantPx = tod ? 80.0 : 70.0;
    for (auto s : steps) if (s * pxPerSec >= wantPx) { step = s; break; }
    g.setFont (juce::FontOptions (11.0f));
    const double t0 = std::floor (viewStart / step) * step;
    const int right = getWidth() - kBarH;
    for (double t = t0; xOf (t) < right; t += step)
    {
        const int x = xOf (t);
        if (x < kHeaderW) continue;
        g.setColour (theme::grid.brighter (0.3f)); g.fillRect (x, r.getY() + r.getHeight() / 2, 1, r.getHeight() / 2);
        g.setColour (theme::dimText);
        juce::String txt;
        if (tod)
        {
            const juce::Time clock (layout->originMs + (juce::int64) std::llround (t * 1000.0));
            txt = step >= 60.0 ? clock.formatted ("%H:%M") : clock.formatted ("%H:%M:%S");
            if (step < 1.0) txt << "." << juce::String ((int) std::lround ((t - std::floor (t)) * 10.0) % 10);
        }
        else
        {
            const int secs = (int) std::floor (t + 1.0e-6);
            txt = secs >= 3600 ? juce::String (secs / 3600) + ":" + juce::String ((secs / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                               : juce::String (secs / 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
            if (step < 1.0) txt << "." << juce::String ((int) std::lround ((t - std::floor (t)) * 10.0));
        }
        g.drawText (txt, x + 3, r.getY(), 80, r.getHeight() / 2 + 3, juce::Justification::centredLeft, false);
    }
    // the time of day: the date at each midnight (DD/MM/YYYY), once a day, with a line down the lanes
    if (tod)
    {
        const juce::int64 firstMs = layout->originMs + (juce::int64) std::llround (viewStart * 1000.0);
        const juce::int64 lastMs = layout->originMs + (juce::int64) std::llround ((viewStart + visibleSeconds()) * 1000.0);
        juce::int64 mid = TakeLayout::midnightOf (firstMs);
        if (mid < firstMs) mid = TakeLayout::midnightOf (mid + 30 * 3600 * 1000LL);
        for (int guard = 0; mid <= lastMs && guard < 400; ++guard)
        {
            const double t = (double) (mid - layout->originMs) / 1000.0;
            const int x = xOf (t);
            if (x >= kHeaderW && x < right)
            {
                g.setColour (pointYellow().withAlpha (0.35f)); g.fillRect (x, r.getBottom(), 1, lanesBottom() - r.getBottom());
                g.setColour (pointYellow()); g.fillRect (x, r.getY(), 2, r.getHeight());
                const auto date = juce::Time (mid).formatted ("%d/%m/%Y");
                auto box = juce::Rectangle<int> (x + 3, r.getY() + 1, 78, r.getHeight() - 2);
                g.setColour (theme::ruler.darker (0.4f)); g.fillRect (box);
                g.setColour (pointYellow()); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                g.drawText (date, box, juce::Justification::centred, false);
                g.setFont (juce::FontOptions (11.0f));
            }
            mid = TakeLayout::midnightOf (mid + 30 * 3600 * 1000LL);
        }
    }
}

void LiveTakeView::paintNotes (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto sorted = notes;
    std::sort (sorted.begin(), sorted.end(), [] (const NoteShown& a, const NoteShown& b) { return a.seconds < b.seconds; });
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const auto& n = sorted[i];
        const int x = xOf (n.seconds);
        if (x < kHeaderW - 200 || x > getWidth()) continue;
        if (n.plain)                                                          // an ordinary marker of the project (a gap ...): grey, its name on hover
        {
            g.setColour (juce::Colour (0xffa0a8b4).withAlpha (0.35f)); g.fillRect (x, r.getBottom(), 1, lanesBottom() - r.getBottom());
            g.setColour (juce::Colour (0xffa0a8b4)); g.fillRect (x, r.getY() + 3, 2, r.getHeight() - 6);
            continue;
        }
        const bool lit = n.id == selectedNote;
        const int nextX = i + 1 < sorted.size() ? xOf (sorted[i + 1].seconds) : getWidth() + 400;
        g.setColour (noteGold().withAlpha (0.30f)); g.fillRect (x, r.getBottom(), 1, lanesBottom() - r.getBottom());
        auto icon = juce::Rectangle<int> (x, r.getY() + 3, 12, r.getHeight() - 6);
        g.setColour (lit ? juce::Colours::white : noteGold()); g.fillRoundedRectangle (icon.toFloat(), 2.0f);
        g.setColour (juce::Colour (0xff3a2a00));
        for (int ly = icon.getY() + 4; ly < icon.getBottom() - 2; ly += 3) g.fillRect (icon.getX() + 2, ly, 8, 1);
        const int w = juce::jmax (0, juce::jmin (260, nextX - x - 18));
        if (w < 24) continue;
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (lit ? juce::Colours::white : juce::Colour (0xfffff0c0));
        const auto first = n.text.trim().upToFirstOccurrenceOf ("\n", false, false);
        g.drawText (first.isNotEmpty() ? first : juce::String ("(empty note)"), x + 15, r.getY(), w, r.getHeight(), juce::Justification::centredLeft, true);
    }
}

void LiveTakeView::paintFlags (juce::Graphics& g, juce::Rectangle<int> r, const std::vector<FlagShown>& list)
{
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& f = list[i];
        const int x = xOf (f.seconds);
        const int nextX = i + 1 < list.size() ? xOf (list[i + 1].seconds) : getWidth() + 400;
        if (x < kHeaderW - 200 || x > getWidth()) continue;
        const bool lit = f.id == selectedFlag;
        // the pole, the flag, and a faint line down the lanes
        g.setColour (flagRed().withAlpha (0.35f)); g.fillRect (x, r.getBottom(), 1, lanesBottom() - r.getBottom());
        g.setColour (lit ? juce::Colours::white : flagRed()); g.fillRect (x, r.getY() + 2, 2, r.getHeight() - 4);
        juce::Path p; p.addTriangle ((float) x + 2.0f, (float) r.getY() + 3.0f, (float) x + 13.0f, (float) r.getY() + 8.0f, (float) x + 2.0f, (float) r.getY() + 13.0f);
        g.fillPath (p);
        // the note and the last reply, as far as the next flag
        const int w = juce::jmax (0, juce::jmin (320, nextX - x - 18));
        if (w < 24) continue;
        auto txt = juce::Rectangle<int> (x + 15, r.getY() + 1, w, r.getHeight() / 2);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (lit ? juce::Colours::white : juce::Colour (0xffffd9cf));
        g.drawText (f.note.isNotEmpty() ? f.note : juce::String ("(no note yet)"), txt, juce::Justification::centredLeft, true);
        if (! f.replies.empty())
        {
            g.setFont (juce::FontOptions (11.5f));
            g.setColour (replyGreen());
            const auto& rep = f.replies.back();
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb3 ")) + rep.by + ": " + rep.text + (f.replies.size() > 1 ? "  (" + juce::String ((int) f.replies.size()) + ")" : juce::String()),
                        txt.translated (0, r.getHeight() / 2 - 1), juce::Justification::centredLeft, true);
        }
    }
}

void LiveTakeView::paintLanes (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (layout == nullptr) return;
    g.saveState();
    g.reduceClipRegion (area.withTrimmedRight (kBarH));
    const int x0 = kHeaderW, x1 = getWidth() - kBarH;
    for (size_t row = 0; row < layout->tracks.size(); ++row)
    {
        const int y = lanesTop() + (int) row * laneH - scrollY;
        if (y > getHeight() || y + laneH < area.getY()) continue;
        auto lane = juce::Rectangle<int> (x0, y, x1 - x0, laneH);
        g.setColour (row % 2 == 0 ? theme::wave : theme::waveAlt); g.fillRect (lane);
        g.setColour (theme::grid.withAlpha (0.5f)); g.fillRect (lane.getX(), lane.getBottom() - 1, lane.getWidth(), 1);
        const auto& tr = layout->tracks[row];
        const auto colour = colourOf ? colourOf (tr.id) : juce::Colour (0xff3f7fc4);
        for (auto& t : layout->takes)
        {
            double end = t.shownEnd();
            if (t.recording && liveEnd >= 0.0) end = juce::jmax (end, liveEnd);
            int cx0 = xOf (t.start), cx1 = xOf (end);
            if (cx1 < x0 || cx0 > x1) continue;
            auto clip = juce::Rectangle<int> (cx0, y + 1, juce::jmax (1, cx1 - cx0), laneH - 3);
            g.setColour (chan::clipFill (colour)); theme::fillBox (g, clip);
            int wx1 = juce::jmin (x1, cx1);
            if (writtenEnd >= 0.0 && t.recording) wx1 = juce::jmin (wx1, xOf (writtenEnd));     // (the Fixer: only what is on the disk)
            paintWave (g, t, tr, clip, juce::jmax (x0, cx0), wx1, chan::waveColour (colour));
            if (writtenEnd >= 0.0 && t.recording && xOf (writtenEnd) < cx1)                     // the part still on its way to the disk
            {
                auto rest = clip.withLeft (juce::jmax (x0, xOf (writtenEnd)));
                g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (rest);
                g.setColour (juce::Colour (0xffffc94d).withAlpha (0.18f));
                for (int hx = rest.getX() - rest.getHeight(); hx < rest.getRight(); hx += 9)
                    g.drawLine ((float) hx, (float) rest.getBottom(), (float) (hx + rest.getHeight()), (float) rest.getY(), 1.0f);
            }
            g.setColour (chan::clipFill (colour).brighter (0.35f)); theme::drawBox (g, clip);
        }
        // the selection (the Fixer's ReTouch, the Primary's loop and marks)
        if (selB > selA && std::find (selTracks.begin(), selTracks.end(), tr.id) != selTracks.end())
        {
            const int sx0 = xOf (selA), sx1 = xOf (selB);
            g.setColour (juce::Colours::white.withAlpha (0.16f)); theme::fillBox (g, juce::Rectangle<int> (sx0, y, juce::jmax (1, sx1 - sx0), laneH));
            g.setColour (juce::Colours::white.withAlpha (0.55f)); theme::drawBox (g, juce::Rectangle<int> (sx0, y, juce::jmax (1, sx1 - sx0), laneH));
        }
    }
    g.restoreState();
}

void LiveTakeView::paintWave (juce::Graphics& g, const ShownTake& t, const LayoutTrack& tr, juce::Rectangle<int> lane, int x0, int x1, juce::Colour colour)
{
    if (x1 <= x0) return;
    const int chans = juce::jlimit (1, 2, tr.channels);                       // (a stereo track in two halves; a wider one shows its first two)
    const bool usePeaks = t.session && tr.ringFirst >= 0 && model != nullptr && model->peaks.channels() > 0;
    WaveThumb* th = nullptr;
    juce::File file;
    if (! usePeaks)
        for (auto& f : t.files) if (f.trackId == tr.id) { th = thumbFor (f.file); file = f.file; break; }
    if (! usePeaks && th == nullptr) return;
    const double rate = t.rate > 0 ? t.rate : 48000.0;
    const int n = x1 - x0;
    std::vector<float> tops ((size_t) n), bottoms ((size_t) n);
    // 0.5.0: the coloured waveform - a colour for every column (the session's chunks while it records, the file's once it is in the project)
    std::vector<juce::Colour> cols;
    bool coloured = false;
    if (colourWaves)
    {
        const double a0 = secondsAt (x0) - t.start;
        if (usePeaks && sessionColours != nullptr && tr.sessionIndex >= 0)
            coloured = sessionColours->colours (tr.sessionIndex, t.sessionStart + (juce::int64) std::floor (a0 * rate), rate / pxPerSec, n, cols, colour);
        else if (fileColours != nullptr && file != juce::File())
            if (auto wc = fileColours->get (file))
            {
                cols.resize ((size_t) n);
                for (int i = 0; i < n; ++i) { const double a = a0 + (double) i / pxPerSec; cols[(size_t) i] = wc->colourFor (a, a + 1.0 / pxPerSec); }
                coloured = true;
            }
    }
    setWaveColour (g, colour);
    for (int c = 0; c < chans; ++c)
    {
        const float bandH = (float) lane.getHeight() / (float) chans;
        const float mid = (float) lane.getY() + bandH * ((float) c + 0.5f);
        const float half = bandH * 0.5f - 1.0f;
        bool any = false;
        for (int i = 0; i < n; ++i)
        {
            const double a = secondsAt (x0 + i) - t.start, b2 = secondsAt (x0 + i + 1) - t.start;
            float lo = 0.0f, hi = 0.0f;
            bool ok = false;
            if (usePeaks)
            {
                const juce::int64 s0 = t.sessionStart + (juce::int64) std::floor (a * rate), s1 = t.sessionStart + (juce::int64) std::ceil (b2 * rate);
                ok = model->peaks.range (tr.ringFirst + juce::jmin (c, tr.channels - 1), juce::jmax (t.sessionStart, s0), juce::jmax (s0 + 1, s1), lo, hi);
            }
            else if (th != nullptr)
            {
                th->getApproximateMinMax (juce::jmax (0.0, a), juce::jmax (0.0, b2), juce::jmin (c, th->getNumChannels() - 1), lo, hi);
                ok = true;
            }
            if (! ok) { lo = hi = 0.0f; }
            any = any || ok;
            lo = juce::jlimit (-1.0f, 1.0f, lo * vzoom); hi = juce::jlimit (-1.0f, 1.0f, hi * vzoom);
            tops[(size_t) i] = mid - hi * half; bottoms[(size_t) i] = mid - lo * half;
        }
        if (any) fillEnvelope (g, (float) x0, tops, bottoms, coloured ? &cols : nullptr, bandH);
    }
}

void LiveTakeView::paintTrackHeaders (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::panel); g.fillRect (area);
    if (layout == nullptr) return;
    g.saveState(); g.reduceClipRegion (area);
    for (size_t row = 0; row < layout->tracks.size(); ++row)
    {
        const int y = lanesTop() + (int) row * laneH - scrollY;
        if (y > getHeight() || y + laneH < area.getY()) continue;
        const auto& tr = layout->tracks[row];
        const auto colour = colourOf ? colourOf (tr.id) : juce::Colour (0xff3f7fc4);
        auto r = juce::Rectangle<int> (0, y, kHeaderW, laneH);
        g.setColour (row % 2 == 0 ? theme::row : theme::rowAlt); g.fillRect (r);
        g.setColour (colour); g.fillRect (r.getX(), r.getY(), 4, r.getHeight() - 1);
        g.setColour (theme::grid.withAlpha (0.5f)); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
        auto key = [&] (juce::Rectangle<int> k, const juce::String& txt, bool on, juce::Colour lit)
        {
            g.setColour (on ? lit : theme::button); g.fillRoundedRectangle (k.toFloat(), 3.0f);
            g.setColour (theme::border); g.drawRoundedRectangle (k.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (on ? juce::Colours::white : theme::inkOn (theme::button)); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));     // (0.9.0: the look's own ink - dark on the desk's light keys, gilt on the Manuscript UI's navy ones, where the dark letters could not be seen)
            g.drawText (txt, k, juce::Justification::centred, false);
        };
        if (isMuteSolo)
        {
            key (headerButton ((int) row, 0), "M", isMuteSolo (tr.id, false), juce::Colour (0xffe0a040));
            key (headerButton ((int) row, 1), "S", isMuteSolo (tr.id, true), juce::Colour (0xff2e9e4f));
        }
        if (showArm && isArmed) key (headerButton ((int) row, 2), "ARM", isArmed (tr.id), juce::Colour (0xffd32f2f));
        g.setColour (theme::text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        const int nameX = isMuteSolo ? 50 : 10;
        g.drawFittedText (tr.name, nameX, y + 2, kHeaderW - nameX - (showArm ? 48 : 6), juce::jmin (laneH - 4, 22), juce::Justification::centredLeft, 1, 0.7f);
        if (laneH >= 34)
        {
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f));
            juce::String sub = tr.channels == 1 ? "mono" : tr.channels == 2 ? "stereo" : juce::String (tr.channels) + " ch";
            if (tr.sessionIndex < 0 && model != nullptr && model->sessionOn) sub << " - not recording";
            g.drawText (sub, nameX, y + laneH - 16, kHeaderW - nameX - 6, 14, juce::Justification::centredLeft, true);
        }
    }
    g.restoreState();
}

void LiveTakeView::mouseDown (const juce::MouseEvent& e)
{
    if (e.x >= getWidth() - kBarH || e.y >= getHeight() - kBarH) return;
    if (e.y < kNoteH && e.x >= kHeaderW)                                    // the notes row: a note opens in the window's note panel
    {
        if (auto* n = noteAt (e.x); n != nullptr && ! n->plain && onNoteClick) onNoteClick (n->id);
        return;
    }
    if (e.y >= kNoteH && e.y < kNoteH + kFlagH && e.x >= kHeaderW)
    {
        // the nearest flag to the left of the click (within its note); where there is none, the flag point
        const auto shown = flagsShown();
        const FlagShown* hit = nullptr;
        for (auto& f : shown) { const int x = xOf (f.seconds); if (e.x >= x - 4 && e.x <= x + 220) hit = &f; }
        if (hit != nullptr && ! e.mods.isAltDown()) { if (onFlagClick) onFlagClick (hit->id, e.getNumberOfClicks() >= 2); return; }
        if (onFlagBandClick) onFlagBandClick (juce::jmax (0.0, secondsAt (e.x)));
        return;
    }
    if (e.y >= kNoteH + kFlagH + kRulerH && e.y < lanesTop() && e.x >= kHeaderW)   // a take's header: the take is chosen (P puts marks 1 and 2 round it)
    {
        int number = 0;
        if (layout != nullptr) if (auto* t = layout->takeAt (secondsAt (e.x))) number = t->number;
        selectedTake = number;
        if (onTakeClick) onTakeClick (number);
        repaint();
        return;
    }
    if (e.x < kHeaderW && layout != nullptr)
    {
        const int row = trackRowAt (e.y);
        if (row < 0) return;
        const auto id = layout->tracks[(size_t) row].id;
        if (isMuteSolo && headerButton (row, 0).contains (e.getPosition())) { if (toggleMuteSolo) toggleMuteSolo (id, false); repaint(); return; }
        if (isMuteSolo && headerButton (row, 1).contains (e.getPosition())) { if (toggleMuteSolo) toggleMuteSolo (id, true); repaint(); return; }
        if (showArm && headerButton (row, 2).contains (e.getPosition())) { if (toggleArm) toggleArm (id); repaint(); return; }
        return;
    }
    if (e.y >= kNoteH + kFlagH && e.x >= kHeaderW)
    {
        const double s = juce::jmax (0.0, secondsAt (e.x));
        if (onLaneClick) onLaneClick (s);
        if (! e.mods.isShiftDown() && (! canSeek || canSeek()) && onSeek) onSeek (s);
        selecting = true; dragFrom = s; dragRow0 = juce::jmax (0, trackRowAt (e.y)); dragAlt = e.mods.isAltDown();
        if (selectedTake != 0 && ! e.mods.isShiftDown()) { selectedTake = 0; if (onTakeClick) onTakeClick (0); }
        if (selA >= 0.0 && onSelect && ! e.mods.isShiftDown()) { selA = selB = -1.0; selTracks.clear(); onSelect (0.0, 0.0, {}); }
        repaint();
    }
}

void LiveTakeView::mouseDrag (const juce::MouseEvent& e)
{
    if (! selecting || layout == nullptr || ! onSelect) return;
    if (std::abs (e.getDistanceFromDragStartX()) < 4) return;
    const double s = juce::jmax (0.0, secondsAt (e.x));
    selA = juce::jmin (dragFrom, s); selB = juce::jmax (dragFrom, s);
    selTracks.clear();
    if (allTracksByDefault && ! (dragAlt || e.mods.isAltDown()))
    {   // 0.5.0, the Fixer: every track recorded in the take the drag started in (Alt + drag: only the tracks dragged over)
        if (auto* t = layout->takeAt (dragFrom)) selTracks = tracksIn (*t);
    }
    else
    {
        int r1 = trackRowAt (juce::jlimit (lanesTop(), lanesBottom() - 1, e.y));
        if (r1 < 0) r1 = (int) layout->tracks.size() - 1;
        for (int r = juce::jmin (dragRow0, r1); r <= juce::jmax (dragRow0, r1); ++r)
            if (juce::isPositiveAndBelow (r, (int) layout->tracks.size())) selTracks.push_back (layout->tracks[(size_t) r].id);
    }
    repaint();
}

void LiveTakeView::mouseUp (const juce::MouseEvent&)
{
    if (selecting && selB > selA && onSelect) onSelect (selA, selB, selTracks);
    selecting = false;
}

void LiveTakeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) { zoomBy (w.deltaY > 0 ? 1.25 : 0.8, secondsAt (e.x)); return; }
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 200.0 / pxPerSec;
        viewStart = juce::jmax (0.0, viewStart - d);
    }
    else if (layout != nullptr)
    {
        const int full = (int) layout->tracks.size() * laneH, room = lanesBottom() - lanesTop();
        scrollY = juce::jlimit (0, juce::jmax (0, full - room), scrollY - (int) std::lround (w.deltaY * 120.0));
    }
    if (onUserScrolled && (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))) onUserScrolled();
    updateBars();
    repaint();
}

void LiveTakeView::mouseMove (const juce::MouseEvent& e)
{
    juce::String t;
    if (e.y < kNoteH && e.x >= kHeaderW)
    {
        if (auto* n = noteAt (e.x))
        {
            if (n->plain) t = n->text;
            else
            {
                t = "Note" + (n->by.isNotEmpty() ? " by " + n->by : juce::String()) + (n->at.toMilliseconds() != 0 ? " (" + n->at.formatted ("%H:%M:%S") + ")" : juce::String()) + ":\n" + n->text;
                t << "\n\nClick to open it.";
            }
        }
    }
    else if (e.y >= kNoteH && e.y < kNoteH + kFlagH && e.x >= kHeaderW)
    {
        for (auto& f : flagsShown())
        {
            const int x = xOf (f.seconds);
            if (e.x >= x - 4 && e.x <= x + 220)
            {
                t = "Flag" + (f.by.isNotEmpty() ? " by " + f.by : juce::String()) + (f.pinnedAt.toMilliseconds() != 0 ? " pinned at " + f.pinnedAt.formatted ("%H:%M:%S") : juce::String()) + ": " + (f.note.isNotEmpty() ? f.note : juce::String ("(no note)"));
                for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
            }
        }
        if (t.isEmpty()) t = "Click here to choose a flag point: Enter (a flag) or N (a note) then pins there, for a moment that has passed. A click on the tracks goes back to NOW.";
    }
    if (t != hoverText) { hoverText = t; }
}

juce::String LiveTakeView::getTooltip() { return hoverText; }
} // namespace td::live
