#include "LiveTakeView.h"
#include "../core/AudioFormats.h"
#include <set>

namespace td::live
{
static juce::Colour flagRed()   { return juce::Colour (0xffff5a3c); }
static juce::Colour replyGreen() { return juce::Colour (0xff7ee08a); }

LiveTakeView::LiveTakeView()
{
    registerAudioFormats (formats);
    markerMouse = std::make_unique<markerui::Controller> (*this, kHeaderW,
        [this] { return editableMarkers ? markersNow() : nullptr; },
        [this] (int x) { return secondsAt (x); }, [this] (double s) { return xOf (s); },
        [this] { if (onMarkersChanged) onMarkersChanged(); });
    setWantsKeyboardFocus (false);
    setOpaque (true);                                                       // (paint() covers all of it: the window behind is not painted again)
}

LiveTakeView::~LiveTakeView() = default;

void LiveTakeView::repaintIfChanged()
{
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (juce::uint64 v) { h ^= v; h *= 1099511628211ull; };
    auto mixD = [&mix] (double d) { juce::uint64 u = 0; std::memcpy (&u, &d, sizeof (u)); mix (u); };
    auto mixX = [&] (double seconds) { mix (seconds >= 0.0 ? (juce::uint64) (juce::int64) xOf (seconds) : 0x5eedull); };
    mixX (playhead); mixX (liveEnd); mixX (writtenEnd); mixX (mark1); mixX (mark2);
    mixD (viewStart); mixD (pxPerSec); mix ((juce::uint64) laneH); mixD ((double) vzoom); mix ((juce::uint64) scrollY);
    mix ((juce::uint64) getWidth()); mix ((juce::uint64) getHeight());
    mix ((juce::uint64) (juce::pointer_sized_uint) layout.get());
    mix ((juce::uint64) selectedFlag.hashCode64()); mixD (selA); mixD (selB); mix ((juce::uint64) selTracks.size());
    if (model != nullptr)
    {
        mix ((juce::uint64) model->peaks.changeCount()); mix ((juce::uint64) model->peaks.addCount());
        for (auto& f : model->flags) { mix ((juce::uint64) f.id.hashCode64()); mix ((juce::uint64) f.note.hashCode64()); mix ((juce::uint64) f.replies.size()); mix (f.removed ? 1u : 2u); mix ((juce::uint64) f.sample); }
    }
    for (auto& m : projectFlags) { mixD (m.seconds); mix ((juce::uint64) m.note.hashCode64()); mix ((juce::uint64) m.replies.size()); }
    if (auto* ms = markersNow()) for (auto& m : *ms) { mixD (m.seconds); mix ((juce::uint64) m.name.hashCode64()); }
    if (layout != nullptr)
        for (auto& t : layout->tracks)
        {
            if (isMuteSolo) { mix (isMuteSolo (t.id, false) ? 3u : 5u); mix (isMuteSolo (t.id, true) ? 7u : 11u); }
            if (showArm && isArmed) mix (isArmed (t.id) ? 13u : 17u);
        }
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
            if (f.removed) { have.insert (f.id); continue; }
            const double s = layout->windowSecondsOf (f.sample);
            if (s < 0.0) continue;                                          // (pinned between takes: not on this window's timeline)
            out.push_back ({ f.id, s, f.note, f.replies, false, f.pinnedAt });
            have.insert (f.id);
        }
    for (auto& m : projectFlags)                                            // flags already in the project (from an earlier session, or put in)
        if (have.count (m.id.toString()) == 0)
        {
            FlagShown f; f.id = m.id.toString(); f.seconds = m.seconds; f.note = m.note; f.pinnedAt = m.pinnedAt;
            for (auto& r : m.replies) f.replies.push_back ({ r.text, r.by, r.at });
            out.push_back (f);
        }
    std::sort (out.begin(), out.end(), [] (const FlagShown& a, const FlagShown& b) { return a.seconds < b.seconds; });
    return out;
}

void LiveTakeView::keepInView (double seconds, bool page)
{
    const double width = (double) juce::jmax (100, getWidth() - kHeaderW) / pxPerSec;
    if (page)
    {
        if (seconds > viewStart + width * 0.92) viewStart = seconds - width * 0.15;
        else if (seconds < viewStart) viewStart = juce::jmax (0.0, seconds - width * 0.1);
    }
    else if (seconds < viewStart || seconds > viewStart + width) viewStart = juce::jmax (0.0, seconds - width * 0.5);
}

void LiveTakeView::zoomBy (double factor, double around)
{
    const double x = (around - viewStart) * pxPerSec;
    pxPerSec = juce::jlimit (0.02, 4000.0, pxPerSec * factor);
    viewStart = juce::jmax (0.0, around - x / pxPerSec);
    repaint();
}

void LiveTakeView::zoomToFit()
{
    if (layout == nullptr) return;
    const double end = juce::jmax (30.0, juce::jmax (layout->endSeconds(), liveEnd) + 5.0);
    pxPerSec = juce::jlimit (0.02, 4000.0, (double) juce::jmax (100, getWidth() - kHeaderW - 20) / end);
    viewStart = 0.0;
    repaint();
}

int LiveTakeView::trackRowAt (int y) const
{
    if (layout == nullptr || y < lanesTop()) return -1;
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

void LiveTakeView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.fillAll (theme::wave);
    const auto shown = flagsShown();
    auto top = b.removeFromTop (kMarkerH);
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
            const double end = t.recording && liveEnd >= 0.0 ? juce::jmax (t.end(), liveEnd) : t.end();
            const int x0 = xOf (t.start), x1 = juce::jmax (x0 + 2, xOf (end));
            if (x1 < kHeaderW || x0 > getWidth()) continue;
            auto r = juce::Rectangle<int> (x0, heads.getY() + 1, x1 - x0, heads.getHeight() - 2);
            g.setColour (t.recording ? juce::Colour (0xffc62828) : theme::accent); g.fillRect (r);
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
    g.drawText ("MARKERS", 8, 0, kHeaderW - 10, kMarkerH, juce::Justification::centredLeft);
    g.drawText ("FLAGS", 8, kMarkerH, kHeaderW - 10, kFlagH, juce::Justification::centredLeft);
    g.drawText ("TIME", 8, kMarkerH + kFlagH, kHeaderW - 10, kRulerH, juce::Justification::centredLeft);
    g.drawText ("TAKES", 8, kMarkerH + kFlagH + kRulerH, kHeaderW - 10, kTakeHeadH, juce::Justification::centredLeft);
    // markers, flags (over the lanes: a faint line down from each)
    g.saveState(); g.reduceClipRegion (getLocalBounds().withTrimmedLeft (kHeaderW));
    if (auto* ms = markersNow()) markerui::paintFlags (g, *ms, [this] (double s) { return xOf (s); }, kHeaderW, getHeight(), markerMouse->draggedId());
    paintFlags (g, flagRow, shown);
    for (int k = 0; k < 2; ++k)                                             // marks 1 and 2 (blue, in the marker strip, with a line down)
    {
        const double at = k == 0 ? mark1 : mark2;
        if (at < 0.0) continue;
        const int x = xOf (at);
        g.setColour (juce::Colour (0xff4aa3ff).withAlpha (0.55f)); g.fillRect (x, kMarkerH, 1, getHeight() - kMarkerH);
        auto f = juce::Rectangle<int> (x - (k == 0 ? 0 : 16), 1, 16, kMarkerH - 2);
        g.setColour (juce::Colour (0xff4aa3ff)); g.fillRect (f);
        g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (k == 0 ? "1" : "2", f, juce::Justification::centred, false);
    }
    // the edge of what is on the disk (the Fixer), the record line, the playhead
    if (writtenEnd >= 0.0 && liveEnd > writtenEnd)
    {
        const int x = xOf (writtenEnd);
        g.setColour (juce::Colour (0xffffc94d).withAlpha (0.85f)); g.fillRect (x, kMarkerH + kFlagH, 2, getHeight());
    }
    if (liveEnd >= 0.0)
    {
        const int x = xOf (liveEnd);
        g.setColour (juce::Colour (0xffff3b30)); g.fillRect (x, kMarkerH + kFlagH, 2, getHeight());
    }
    theme::paintPlayhead (g, xOf (playhead), kMarkerH + kFlagH, getHeight() - kMarkerH - kFlagH);
    g.restoreState();
    g.setColour (theme::border); g.fillRect (kHeaderW - 1, 0, 1, getHeight());
}

void LiveTakeView::paintRuler (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (theme::ruler); g.fillRect (r);
    g.setColour (theme::grid); g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    const double span = (double) (getWidth() - kHeaderW) / pxPerSec;
    static const double steps[] { 0.1, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
    double step = 3600;
    for (auto s : steps) if (s * pxPerSec >= 70.0) { step = s; break; }
    juce::ignoreUnused (span);
    g.setFont (juce::FontOptions (11.0f));
    const double t0 = std::floor (viewStart / step) * step;
    for (double t = t0; xOf (t) < getWidth(); t += step)
    {
        const int x = xOf (t);
        if (x < kHeaderW) continue;
        g.setColour (theme::grid.brighter (0.3f)); g.fillRect (x, r.getY() + r.getHeight() / 2, 1, r.getHeight() / 2);
        g.setColour (theme::dimText);
        const int secs = (int) std::floor (t + 1.0e-6);
        juce::String txt = secs >= 3600 ? juce::String (secs / 3600) + ":" + juce::String ((secs / 60) % 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2)
                                        : juce::String (secs / 60).paddedLeft ('0', 2) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
        if (step < 1.0) txt << "." << juce::String ((int) std::lround ((t - std::floor (t)) * 10.0));
        g.drawText (txt, x + 3, r.getY(), 80, r.getHeight() / 2 + 3, juce::Justification::centredLeft, false);
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
        g.setColour (flagRed().withAlpha (0.35f)); g.fillRect (x, r.getBottom(), 1, getHeight() - r.getBottom());
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
    g.reduceClipRegion (area);
    const int x0 = kHeaderW, x1 = getWidth();
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
            double end = t.end();
            if (t.recording && liveEnd >= 0.0) end = juce::jmax (end, liveEnd);
            int cx0 = xOf (t.start), cx1 = xOf (end);
            if (cx1 < x0 || cx0 > x1) continue;
            auto clip = juce::Rectangle<int> (cx0, y + 1, juce::jmax (1, cx1 - cx0), laneH - 3);
            g.setColour (chan::clipFill (colour)); g.fillRect (clip);
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
            g.setColour (chan::clipFill (colour).brighter (0.35f)); g.drawRect (clip, 1);
        }
        // the selection (the Fixer's ReTouch, the Primary's loop)
        if (selB > selA && std::find (selTracks.begin(), selTracks.end(), tr.id) != selTracks.end())
        {
            const int sx0 = xOf (selA), sx1 = xOf (selB);
            g.setColour (juce::Colours::white.withAlpha (0.16f)); g.fillRect (sx0, y, juce::jmax (1, sx1 - sx0), laneH);
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawRect (sx0, y, juce::jmax (1, sx1 - sx0), laneH, 1);
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
    if (! usePeaks)
        for (auto& f : t.files) if (f.trackId == tr.id) { th = thumbFor (f.file); break; }
    if (! usePeaks && th == nullptr) return;
    const double rate = t.rate > 0 ? t.rate : 48000.0;
    const int n = x1 - x0;
    std::vector<float> tops ((size_t) n), bottoms ((size_t) n);
    setWaveColour (g, colour);
    for (int c = 0; c < chans; ++c)
    {
        const float bandH = (float) lane.getHeight() / (float) chans;
        const float mid = (float) lane.getY() + bandH * ((float) c + 0.5f);
        const float half = bandH * 0.5f - 1.0f;
        int from = 0;
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
        juce::ignoreUnused (from);
        if (any) fillEnvelope (g, (float) x0, tops, bottoms, nullptr, bandH);
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
            g.setColour (on ? juce::Colours::white : juce::Colour (0xff14181f)); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
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
    if (editableMarkers && markersNow() != nullptr && markerMouse->mouseDown (e)) return;
    if (e.y >= kMarkerH && e.y < kMarkerH + kFlagH && e.x >= kHeaderW)
    {
        // the nearest flag to the left of the click (within its note)
        const auto shown = flagsShown();
        const FlagShown* hit = nullptr;
        for (auto& f : shown) { const int x = xOf (f.seconds); if (e.x >= x - 4 && e.x <= x + 220) hit = &f; }
        if (hit != nullptr && onFlagClick) onFlagClick (hit->id, e.getNumberOfClicks() >= 2);
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
    if (e.y >= kMarkerH + kFlagH && e.x >= kHeaderW)
    {
        const double s = juce::jmax (0.0, secondsAt (e.x));
        if (! e.mods.isShiftDown() && (! canSeek || canSeek()) && onSeek) onSeek (s);
        selecting = true; dragFrom = s; dragRow0 = juce::jmax (0, trackRowAt (e.y));
        if (selA >= 0.0 && onSelect && ! e.mods.isShiftDown()) { selA = selB = -1.0; selTracks.clear(); onSelect (0.0, 0.0, {}); }
    }
}

void LiveTakeView::mouseDrag (const juce::MouseEvent& e)
{
    if (markerMouse->mouseDrag (e)) return;
    if (! selecting || layout == nullptr || ! onSelect) return;
    if (std::abs (e.getDistanceFromDragStartX()) < 4) return;
    const double s = juce::jmax (0.0, secondsAt (e.x));
    selA = juce::jmin (dragFrom, s); selB = juce::jmax (dragFrom, s);
    int r1 = trackRowAt (juce::jlimit (lanesTop(), getHeight() - 1, e.y));
    if (r1 < 0) r1 = (int) layout->tracks.size() - 1;
    selTracks.clear();
    for (int r = juce::jmin (dragRow0, r1); r <= juce::jmax (dragRow0, r1); ++r)
        if (juce::isPositiveAndBelow (r, (int) layout->tracks.size())) selTracks.push_back (layout->tracks[(size_t) r].id);
    repaint();
}

void LiveTakeView::mouseUp (const juce::MouseEvent& e)
{
    if (markerMouse->mouseUp (e)) return;
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
        const int full = (int) layout->tracks.size() * laneH, room = getHeight() - lanesTop();
        scrollY = juce::jlimit (0, juce::jmax (0, full - room), scrollY - (int) std::lround (w.deltaY * 120.0));
    }
    repaint();
}

void LiveTakeView::mouseMove (const juce::MouseEvent& e)
{
    juce::String t;
    if (e.y >= kMarkerH && e.y < kMarkerH + kFlagH && e.x >= kHeaderW)
        for (auto& f : flagsShown())
        {
            const int x = xOf (f.seconds);
            if (e.x >= x - 4 && e.x <= x + 220)
            {
                t = "Flag" + (f.pinnedAt.toMilliseconds() != 0 ? " pinned at " + f.pinnedAt.formatted ("%H:%M:%S") : juce::String()) + ": " + (f.note.isNotEmpty() ? f.note : juce::String ("(no note)"));
                for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
            }
        }
    if (t != hoverText) { hoverText = t; }
}

juce::String LiveTakeView::getTooltip() { return hoverText; }
} // namespace td::live
