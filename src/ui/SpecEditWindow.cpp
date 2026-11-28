#include "SpecEditWindow.h"
#include "Uikit.h"
#include "../core/AudioFormats.h"
#include "../core/Dsp.h"
#include "../core/TimeFormat.h"

namespace td
{
namespace
{
using wavestats::Profile;
using wavestats::Stats;
using ProfilePtr = std::shared_ptr<const Profile>;

bool none (double v) { return v <= wavestats::kNone + 1.0; }
juce::String levelText (double v, int decimals = 2) { return none (v) ? juce::String ("-inf") : juce::String (v, decimals); }
juce::String clockText (double seconds) { return formatTimeShort (juce::jmax (0.0, seconds), 3); }

//==================================================================================================================================
/** The render read into its Profile, on a thread of its own. 'done' comes back on the message thread (not at all once this has gone). */
class FileAnalysis : private juce::Thread
{
public:
    using Done = std::function<void (ProfilePtr, juce::String)>;
    FileAnalysis (const juce::File& f, Done d) : juce::Thread ("fermata wave statistics"), file (f), done (std::move (d)) { startThread(); }
    ~FileAnalysis() override { alive->store (false); stopThread (10000); }
    float progress() const noexcept { return prog.load(); }

private:
    void run() override
    {
        juce::AudioFormatManager fm; registerAudioFormats (fm);
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        std::shared_ptr<Profile> out; juce::String err;
        if (r == nullptr || r->lengthInSamples <= 0 || r->numChannels == 0) err = "The render could not be read.";
        else
        {
            const int nc = (int) r->numChannels, block = 65536;
            wavestats::Analyzer an (r->sampleRate, nc);
            juce::AudioBuffer<float> buf (nc, block);
            for (juce::int64 pos = 0; pos < r->lengthInSamples && ! threadShouldExit(); pos += block)
            {
                const int n = (int) juce::jmin<juce::int64> (block, r->lengthInSamples - pos);
                if (! r->read (buf.getArrayOfWritePointers(), nc, pos, n)) { err = "The render could not be read to its end."; break; }
                an.process (buf.getArrayOfReadPointers(), n);
                prog.store ((float) ((double) (pos + n) / (double) r->lengthInSamples));
            }
            if (err.isEmpty() && ! threadShouldExit()) out = std::make_shared<Profile> (an.finish());
        }
        if (threadShouldExit()) return;
        juce::MessageManager::callAsync ([a = alive, d = done, out, err] { if (a->load()) d (out, err); });
    }
    juce::File file; Done done;
    std::atomic<float> prog { 0.0f };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

/** The render's samples for the window itself (the ends of a selection, the waveform zoomed far in, the vectorscope and the spectrum):
    frames outside the file are silence. Message thread. */
struct FileSamples
{
    std::unique_ptr<juce::AudioFormatReader> reader;
    void open (const juce::File& f)
    {
        juce::AudioFormatManager fm; registerAudioFormats (fm);
        reader.reset (f.existsAsFile() ? fm.createReaderFor (f) : nullptr);
    }
    bool read (juce::int64 start, int n, float* const* dest, int channels) const
    {
        if (reader == nullptr || n <= 0) return false;
        for (int c = 0; c < channels; ++c) juce::FloatVectorOperations::clear (dest[c], n);
        const juce::int64 a = juce::jmax<juce::int64> (0, start), b = juce::jmin<juce::int64> (reader->lengthInSamples, start + n);
        if (b <= a) return true;
        const int nc = juce::jmin (channels, (int) reader->numChannels);
        std::vector<float*> at ((size_t) nc);
        for (int c = 0; c < nc; ++c) at[(size_t) c] = dest[c] + (a - start);
        return reader->read (at.data(), nc, a, (int) (b - a));
    }
};

//==================================================================================================================================
/** The piece along the window: its waveform, its loudness under it, the selection and the playhead. */
class WaveView : public juce::Component, private juce::ScrollBar::Listener
{
public:
    std::function<void (double)> onPlayhead;                 // a click: the playhead goes there
    std::function<void()> onSelection;                        // the selection changed (also while it is dragged)
    const FileSamples* samples = nullptr;

    WaveView()
    {
        addChildComponent (bar);
        bar.addListener (this); bar.setAutoHide (false);
        setWantsKeyboardFocus (false); setMouseClickGrabsKeyboardFocus (false);
    }
    void setProfile (ProfilePtr p, bool samePiece)
    {
        profile = std::move (p);
        const double len = profile != nullptr ? profile->seconds() : 0.0;
        if (! samePiece || viewLen <= 0.0 || viewStart + viewLen > len + 1.0e-6) { viewStart = 0.0; viewLen = len; }
        if (! samePiece) { selA = selB = -1.0; }
        selA = juce::jmin (selA, len); selB = juce::jmin (selB, len);
        integrated = profile != nullptr ? wavestats::measure (*profile).integrated : wavestats::kNone;
        fine.valid = false; updateBar(); repaint();
    }
    void setPlayhead (double s) { if (std::abs (s - playhead) < 1.0e-9) return; const int a = xOf (playhead), b = xOf (s); playhead = s; repaint (juce::jmin (a, b) - 3, 0, std::abs (a - b) + 6, getHeight()); }
    double playheadSeconds() const noexcept { return playhead; }
    bool hasSelection() const noexcept { return selB > selA + 1.0e-9; }
    double selStart() const noexcept { return selA; }
    double selEnd() const noexcept { return selB; }
    void clearSelection() { if (hasSelection()) { selA = selB = -1.0; repaint(); if (onSelection) onSelection(); } }
    void selectAll() { if (profile == nullptr) return; selA = 0.0; selB = profile->seconds(); repaint(); if (onSelection) onSelection(); }
    void zoomBy (double factor, double around) { zoomAround (factor, around); }
    void zoomToAll() { if (profile == nullptr) return; viewStart = 0.0; viewLen = profile->seconds(); viewChanged(); }
    /** Keeps the playhead in view while it plays. */
    void follow (double s)
    {
        if (profile == nullptr || viewLen >= profile->seconds() - 1.0e-9 || dragging) return;
        if (s < viewStart || s > viewStart + viewLen) { viewStart = juce::jlimit (0.0, juce::jmax (0.0, profile->seconds() - viewLen), s - viewLen * 0.1); viewChanged(); }
    }

    void resized() override { bar.setBounds (0, getHeight() - kBarH, getWidth(), kBarH); fine.valid = false; updateBar(); }

    void paint (juce::Graphics& g) override
    {
        const auto all = getLocalBounds();
        g.setColour (theme::wave); g.fillRect (all);
        const auto ruler = rulerArea(), waves = waveArea(), loud = loudArea();
        g.setColour (theme::ruler); g.fillRect (ruler);
        if (profile == nullptr || profile->empty() || viewLen <= 0.0)
        {
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (14.0f));
            g.drawText (emptyText, waves, juce::Justification::centred, true);
            return;
        }
        const auto& p = *profile;
        // the times
        {
            static const double steps[] = { 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1200, 3600 };
            double step = steps[std::size (steps) - 1];
            for (double s : steps) if (s / viewLen * (double) getWidth() >= 84.0) { step = s; break; }
            g.setFont (juce::FontOptions (11.0f));
            for (double t = std::ceil (viewStart / step) * step; t < viewStart + viewLen; t += step)
            {
                const int x = xOf (t);
                g.setColour (theme::grid); g.fillRect (x, ruler.getBottom() - 5, 1, 5 + waves.getHeight() + loud.getHeight());
                g.setColour (theme::dimText); g.drawText (formatTimeShort (t, step < 1.0 ? 2 : 0), x + 3, ruler.getY(), 80, ruler.getHeight(), juce::Justification::centredLeft, false);
            }
        }
        // the waveform: a lane a channel
        const int lanes = juce::jmin (p.channels, 6);
        const double ticksPerPx = viewLen * 100.0 / (double) juce::jmax (1, getWidth());
        const bool useFine = ticksPerPx < 0.75 && samples != nullptr && samples->reader != nullptr;
        if (useFine) fillFine();
        for (int c = 0; c < lanes; ++c)
        {
            const auto lane = juce::Rectangle<int> (0, waves.getY() + waves.getHeight() * c / lanes, getWidth(), waves.getHeight() / lanes).reduced (0, 1);
            const float mid = (float) lane.getCentreY(), half = (float) lane.getHeight() * 0.5f;
            g.setColour (theme::grid.withAlpha (0.7f)); g.fillRect (0.0f, mid - 0.5f, (float) getWidth(), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));                                    // full scale, and 6 dB under it
            for (float lv : { 1.0f, 0.5f }) { g.fillRect (0.0f, mid - half * lv, (float) getWidth(), 1.0f); g.fillRect (0.0f, mid + half * lv - 1.0f, (float) getWidth(), 1.0f); }
            g.setColour (theme::manuscript() ? theme::accent : juce::Colour (0xff6fb7e9));
            const auto& ticks = p.ch[(size_t) c];
            juce::RectangleList<float> cols;
            for (int x = 0; x < getWidth(); ++x)
            {
                float lo, hi;
                if (useFine && fine.valid) { lo = fine.lo[(size_t) c][(size_t) x]; hi = fine.hi[(size_t) c][(size_t) x]; }
                else
                {
                    const auto t0 = (juce::int64) std::floor ((viewStart + viewLen * (double) x / (double) getWidth()) * 100.0);
                    const auto t1 = juce::jmax (t0 + 1, (juce::int64) std::floor ((viewStart + viewLen * (double) (x + 1) / (double) getWidth()) * 100.0));
                    if (t0 >= (juce::int64) ticks.size() || t1 <= 0) continue;
                    lo = 1.0e9f; hi = -1.0e9f;
                    for (auto t = juce::jmax<juce::int64> (0, t0); t < juce::jmin<juce::int64> ((juce::int64) ticks.size(), t1); ++t) { lo = juce::jmin (lo, ticks[(size_t) t].lo); hi = juce::jmax (hi, ticks[(size_t) t].hi); }
                    if (lo > hi) continue;
                }
                const float y0 = mid - half * juce::jlimit (-1.0f, 1.0f, hi), y1 = mid - half * juce::jlimit (-1.0f, 1.0f, lo);
                cols.addWithoutMerging ({ (float) x, y0, 1.0f, juce::jmax (1.0f, y1 - y0) });
            }
            g.fillRectList (cols);
            if (p.channels <= 2) { g.setColour (theme::dimText); g.setFont (juce::FontOptions (11.0f, juce::Font::bold)); g.drawText (p.channels == 1 ? "M" : c == 0 ? "L" : "R", 4, lane.getY() + 2, 20, 14, juce::Justification::centredLeft, false); }
        }
        // the loudness along the piece: momentary (faint), short-term, and the integrated loudness as a line
        if (loud.getHeight() > 12)
        {
            g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (loud);
            g.setColour (theme::border); g.fillRect (0, loud.getY(), getWidth(), 1);
            const double top = loudTop(), bottom = top - 36.0;
            auto yOf = [&] (double lufs) { return (float) loud.getY() + (float) ((top - juce::jlimit (bottom, top, lufs)) / (top - bottom)) * (float) (loud.getHeight() - 1); };
            g.setFont (juce::FontOptions (9.5f));
            for (double l = std::floor (top / 6.0) * 6.0; l > bottom; l -= 6.0)
            {
                g.setColour (theme::grid.withAlpha (0.8f)); g.fillRect (0.0f, yOf (l), (float) getWidth(), 1.0f);
                g.setColour (theme::dimText.withAlpha (0.8f)); g.drawText (juce::String ((int) l), getWidth() - 30, (int) yOf (l) - 6, 26, 12, juce::Justification::centredRight, false);
            }
            juce::Path m, s; bool mOn = false, sOn = false;
            for (int x = 0; x < getWidth(); ++x)
            {
                const auto t = (juce::int64) std::floor ((viewStart + viewLen * ((double) x + 0.5) / (double) getWidth()) * 100.0);
                const double mv = p.momentaryAt (t), sv = p.shortTermAt (t);
                if (! none (mv)) { if (mOn) m.lineTo ((float) x, yOf (mv)); else m.startNewSubPath ((float) x, yOf (mv)); mOn = true; } else mOn = false;
                if (! none (sv)) { if (sOn) s.lineTo ((float) x, yOf (sv)); else s.startNewSubPath ((float) x, yOf (sv)); sOn = true; } else sOn = false;
            }
            g.setColour (theme::text.withAlpha (0.35f)); g.strokePath (m, juce::PathStrokeType (1.0f));
            g.setColour (juce::Colour (0xff3fbf6a)); g.strokePath (s, juce::PathStrokeType (1.6f));
            if (! none (integrated))
            {
                g.setColour (juce::Colour (0xffffc83d));
                for (int x = 0; x < getWidth(); x += 8) g.fillRect ((float) x, yOf (integrated), 5.0f, 1.0f);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText ("I " + juce::String (integrated, 1), 6, (int) yOf (integrated) - 13, 70, 12, juce::Justification::centredLeft, false);
            }
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("LOUDNESS (LUFS):  short-term, momentary (faint), integrated (dashes)", 6, loud.getY() + 2, getWidth() - 44, 12, juce::Justification::centredLeft, true);
        }
        // the selection and the playhead
        if (hasSelection())
        {
            const int a = xOf (selA), b = juce::jmax (a + 1, xOf (selB));
            g.setColour (theme::accent.withAlpha (0.22f)); g.fillRect (a, ruler.getBottom(), b - a, waves.getHeight() + loud.getHeight());
            g.setColour (theme::accent); g.fillRect (a, ruler.getY(), 1, getHeight() - kBarH - ruler.getY()); g.fillRect (b - 1, ruler.getY(), 1, getHeight() - kBarH - ruler.getY());
            g.fillRect (a, ruler.getBottom() - 3, b - a, 3);
        }
        theme::paintPlayhead (g, xOf (playhead), ruler.getY(), getHeight() - kBarH - ruler.getY(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (profile == nullptr) return;
        anchor = e.mods.isShiftDown() ? (hasSelection() ? (std::abs (timeAt (e.x) - selA) < std::abs (timeAt (e.x) - selB) ? selB : selA) : playhead) : timeAt (e.x);
        dragging = false;
        if (e.mods.isShiftDown()) { dragging = true; mouseDrag (e); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (profile == nullptr) return;
        if (! dragging && std::abs (e.getDistanceFromDragStartX()) < 3) return;
        dragging = true;
        const double t = timeAt (e.x);
        selA = juce::jmin (anchor, t); selB = juce::jmax (anchor, t);
        repaint();
        if (onSelection) onSelection();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (profile == nullptr) return;
        if (! dragging)
        {
            const bool had = hasSelection();
            selA = selB = -1.0; repaint();
            if (had && onSelection) onSelection();
            if (onPlayhead) onPlayhead (timeAt (e.x));
        }
        dragging = false;
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (viewLen < (profile != nullptr ? profile->seconds() : 0.0) - 1.0e-9) zoomToAll(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (profile == nullptr) return;
        if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const double d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY) * viewLen * 0.5;
            viewStart = juce::jlimit (0.0, juce::jmax (0.0, profile->seconds() - viewLen), viewStart + d); viewChanged();
        }
        else zoomAround (std::pow (1.6, (double) w.deltaY * 3.0), timeAt (e.x));
    }

    juce::String emptyText { "Measuring..." };

private:
    static constexpr int kRulerH = 18, kBarH = 12;
    juce::Rectangle<int> rulerArea() const { return { 0, 0, getWidth(), kRulerH }; }
    int loudH() const { const int h = getHeight() - kRulerH - kBarH; return h >= 150 ? juce::jmin (86, h / 3) : 0; }
    juce::Rectangle<int> waveArea() const { return { 0, kRulerH, getWidth(), juce::jmax (0, getHeight() - kRulerH - kBarH - loudH()) }; }
    juce::Rectangle<int> loudArea() const { return { 0, getHeight() - kBarH - loudH(), getWidth(), loudH() }; }
    double timeAt (int x) const { const double len = profile != nullptr ? profile->seconds() : 0.0; return juce::jlimit (0.0, len, viewStart + viewLen * (double) x / (double) juce::jmax (1, getWidth())); }
    int xOf (double t) const { return viewLen > 0.0 ? (int) std::floor ((t - viewStart) / viewLen * (double) getWidth()) : 0; }
    /** The top of the loudness lane's scale: a little over the loudest 400 ms, on a whole 6. */
    double loudTop() const
    {
        if (profile == nullptr) return -6.0;
        if (loudTopFor != profile.get())
        {
            loudTopFor = profile.get();
            const auto st = wavestats::measure (*profile);
            loudTopValue = none (st.maxMomentary) ? -6.0 : juce::jmin (0.0, std::ceil ((st.maxMomentary + 2.0) / 6.0) * 6.0);
        }
        return loudTopValue;
    }
    void zoomAround (double factor, double around)
    {
        if (profile == nullptr) return;
        const double len = profile->seconds();
        const double least = juce::jmin (len, juce::jmax (0.02, (double) getWidth() / profile->rate * 4.0));       // no closer than 4 pixels a sample
        const double newLen = juce::jlimit (least, len, viewLen / factor);
        const double frac = viewLen > 0.0 ? (around - viewStart) / viewLen : 0.5;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, len - newLen), around - frac * newLen); viewLen = newLen;
        viewChanged();
    }
    void viewChanged() { fine.valid = false; updateBar(); repaint(); }
    void updateBar()
    {
        const double len = profile != nullptr ? profile->seconds() : 0.0;
        const bool zoomed = len > 0.0 && viewLen < len - 1.0e-9;
        bar.setVisible (zoomed);
        if (zoomed) { bar.setRangeLimits (0.0, len, juce::dontSendNotification); bar.setCurrentRange (viewStart, viewLen, juce::dontSendNotification); }
    }
    void scrollBarMoved (juce::ScrollBar*, double newStart) override { viewStart = newStart; fine.valid = false; repaint(); }
    /** Zoomed further in than a tick a pixel: the columns from the samples themselves (read when the view changes). */
    void fillFine()
    {
        if (fine.valid || profile == nullptr || samples == nullptr) return;
        const auto& p = *profile;
        const int w = getWidth(), nc = juce::jmin (p.channels, 6);
        const auto a = (juce::int64) std::floor (viewStart * p.rate), b = (juce::int64) std::ceil ((viewStart + viewLen) * p.rate) + 1;
        const int n = (int) juce::jlimit<juce::int64> (0, 4000000, b - a);
        fine.lo.assign ((size_t) nc, std::vector<float> ((size_t) w, 0.0f)); fine.hi = fine.lo;
        if (n <= 0 || w <= 0) return;
        juce::AudioBuffer<float> buf (p.channels, n);
        if (! samples->read (a, n, buf.getArrayOfWritePointers(), p.channels)) return;
        for (int c = 0; c < nc; ++c)
        {
            const float* x = buf.getReadPointer (c);
            for (int px = 0; px < w; ++px)
            {
                // (a pixel's column reaches to the next pixel's first sample, so neighbours join up when a sample is wider than a pixel)
                const auto i0 = juce::jlimit<juce::int64> (0, n - 1, (juce::int64) std::floor ((viewStart + viewLen * (double) px / (double) w) * p.rate) - a);
                const auto i1 = juce::jlimit<juce::int64> (i0, n - 1, (juce::int64) std::floor ((viewStart + viewLen * (double) (px + 1) / (double) w) * p.rate) - a);
                float lo = x[i0], hi = x[i0];
                for (auto i = i0 + 1; i <= i1; ++i) { lo = juce::jmin (lo, x[i]); hi = juce::jmax (hi, x[i]); }
                fine.lo[(size_t) c][(size_t) px] = lo; fine.hi[(size_t) c][(size_t) px] = hi;
            }
        }
        fine.valid = true;
    }

    ProfilePtr profile;
    double viewStart = 0.0, viewLen = 0.0, playhead = 0.0, selA = -1.0, selB = -1.0, anchor = 0.0, integrated = wavestats::kNone;
    mutable const Profile* loudTopFor = nullptr; mutable double loudTopValue = -6.0;
    bool dragging = false;
    struct Fine { bool valid = false; std::vector<std::vector<float>> lo, hi; } fine;
    juce::ScrollBar bar { false };
};

//==================================================================================================================================
/** The figures: the whole piece, and the selected part beside it. */
class StatsPanel : public juce::Component, public juce::TooltipClient
{
public:
    std::function<void (juce::int64 frame)> onGoTo;           // a click on a peak's figure: the playhead goes to the peak

    void set (ProfilePtr p, const Stats& whole, const Stats* selection, bool busyNow)
    {
        profile = std::move (p); all = whole; hasSel = selection != nullptr; if (hasSel) sel = *selection; busy = busyNow;
        repaint();
    }
    /** The figures as text (the Copy key). */
    juce::String asText (const juce::String& piece) const
    {
        juce::String t = "Waveform statistics - " + piece + "\n";
        if (profile == nullptr) return t;
        auto block = [&] (const juce::String& head, const Stats& s)
        {
            t << "\n" << head << "\n";
            for (int r = 0; r < kRows; ++r)
            {
                t << juce::String (rowName (r)).paddedRight (' ', 28);
                if (isPerChannel (r)) for (int c = 0; c < juce::jmin (2, (int) s.ch.size()); ++c) t << cell (s, r, c).paddedLeft (' ', 14);
                else t << cell (s, r, -1).paddedLeft (' ', 14);
                t << "\n";
            }
        };
        block ("Whole piece  (" + clockText (profile->seconds()) + ")", all);
        if (hasSel) block ("Selection  " + rangeText (sel), sel);
        return t;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel); theme::fillBox (g, getLocalBounds());
        g.setColour (theme::border); theme::drawBox (g, getLocalBounds());
        if (profile == nullptr)
        {
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (13.0f));
            g.drawText (busy ? "Measuring..." : "No figures yet.", getLocalBounds(), juce::Justification::centred, true);
            return;
        }
        hits.clear();
        const int x0 = 10, w = getWidth() - 20;
        rowH = juce::jlimit (14, 19, (getHeight() - 8 - 2 * (kHeadH + kColH) - 16) / (2 * kRows));       // (a short window: the rows close up)
        int y = 8;
        drawBlock (g, "WHOLE PIECE   " + clockText (profile->seconds()), all, x0, y, w, true);
        y += 6;
        if (hasSel) drawBlock (g, "SELECTION   " + rangeText (sel), sel, x0, y, w, false);
        else
        {
            g.setColour (theme::accent); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("SELECTION", x0, y, w, kHeadH, juce::Justification::centredLeft, false); y += kHeadH;
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText ("Drag along the waveform to select a part: its figures come here, beside the whole piece's.", x0, y, w, 36, juce::Justification::topLeft, 3);
        }
    }

    juce::String getTooltip() override
    {
        const auto p = getMouseXYRelative();
        for (auto& h : hits) if (h.area.contains (p)) return h.tip;
        return {};
    }
    void mouseDown (const juce::MouseEvent& e) override { for (auto& h : hits) if (h.area.contains (e.getPosition()) && h.frame >= 0 && onGoTo) { onGoTo (h.frame); return; } }
    void mouseMove (const juce::MouseEvent& e) override
    {
        bool hand = false;
        for (auto& h : hits) if (h.area.contains (e.getPosition()) && h.frame >= 0) hand = true;
        setMouseCursor (hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

private:
    static constexpr int kRows = 11, kHeadH = 20, kColH = 16;
    int rowH = 19;
    static const char* rowName (int r)
    {
        static const char* names[kRows] = { "True peak level", "Sample peak level", "Max. RMS level", "Min. RMS level", "Total RMS level", "Possibly clipped samples", "DC offset",
                                            "Max. momentary loudness", "Max. short-term loudness", "Integrated loudness", "Loudness range (LRA)" };
        return names[r];
    }
    static bool isPerChannel (int r) { return r <= 6; }
    static juce::String rowTip (int r)
    {
        switch (r)
        {
            case 0:  return "The highest level, the levels BETWEEN the samples included (inter-sample peaks): the signal is oversampled 4 times (ITU-R BS.1770). Click a figure: the playhead goes to that peak.";
            case 1:  return "The highest sample. Click a figure: the playhead goes there (to within 10 ms).";
            case 2:  return "The highest RMS level of the windows (their length is chosen below).";
            case 3:  return "The lowest RMS level of the windows.";
            case 4:  return "The RMS level of all of it.";
            case 5:  return "How many samples have a true peak over 0 dBTP.";
            case 6:  return "The mean of the samples, in per cent of full scale.";
            case 7:  return "The loudest 400 ms: K-weighted, as ITU-R BS.1770 defines momentary loudness (a new window every 100 ms).";
            case 8:  return "The loudest 3 seconds (BS.1770 short-term loudness, a new window every 100 ms).";
            case 9:  return "The loudness of all of it, as BS.1770 measures a programme: 400 ms blocks, the ones under -70 LUFS and those more than 10 LU under the rest left out.";
            default: return "How far the loudness moves (EBU Tech 3342): between the 10th and the 95th percentile of the 3-second loudness, the quiet end gated.";
        }
    }
    static juce::String cell (const Stats& s, int r, int c)
    {
        if (isPerChannel (r))
        {
            if (c < 0 || c >= (int) s.ch.size()) return {};
            const auto& k = s.ch[(size_t) c];
            switch (r)
            {
                case 0:  return levelText (k.truePeakDb) + " dBTP";
                case 1:  return levelText (k.samplePeakDb) + " dB";
                case 2:  return levelText (k.maxRmsDb) + " dB";
                case 3:  return levelText (k.minRmsDb) + " dB";
                case 4:  return levelText (k.totalRmsDb) + " dB";
                case 5:  return juce::String (k.clipped);
                default: return juce::String (k.dcPercent, 3) + " %";
            }
        }
        switch (r)
        {
            case 7:  return none (s.maxMomentary) ? juce::String ("-") : juce::String (s.maxMomentary, 1) + " LUFS";
            case 8:  return none (s.maxShortTerm) ? juce::String ("-") : juce::String (s.maxShortTerm, 1) + " LUFS";
            case 9:  return none (s.integrated) ? juce::String ("-") : juce::String (s.integrated, 1) + " LUFS";
            default: return s.lraValid ? juce::String (s.lra, 1) + " LU" : juce::String ("-");
        }
    }
    juce::String rangeText (const Stats& s) const
    {
        const double r = profile != nullptr && profile->rate > 0.0 ? profile->rate : 48000.0;
        return clockText ((double) s.from / r) + " - " + clockText ((double) s.to / r) + "   (" + juce::String ((double) (s.to - s.from) / r, 3) + " s)";
    }
    void drawBlock (juce::Graphics& g, const juce::String& head, const Stats& s, int x0, int& y, int w, bool whole)
    {
        const int nameW = juce::jmin (176, w * 46 / 100), nch = juce::jmin (2, (int) s.ch.size()), colW = (w - nameW) / 2;
        g.setColour (whole ? theme::text : theme::accent); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (head, x0, y, w, kHeadH, juce::Justification::centredLeft, true); y += kHeadH;
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        if (nch == 2) { g.drawText ("LEFT", x0 + nameW, y, colW - 6, kColH, juce::Justification::centredRight, false); g.drawText ("RIGHT", x0 + nameW + colW, y, colW - 6, kColH, juce::Justification::centredRight, false); }
        y += kColH;
        for (int r = 0; r < kRows; ++r)
        {
            const juce::Rectangle<int> row (x0, y, w, rowH);
            if (r % 2 == 0) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRect (row); }
            if (r == 7) { g.setColour (theme::border); g.fillRect (x0, y, w, 1); }
            g.setColour (theme::text.withAlpha (0.86f)); g.setFont (juce::FontOptions (12.0f));
            g.drawText (rowName (r), x0 + 4, y, nameW - 4, rowH, juce::Justification::centredLeft, true);
            juce::String tip = rowTip (r);
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            if (isPerChannel (r))
            {
                if (r == 6 && nch > 0)
                {
                    tip << "\nAs a level: " << levelText (s.dcDb (0), 1) << " dB" << (nch == 2 ? " (left), " + levelText (s.dcDb (1), 1) + " dB (right)" : juce::String());
                }
                for (int c = 0; c < nch; ++c)
                {
                    const auto& k = s.ch[(size_t) c];
                    const juce::Rectangle<int> a (nch == 2 ? x0 + nameW + c * colW : x0 + nameW, y, nch == 2 ? colW - 6 : w - nameW - 6, rowH);
                    juce::Colour col = theme::text;
                    if (r == 0 && k.truePeakDb > 0.0) col = juce::Colour (0xffff6a5a);
                    if (r == 5 && k.clipped > 0) col = juce::Colour (0xffff6a5a);
                    g.setColour (col);
                    g.drawText (cell (s, r, c), a, juce::Justification::centredRight, false);
                    if (r <= 1) hits.push_back ({ a, tip, r == 0 ? k.truePeakAt : k.samplePeakAt });
                }
                hits.push_back ({ row.withWidth (nameW), tip, -1 });
                if (r > 1) hits.push_back ({ row, tip, -1 });
            }
            else
            {
                g.setColour (theme::text);
                g.drawText (cell (s, r, -1), x0 + nameW, y, w - nameW - 6, rowH, juce::Justification::centredRight, false);
                hits.push_back ({ row, tip, -1 });
            }
            y += rowH;
        }
        if (! s.exactEnds)
        {
            g.setColour (theme::warn); g.setFont (juce::FontOptions (10.5f));
            g.drawText ("(the ends rounded to 10 ms: the file could not be read)", x0, y, w, 12, juce::Justification::centredLeft, true);
        }
    }

    struct Hit { juce::Rectangle<int> area; juce::String tip; juce::int64 frame; };
    std::vector<Hit> hits;
    ProfilePtr profile; Stats all, sel; bool hasSel = false, busy = false;
};

//==================================================================================================================================
/** The meters: they read the Profile at the playhead (WaveStats.h) - and the file itself for the vectorscope and the spectrum. */
class MetersPanel : public juce::Component, public juce::TooltipClient
{
public:
    const FileSamples* samples = nullptr;
    bool aes17 = true;

    void setProfile (ProfilePtr p) { profile = std::move (p); resetHolds(); lastTick = -1; repaint(); }
    void resetHolds() { for (auto& h : tpMax) h = 0.0f; for (auto& b : bars) b = {}; playedFrom = -1; playedI = wavestats::kNone; playedLra = 0.0; playedLraOk = false; specHold.clear(); }
    /** The playhead moved (playing: every frame of the timer; standing still: once). */
    void setPlayhead (double seconds, bool playing)
    {
        if (profile == nullptr || profile->empty()) return;
        const auto& p = *profile;
        const auto tick = juce::jlimit<juce::int64> (0, juce::jmax<juce::int64> (0, p.ticks() - 1), (juce::int64) std::floor (seconds * 100.0));
        const auto now = juce::Time::getMillisecondCounter();
        const double dt = playing && wasPlaying ? juce::jlimit (0.0, 0.25, (double) (now - lastMs) / 1000.0) : 0.0;
        if (playing && ! wasPlaying) { resetHolds(); playedFrom = tick; }
        const auto from = playing && wasPlaying && lastTick >= 0 && tick >= lastTick && tick - lastTick < 60 ? lastTick + 1 : juce::jmax<juce::int64> (0, tick - 2);
        for (int c = 0; c < juce::jmin (2, p.channels); ++c)
        {
            float tp = 0.0f, sp = 0.0f;
            for (auto t = from; t <= tick; ++t) { tp = juce::jmax (tp, p.ch[(size_t) c][(size_t) t].truePeak); sp = juce::jmax (sp, p.ch[(size_t) c][(size_t) t].samplePeak); }
            auto& b = bars[(size_t) c];
            const double tpDb = wavestats::ampToDb (tp), spDb = wavestats::ampToDb (sp);
            if (! playing) { b.tp = tpDb; b.sp = spDb; b.hold = tpDb; b.holdMs = now; }
            else
            {
                b.tp = juce::jmax (tpDb, b.tp - 20.0 * dt); b.sp = juce::jmax (spDb, b.sp - 20.0 * dt);          // (they fall back 20 dB a second)
                if (tpDb >= b.hold || now - b.holdMs > 2000) { b.hold = tpDb; b.holdMs = now; }
                tpMax[(size_t) c] = juce::jmax (tpMax[(size_t) c], tp);
            }
            b.avg = wavestats::powerToDb ((aes17 ? 2.0 : 1.0) * p.power (c, tick + 1 - 30, tick + 1));           // the last 300 ms
        }
        momentary = p.momentaryAt (tick + 1); shortTerm = p.shortTermAt (tick + 1);
        corr = p.correlation (tick + 1 - 30, tick + 1);
        if (playing && playedFrom >= 0 && (now - playedMs > 250 || tick < lastTick))
        {
            playedMs = now;
            const auto st = wavestats::measure (p, playedFrom * p.tickLen, (tick + 1) * p.tickLen);
            playedI = st.integrated; playedLra = st.lra; playedLraOk = st.lraValid;
        }
        wasPlayingNow = playing;
        readScope (seconds);
        lastTick = tick; lastMs = now; wasPlaying = playing;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel); theme::fillBox (g, getLocalBounds());
        g.setColour (theme::border); theme::drawBox (g, getLocalBounds());
        tips.clear();
        auto r = getLocalBounds().reduced (8, 6);
        if (profile == nullptr) { g.setColour (theme::dimText); g.setFont (juce::FontOptions (13.0f)); g.drawText ("The meters follow the playhead once the piece is measured.", r, juce::Justification::centred, true); return; }
        const int h = r.getHeight();
        // 1: true peak
        {
            auto a = r.removeFromLeft (92); r.removeFromLeft (10);
            head (g, a, "TRUE PEAK");
            auto nums = a.removeFromBottom (30);
            const auto scale = a.removeFromRight (22);
            const int bw = (a.getWidth() - 4) / 2;
            for (int c = 0; c < 2; ++c)
            {
                const auto bar = juce::Rectangle<int> (a.getX() + c * (bw + 4), a.getY(), bw, a.getHeight());
                const auto& b = bars[(size_t) c];
                drawBar (g, bar, b.tp, kPeakTop, kPeakBottom, true);
                marker (g, bar, b.sp, kPeakTop, kPeakBottom, juce::Colours::white.withAlpha (0.55f), 1);
                marker (g, bar, b.hold, kPeakTop, kPeakBottom, b.hold > 0.0 ? juce::Colour (0xffff6a5a) : juce::Colours::white, 2);
                const double mx = wavestats::ampToDb (tpMax[(size_t) c]);
                g.setColour (mx > 0.0 ? juce::Colour (0xffff6a5a) : theme::text); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                g.drawText (wasPlaying || tpMax[(size_t) c] > 0.0f ? levelText (mx, 1) : levelText (b.tp, 1), bar.getX() - 4, nums.getY() + 2, bw + 8, 14, juce::Justification::centred, false);
                g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (c == 0 ? "L" : "R", bar.getX(), nums.getY() + 16, bw, 12, juce::Justification::centred, false);
            }
            drawScale (g, scale, a.getY(), a.getHeight(), kPeakTop, kPeakBottom, { 0, -6, -12, -18, -24, -36, -48 });
            tips.push_back ({ a.getUnion (nums), "True peak (the bar, dBTP) with the sample peak (the thin line) and a peak-hold line (red over 0 dBTP). The figure: the highest true peak "
                                                 "since Play was pressed - or, standing still, the true peak at the playhead." });
        }
        // 2: peak and average on one scale
        {
            auto a = r.removeFromLeft (92); r.removeFromLeft (10);
            head (g, a, "PEAK + AVERAGE");
            auto nums = a.removeFromBottom (30);
            const auto scale = a.removeFromRight (22);
            const int bw = (a.getWidth() - 4) / 2;
            for (int c = 0; c < 2; ++c)
            {
                const auto bar = juce::Rectangle<int> (a.getX() + c * (bw + 4), a.getY(), bw, a.getHeight());
                const auto& b = bars[(size_t) c];
                drawBar (g, bar, b.avg, kPeakTop, kPeakBottom, false);
                marker (g, bar, b.tp, kPeakTop, kPeakBottom, juce::Colour (0xffffc83d), 3);
                g.setColour (theme::text); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
                g.drawText (levelText (b.avg, 1), bar.getX() - 4, nums.getY() + 2, bw + 8, 14, juce::Justification::centred, false);
                g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText (c == 0 ? "L" : "R", bar.getX(), nums.getY() + 16, bw, 12, juce::Justification::centred, false);
            }
            drawScale (g, scale, a.getY(), a.getHeight(), kPeakTop, kPeakBottom, { 0, -6, -12, -18, -24, -36, -48 });
            tips.push_back ({ a.getUnion (nums), "The average level (the bar: RMS over the last 300 ms) and the peak (the yellow block) on one scale: the gap between them is the crest "
                                                 "of the music at this moment. The figure is the average." });
        }
        // 3: loudness
        {
            auto a = r.removeFromLeft (150); r.removeFromLeft (10);
            head (g, a, "LOUDNESS  (LUFS)");
            auto nums = a.removeFromBottom (30);
            const auto scale = a.removeFromLeft (24);
            auto mBar = a.removeFromLeft (20); a.removeFromLeft (4);
            auto sBar = a.removeFromLeft (20); a.removeFromLeft (8);
            drawBar (g, mBar, momentary, kLoudTop, kLoudBottom, false, juce::Colour (0xff59a9e0));
            drawBar (g, sBar, shortTerm, kLoudTop, kLoudBottom, false, juce::Colour (0xff3fbf6a));
            if (! none (playedI)) { marker (g, mBar, playedI, kLoudTop, kLoudBottom, juce::Colour (0xffffc83d), 2); marker (g, sBar, playedI, kLoudTop, kLoudBottom, juce::Colour (0xffffc83d), 2); }
            drawScale (g, scale, mBar.getY(), mBar.getHeight(), kLoudTop, kLoudBottom, { 0, -6, -12, -18, -23, -30, -36, -42, -48 }, true);
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("M", mBar.getX(), nums.getY() + 16, mBar.getWidth(), 12, juce::Justification::centred, false);
            g.drawText ("S", sBar.getX(), nums.getY() + 16, sBar.getWidth(), 12, juce::Justification::centred, false);
            auto line = [&] (int row, const juce::String& name, const juce::String& value, juce::Colour c)
            {
                const int y = a.getY() + 2 + row * 30;
                g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold)); g.drawText (name, a.getX(), y, a.getWidth(), 11, juce::Justification::centredLeft, false);
                g.setColour (c); g.setFont (juce::FontOptions (15.0f, juce::Font::bold)); g.drawText (value, a.getX(), y + 11, a.getWidth(), 17, juce::Justification::centredLeft, false);
            };
            line (0, "MOMENTARY", none (momentary) ? juce::String ("-") : juce::String (momentary, 1), juce::Colour (0xff59a9e0));
            line (1, "SHORT-TERM", none (shortTerm) ? juce::String ("-") : juce::String (shortTerm, 1), juce::Colour (0xff3fbf6a));
            if (a.getHeight() >= 96) line (2, "INTEGRATED", none (playedI) ? juce::String ("-") : juce::String (playedI, 1), juce::Colour (0xffffc83d));
            if (a.getHeight() >= 126) line (3, "RANGE (LRA)", playedLraOk ? juce::String (playedLra, 1) + " LU" : juce::String ("-"), theme::text);
            tips.push_back ({ a.getUnion (nums).getUnion (mBar).getUnion (scale), "Loudness at the playhead (ITU-R BS.1770): M, the last 400 ms; S, the last 3 s. INTEGRATED and RANGE are of what has played since "
                                                                                  "Play was pressed (the yellow lines on the bars) - the whole piece's are in the figures on the right." });
        }
        // 4: the correlation and the vectorscope
        {
            const int side = juce::jlimit (60, 190, h - 46);
            auto a = r.removeFromLeft (side + 8); r.removeFromLeft (10);
            head (g, a, "STEREO");
            auto cm = a.removeFromBottom (26);
            auto sq = juce::Rectangle<int> (a.getX(), a.getY() + 2, juce::jmin (a.getWidth(), a.getHeight() - 4), juce::jmin (a.getWidth(), a.getHeight() - 4));
            g.setColour (theme::wellDark); theme::fillBox (g, sq);
            g.setColour (theme::grid);
            g.drawLine ((float) sq.getX(), (float) sq.getY(), (float) sq.getRight(), (float) sq.getBottom(), 1.0f); g.drawLine ((float) sq.getRight(), (float) sq.getY(), (float) sq.getX(), (float) sq.getBottom(), 1.0f);
            g.fillRect (sq.getCentreX(), sq.getY(), 1, sq.getHeight()); g.fillRect (sq.getX(), sq.getCentreY(), sq.getWidth(), 1);
            {   // every dot: up = the same in both (mono), across = out of phase; turned so that left leans left
                juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (sq.reduced (1));
                const float cx = (float) sq.getCentreX(), cy = (float) sq.getCentreY(), k = (float) sq.getWidth() * 0.5f * scopeGain;
                juce::RectangleList<float> dots;
                for (size_t i = 0; i < scopeL.size(); ++i)
                {
                    const float m = (scopeL[i] + scopeR[i]) * 0.7071f, sd = (scopeR[i] - scopeL[i]) * 0.7071f;
                    dots.addWithoutMerging ({ cx + sd * k - 0.5f, cy - m * k - 0.5f, 1.3f, 1.3f });
                }
                g.setColour (juce::Colour (0xff7fe0a8).withAlpha (0.75f)); g.fillRectList (dots);
            }
            g.setColour (theme::border); theme::drawBox (g, sq);
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("L", sq.getX() + 3, sq.getY() + 2, 12, 11, juce::Justification::centredLeft, false); g.drawText ("R", sq.getRight() - 14, sq.getY() + 2, 12, 11, juce::Justification::centredRight, false);
            // the correlation: -1 .. 0 .. +1
            auto cb = juce::Rectangle<int> (sq.getX(), cm.getY() + 4, sq.getWidth(), 10).toFloat();
            g.setColour (theme::wellDark); theme::fillBox (g, cb, 3.0f);
            const float zero = cb.getCentreX(), pos = zero + (float) corr * cb.getWidth() * 0.5f;
            g.setColour (corr < 0.0 ? juce::Colour (0xffe8563f) : juce::Colour (0xff3fbf6a));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (zero, pos), cb.getY() + 2.0f, juce::jmax (zero, pos), cb.getBottom() - 2.0f));
            g.setColour (theme::border); theme::drawBox (g, cb, 1.0f, 3.0f);
            g.setColour (theme::text.withAlpha (0.6f)); g.fillRect (zero - 0.5f, cb.getY(), 1.0f, cb.getHeight());
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("-1", (int) cb.getX(), (int) cb.getBottom(), 16, 11, juce::Justification::centredLeft, false);
            g.drawText ("+1", (int) cb.getRight() - 16, (int) cb.getBottom(), 16, 11, juce::Justification::centredRight, false);
            g.drawText (juce::String (corr, 2), (int) zero - 20, (int) cb.getBottom(), 40, 11, juce::Justification::centred, false);
            tips.push_back ({ a.getUnion (cm), "The stereo picture at the playhead: a line straight up is mono, a wide cloud is wide stereo, a line across is out of phase. Under it the correlation "
                                               "of left and right over the last 300 ms: +1 the same, 0 nothing in common, below 0 (red) parts cancel in mono." });
        }
        // 5: the spectrum, in third octaves
        if (r.getWidth() > 120)
        {
            auto a = r;
            head (g, a, "SPECTRUM  (third octaves, left + right)");
            auto labels = a.removeFromBottom (14);
            g.setColour (theme::wellDark); theme::fillBox (g, a);
            const int n = (int) spec.size();
            if (n > 0)
            {
                const float bw = (float) (a.getWidth() - 4) / (float) n;
                auto yOf = [&] (float db) { return (float) a.getY() + 2.0f + (kSpecTop - juce::jlimit (kSpecBottom, kSpecTop, db)) / (kSpecTop - kSpecBottom) * (float) (a.getHeight() - 4); };
                g.setFont (juce::FontOptions (9.5f));
                for (float db = -20.0f; db > kSpecBottom; db -= 20.0f)
                {
                    g.setColour (theme::grid.withAlpha (0.7f)); g.fillRect ((float) a.getX() + 2.0f, yOf (db), (float) a.getWidth() - 4.0f, 1.0f);
                    g.setColour (theme::dimText.withAlpha (0.8f)); g.drawText (juce::String ((int) db), a.getRight() - 30, (int) yOf (db) - 11, 26, 11, juce::Justification::centredRight, false);
                }
                for (int i = 0; i < n; ++i)
                {
                    const float x = (float) a.getX() + 2.0f + bw * (float) i;
                    const float y = yOf (spec[(size_t) i]);
                    g.setColour (juce::Colour (0xff59a9e0)); g.fillRect (x + 0.5f, y, juce::jmax (1.0f, bw - 1.0f), (float) a.getBottom() - 2.0f - y);
                    if (i < (int) specHold.size()) { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (x + 0.5f, yOf (specHold[(size_t) i]), juce::jmax (1.0f, bw - 1.0f), 1.0f); }
                }
                g.setColour (theme::dimText); g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                for (int i = 0; i < n; ++i)
                {
                    const double f = bandHz (i);
                    for (double mark : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 })
                        if (std::abs (f / mark - 1.0) < 0.03)
                            g.drawText (mark >= 1000.0 ? juce::String ((int) (mark / 1000.0)) + "k" : juce::String (mark, mark < 100.0 && mark != std::floor (mark) ? 1 : 0),
                                        (int) ((float) a.getX() + 2.0f + bw * ((float) i + 0.5f)) - 20, labels.getY() + 1, 40, 12, juce::Justification::centred, false);
                }
            }
            g.setColour (theme::border); theme::drawBox (g, a);
            tips.push_back ({ a, "The spectrum at the playhead: the level in each third of an octave (left and right together, the last 170 ms), with a line where each band last peaked." });
        }
    }
    juce::String getTooltip() override { const auto p = getMouseXYRelative(); for (auto& t : tips) if (t.area.contains (p)) return t.tip; return {}; }
    void mouseDown (const juce::MouseEvent&) override { for (auto& hmax : tpMax) hmax = 0.0f; specHold.clear(); repaint(); }       // a click: the holds start again

private:
    static constexpr double kPeakTop = 3.0, kPeakBottom = -60.0, kLoudTop = 0.0, kLoudBottom = -54.0;
    static constexpr float kSpecTop = 0.0f, kSpecBottom = -96.0f;
    static constexpr int kFft = 8192, kBands = 31;                     // 20 Hz .. 20 kHz
    static double bandHz (int i) { return 1000.0 * std::pow (2.0, (double) (i - 17) / 3.0); }

    static void head (juce::Graphics& g, juce::Rectangle<int>& a, const juce::String& t)
    {
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (t, a.removeFromTop (14), juce::Justification::centredLeft, true);
        a.removeFromTop (2);
    }
    static float fracOf (double db, double top, double bottom) { return (float) ((juce::jlimit (bottom, top, db) - bottom) / (top - bottom)); }
    /** A bar from the bottom of its well up to 'db'. peakColours: green, yellow over -6, red over 0. */
    static void drawBar (juce::Graphics& g, juce::Rectangle<int> well, double db, double top, double bottom, bool peakColours, juce::Colour plain = juce::Colour (0xff3fbf6a))
    {
        g.setColour (theme::wellDark); theme::fillBox (g, well, 3.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path shape; shape.addRoundedRectangle (well.toFloat(), theme::cornerFor (well.toFloat(), 3.0f)); g.reduceClipRegion (shape);
            const auto in = well.toFloat();
            auto part = [&] (double from, double to, juce::Colour c)
            {
                const double hi = juce::jmin (to, db);
                if (none (db) || hi <= from) return;
                const float y1 = in.getBottom() - fracOf (from, top, bottom) * in.getHeight(), y0 = in.getBottom() - fracOf (hi, top, bottom) * in.getHeight();
                g.setColour (c); g.fillRect (in.getX(), y0, in.getWidth(), y1 - y0);
            };
            if (peakColours) { part (bottom, -6.0, juce::Colour (0xff3fbf6a)); part (-6.0, 0.0, juce::Colour (0xffe0a800)); part (0.0, top, juce::Colour (0xffe53935)); }
            else part (bottom, top, plain);
        }
        g.setColour (theme::border); theme::drawBox (g, well, 1.0f, 3.0f);
    }
    static void marker (juce::Graphics& g, juce::Rectangle<int> well, double db, double top, double bottom, juce::Colour c, int thick)
    {
        if (none (db) || db < bottom) return;
        const float y = (float) well.getBottom() - fracOf (db, top, bottom) * (float) well.getHeight();
        g.setColour (c); g.fillRect ((float) well.getX() + 1.0f, juce::jlimit ((float) well.getY(), (float) well.getBottom() - (float) thick, y - (float) thick * 0.5f), (float) well.getWidth() - 2.0f, (float) thick);
    }
    static void drawScale (juce::Graphics& g, juce::Rectangle<int> a, int y0, int h, double top, double bottom, std::initializer_list<int> marks, bool rightAligned = false)
    {
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (9.5f));
        for (int m : marks)
        {
            const int y = y0 + h - (int) std::lround (fracOf ((double) m, top, bottom) * (float) h);
            g.drawText (juce::String (m), a.getX(), juce::jlimit (y0, y0 + h - 10, y - 5), a.getWidth() - 2, 10, rightAligned ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }
    }
    /** The file's samples just before the playhead: the vectorscope's dots and the spectrum. */
    void readScope (double seconds)
    {
        scopeL.clear(); scopeR.clear();
        if (profile == nullptr || samples == nullptr || samples->reader == nullptr) { spec.clear(); return; }
        const auto& p = *profile;
        // (playing: what was just heard; standing still: the stretch around the playhead)
        const auto end = (juce::int64) std::floor (seconds * p.rate) + (wasPlayingNow ? 0 : kFft / 2);
        const int nc = juce::jmax (1, p.channels);
        juce::AudioBuffer<float> buf (nc, kFft);
        if (! samples->read (end - kFft, kFft, buf.getArrayOfWritePointers(), nc)) { spec.clear(); return; }
        const float* l = buf.getReadPointer (0); const float* rr = buf.getReadPointer (nc > 1 ? 1 : 0);
        const int dots = 1536;
        const int dot0 = wasPlayingNow ? kFft - dots : (kFft - dots) / 2;
        scopeL.assign (l + dot0, l + dot0 + dots); scopeR.assign (rr + dot0, rr + dot0 + dots);
        float pk = 1.0e-4f; for (int i = 0; i < dots; ++i) pk = juce::jmax (pk, std::abs (scopeL[(size_t) i]), std::abs (scopeR[(size_t) i]));
        const float want = juce::jlimit (1.0f, 60.0f, 0.7f / pk);                              // the picture fills its square, whatever the level
        scopeGain = scopeGain <= 0.0f ? want : scopeGain + (want - scopeGain) * 0.15f;
        // the spectrum: both channels' power, in third octaves
        static const dsp::FastFFT fft (kFft); static const auto window = dsp::hann (kFft);
        std::vector<double> power ((size_t) kFft / 2 + 1, 0.0);
        std::vector<std::complex<double>> x ((size_t) kFft);
        for (int c = 0; c < juce::jmin (2, nc); ++c)
        {
            const float* s = buf.getReadPointer (c);
            for (int i = 0; i < kFft; ++i) x[(size_t) i] = { (double) s[i] * window[(size_t) i], 0.0 };
            fft.forward (x);
            for (int k = 0; k <= kFft / 2; ++k) power[(size_t) k] += std::norm (x[(size_t) k]);
        }
        // (a full-scale sine reads 0 dB in its band: the Hann window halves the amplitude, and the two halves of the spectrum share the power)
        const double norm = 1.0 / ((double) kFft * (double) kFft / 16.0);
        spec.assign ((size_t) kBands, kSpecBottom);
        for (int b = 0; b < kBands; ++b)
        {
            const double f0 = bandHz (b) * std::pow (2.0, -1.0 / 6.0), f1 = bandHz (b) * std::pow (2.0, 1.0 / 6.0);
            if (f0 >= p.rate * 0.5) continue;
            const double k0 = f0 * (double) kFft / p.rate, k1 = juce::jmin (f1 * (double) kFft / p.rate, (double) kFft / 2.0);
            double sum = 0.0;
            for (int k = (int) std::floor (k0); k <= (int) std::floor (k1) && k <= kFft / 2; ++k)
            {
                const double lo = juce::jmax (k0, (double) k), hi = juce::jmin (k1, (double) k + 1.0);       // (the share of this line that lies in the band)
                if (hi > lo) sum += power[(size_t) k] * (hi - lo);
            }
            spec[(size_t) b] = (float) juce::jmax ((double) kSpecBottom, wavestats::powerToDb (sum * norm));
        }
        if (specHold.size() != spec.size()) specHold = spec;
        for (size_t i = 0; i < spec.size(); ++i) specHold[i] = juce::jmax (spec[i], specHold[i] - (wasPlayingNow ? 0.25f : 200.0f));
    }

    struct Bar { double tp = wavestats::kNone, sp = wavestats::kNone, avg = wavestats::kNone, hold = wavestats::kNone; juce::uint32 holdMs = 0; };
    struct Tip { juce::Rectangle<int> area; juce::String tip; };
    ProfilePtr profile;
    std::array<Bar, 2> bars;
    std::array<float, 2> tpMax { { 0.0f, 0.0f } };
    double momentary = wavestats::kNone, shortTerm = wavestats::kNone, corr = 0.0, playedI = wavestats::kNone, playedLra = 0.0;
    bool playedLraOk = false, wasPlaying = false, wasPlayingNow = false;
    juce::int64 lastTick = -1, playedFrom = -1;
    juce::uint32 lastMs = 0, playedMs = 0;
    std::vector<float> scopeL, scopeR, spec, specHold;
    float scopeGain = 0.0f;
    std::vector<Tip> tips;
};
} // namespace

//==================================================================================================================================
struct SpecEditComponent::Impl
{
    Impl (SpecEditComponent& o, AppContext& a) : owner (o), app (a) {}

    SpecEditComponent& owner;
    AppContext& app;
    juce::Uuid editId;
    // the controls
    juce::Label pieceCaption, status, timeLabel, rmsCaption, refCaption;
    juce::ComboBox pieceBox, rmsBox, refBox;
    std::vector<juce::Uuid> pieceIds;
    juce::TextButton playBtn { "Play" }, measureBtn { "Measure again" }, copyBtn { "Copy the figures" }, fitBtn { "Whole piece" };
    juce::ToggleButton autoToggle { "Keep up to date" };
    double progressValue = 0.0;
    juce::ProgressBar progress { progressValue };
    WaveView wave;
    StatsPanel stats;
    MetersPanel meters;
    // the render and its figures
    FileSamples samples;
    juce::File file; juce::Time fileTime; double fileRate = 0.0; juce::int64 fileFrames = 0;
    ProfilePtr profile;
    Stats whole;
    wavestats::Options options;
    std::unique_ptr<FileAnalysis> analysis;
    std::unique_ptr<MasterRenderJob> renderJob;
    std::unique_ptr<masterbusy::Hold> hold;
    MasterRenderParts renderingParts;
    juce::String lastFp, problem;
    bool dirty = true, needsRender = false, staleAfterRender = false, wantRender = false, updating = false;
    juce::uint32 dirtyAt = 0, lastStoreLook = 0;
    // playing
    juce::Uuid playToken;
    bool playing = false;
    double playStart = 0.0;

    EditDef* edit() const { return app.project.findEdit (editId); }
    juce::String pieceName() const { if (auto* it = app.project.mastering.findItem (editId); it != nullptr && it->fileName.isNotEmpty()) return it->fileName; if (auto* e = edit()) return e->name; return "(no piece)"; }

    void say (const juce::String& t, bool warn = false) { status.setColour (juce::Label::textColourId, warn ? theme::warn : theme::text); status.setText (t, juce::dontSendNotification); }

    void fillPieces()
    {
        updating = true;
        app.project.mastering.sync (app.project.edits);
        pieceBox.clear (juce::dontSendNotification); pieceIds.clear();
        int sel = 0, i = 0;
        for (auto& it : app.project.mastering.items)
        {
            auto* e = app.project.findEdit (it.editId);
            if (e == nullptr || e->isEmpty()) continue;
            pieceBox.addItem (it.fileName.isNotEmpty() ? it.fileName : e->name, ++i); pieceIds.push_back (it.editId);
            if (it.editId == editId) sel = i;
        }
        if (sel > 0) pieceBox.setSelectedId (sel, juce::dontSendNotification);
        else pieceBox.setText (pieceIds.empty() ? "(no pieces with audio)" : "(choose a piece)", juce::dontSendNotification);
        updating = false;
    }

    void setPiece (const juce::Uuid& id)
    {
        if (id == editId && (profile != nullptr || analysis != nullptr || renderJob != nullptr)) { fillPieces(); return; }
        stopPlay();
        editId = id;
        analysis.reset(); profile.reset(); file = juce::File(); samples.reader.reset();
        lastFp.clear(); problem.clear(); needsRender = false; staleAfterRender = false; wantRender = false;
        wave.emptyText = "Measuring..."; wave.setProfile (nullptr, false); meters.setProfile (nullptr);
        showFigures();
        fillPieces();
        if (auto* tl = owner.getTopLevelComponent()) if (owner.isShowing()) tl->setName ("Spectrogram Editing - " + pieceName());
        dirty = false;
        lookAtPiece();
    }

    /** Looks at the piece's render (never makes one): measures it when it is new, or says what is in the way. */
    void lookAtPiece()
    {
        lastStoreLook = juce::Time::getMillisecondCounter();
        if (renderJob != nullptr) return;
        auto* e = edit();
        if (e == nullptr || e->isEmpty()) { problem = e == nullptr ? "Choose a piece (the pieces are the Mastering window's list)." : "This piece has no audio."; wave.emptyText = problem; wave.repaint(); say (problem, true); updateButtons(); return; }
        RenderParams rp;
        const auto perr = makeRenderParams (app.project, app.project.mastering, rp);
        if (perr.isNotEmpty()) { problem = perr; wave.emptyText = perr; wave.repaint(); say (perr, true); updateButtons(); return; }
        problem.clear();
        lastFp = masterRenderFingerprint (app.project, *e, rp);
        MasterRenders store (MasterRenders::folderFor (app.project));
        const auto info = store.check (editId, lastFp);
        if (info.state == MasterRenders::State::Ready || info.state == MasterRenders::State::Previous)
        {
            needsRender = false; staleAfterRender = false;
            const auto t = info.file.getLastModificationTime();
            if (info.file != file || t != fileTime || (profile == nullptr && analysis == nullptr))
            {
                const bool same = info.file.getParentDirectory() == file.getParentDirectory() && profile != nullptr;
                file = info.file; fileTime = t; fileRate = info.rate > 0.0 ? info.rate : e->sampleRate; fileFrames = info.frames;
                startAnalysis (same);
            }
            else if (analysis == nullptr) say (measuredText (info.state == MasterRenders::State::Previous));
        }
        else
        {
            needsRender = true;
            const bool never = info.state == MasterRenders::State::None;
            if (profile == nullptr) { wave.emptyText = never ? "This piece has not been rendered yet." : "This piece has changed since its render."; wave.repaint(); }
            if ((autoToggle.getToggleState() && ! staleAfterRender) || wantRender) startRender();
            else say (never ? "Not rendered yet: press Measure again." : (staleAfterRender ? "The piece changed while it was rendered: press Measure again." : "The piece has changed: these figures are of its last render. Press Measure again."), true);
        }
        updateButtons();
    }
    juce::String measuredText (bool previous) const
    {
        return "Measured: the render of " + fileTime.toString (true, true, false, true) + (previous ? "  (the PREVIOUS render is in use: Mastering's \"Use newest render\")" : juce::String())
               + (needsRender ? juce::String() : juce::String ("  -  up to date"));
    }

    void startRender()
    {
        wantRender = true;
        if (renderJob != nullptr) return;
        if (masterbusy::holds() > 0) { say ("The Mastering window is rendering or exporting: this piece is measured when it has finished."); return; }
        if (app.engine.isRecording()) { say ("Recording: the piece is measured when the recording has stopped."); return; }
        auto* e = edit(); if (e == nullptr) return;
        RenderParams rp;
        if (const auto perr = makeRenderParams (app.project, app.project.mastering, rp); perr.isNotEmpty()) { say (perr, true); wantRender = false; return; }
        auto batch = std::make_unique<MasterRenderBatch> (app.project, std::vector<juce::Uuid> { editId }, rp, false);
        if (batch->getPrepareError().isNotEmpty()) { say (batch->getPrepareError(), true); wantRender = false; return; }
        wantRender = false;
        if (batch->toRender() == 0) { needsRender = false; lookAtPiece(); return; }
        renderingParts = masterRenderParts (app.project, *e, rp);                 // (what the render is made from: see renderDone)
        hold = std::make_unique<masterbusy::Hold>();
        progressValue = 0.0;
        say ("Rendering \"" + e->name + "\" through its mixer...");
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        renderJob = std::make_unique<MasterRenderJob> (std::move (batch), [safe] (juce::String err) { if (safe != nullptr) safe->impl->renderDone (err); });
        updateButtons();
    }

    /** As the Mastering window's renderFinished: the new render takes the project's description of the piece as it is now, unless the piece was
        changed by hand while it was rendered (then it is out of date, and says so). */
    void renderDone (const juce::String& err)
    {
        renderJob.reset(); hold.reset();
        progressValue = 0.0;
        auto* e = edit();
        if (err.isNotEmpty() || e == nullptr) { say (err == "Cancelled." ? juce::String ("The render was cancelled.") : "The render stopped: " + err, true); staleAfterRender = true; updateButtons(); return; }
        RenderParams rp;
        if (makeRenderParams (app.project, app.project.mastering, rp).isEmpty())
        {
            MasterRenders store (MasterRenders::folderFor (app.project));
            auto fp = masterRenderFingerprint (app.project, *e, rp);
            if (store.check (editId, fp).state == MasterRenders::State::Stale)
            {
                learnMasterRenderVolatile (app.project, *e, rp);
                fp = masterRenderFingerprint (app.project, *e, rp);
                if (masterRenderOnlyVolatileDiffers (renderingParts, masterRenderParts (app.project, *e, rp), editId)) store.restamp (editId, fp);
                else { juce::Logger::writeToLog ("Spectrogram Editing: \"" + e->name + "\" was changed while it was rendered; its render is out of date"); staleAfterRender = true; }
            }
        }
        renderingParts.clear();
        lookAtPiece();
    }

    void startAnalysis (bool samePiece)
    {
        analysis.reset();
        samples.open (file);
        samePieceNext = samePiece;
        progressValue = 0.0;
        say ("Measuring...");
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        analysis = std::make_unique<FileAnalysis> (file, [safe] (ProfilePtr p, juce::String err) { if (safe != nullptr) safe->impl->analysisDone (std::move (p), err); });
        updateButtons();
    }
    bool samePieceNext = false;
    void analysisDone (ProfilePtr p, const juce::String& err)
    {
        analysis.reset();
        if (p == nullptr) { say (err.isNotEmpty() ? err : juce::String ("The render could not be measured."), true); updateButtons(); return; }
        const bool wasPlaying = playing;
        if (wasPlaying) stopPlay();
        profile = std::move (p);
        whole = wavestats::measure (*profile, options);
        wave.setProfile (profile, samePieceNext); meters.setProfile (profile);
        if (wave.playheadSeconds() > profile->seconds()) wave.setPlayhead (0.0);
        showFigures(); showPlayhead();
        say (measuredText (false));
        updateButtons();
    }

    wavestats::Reader reader()
    {
        const int nc = profile != nullptr ? profile->channels : 2;
        return [this, nc] (std::int64_t start, int n, float* const* dest) { return samples.read ((juce::int64) start, n, dest, nc); };
    }
    void showFigures()
    {
        if (profile == nullptr) { stats.set (nullptr, {}, nullptr, analysis != nullptr || renderJob != nullptr); return; }
        if (wave.hasSelection())
        {
            const auto rd = reader();
            const auto sel = wavestats::measure (*profile, (std::int64_t) std::llround (wave.selStart() * profile->rate), (std::int64_t) std::llround (wave.selEnd() * profile->rate), options, &rd);
            stats.set (profile, whole, &sel, false);
        }
        else stats.set (profile, whole, nullptr, false);
    }
    void optionsChanged()
    {
        if (updating) return;
        static const int windows[] = { 10, 20, 50, 100, 300, 400, 1000, 3000 };
        options.rmsWindowMs = windows[juce::jlimit (0, 7, rmsBox.getSelectedId() - 1)];
        options.aes17 = refBox.getSelectedId() != 2;
        meters.aes17 = options.aes17;
        if (auto* st = app.props.getUserSettings()) { st->setValue ("specEditRmsMs", options.rmsWindowMs); st->setValue ("specEditAes17", options.aes17); app.props.saveIfNeeded(); }
        if (profile != nullptr) whole = wavestats::measure (*profile, options);
        showFigures(); showPlayhead();
    }

    // ---- playing: the render itself, straight to the outputs (as the Mastering window's disc audition)
    int outFirst() const
    {
        auto& p = app.project;
        if (p.mixers.empty()) return 0;
        const auto& pm = *p.mixers.front();
        auto outOf = [&] (const juce::Uuid& busId) { for (auto& b : pm.busPool) if (b->busId == busId) return b->outFirst.load(); return -1; };
        if (p.kindOf (p.mastering.sourceId) == NodeKind::ExtBus) { const int f = outOf (p.mastering.sourceId); if (f >= 0) return f; }
        for (auto& b : p.buses) if (b.external) { const int f = outOf (b.id); if (f >= 0) return f; }
        return 0;
    }
    bool mine() const { return app.isPlaying() && app.playInfo.kind == AppContext::PlayInfo::Kind::Other && app.playInfo.id == playToken; }
    void startPlay()
    {
        if (profile == nullptr || ! file.existsAsFile()) return;
        if (app.engine.isRecording()) { showError ("Spectrogram Editing", "Stop recording first."); return; }
        if (playing) { if (mine()) app.stopPlayback(); playing = false; }
        const bool sel = wave.hasSelection();
        double from = sel ? wave.selStart() : wave.playheadSeconds();
        const double to = sel ? wave.selEnd() : -1.0;
        if (! sel && from >= profile->seconds() - 0.05) from = 0.0;
        playToken = juce::Uuid();
        const auto err = app.playRenders ({ { file, 0.0, profile->rate, profile->frames } }, from, to, playToken, outFirst());
        if (err.isNotEmpty()) { say (err, true); return; }
        playing = true; playStart = from;
        playBtn.setButtonText ("Stop");
    }
    void stopPlay()
    {
        if (! playing) return;
        const bool m = mine();
        const double pos = m ? app.playheadSeconds() : -1.0;
        if (m) app.stopPlayback();
        playing = false;
        playBtn.setButtonText ("Play");
        wave.setPlayhead (app.playheadFollows && pos >= 0.0 && ! wave.hasSelection() ? pos : playStart);
        showPlayhead();
    }
    void pollPlay()
    {
        if (! playing) return;
        if (mine() && ! app.engine.playbackFinished())
        {
            const double pos = app.playheadSecondsSmooth();
            wave.setPlayhead (pos); wave.follow (pos);
            meters.setPlayhead (pos, true);
            timeLabel.setText (clockText (pos), juce::dontSendNotification);
            return;
        }
        const double last = wave.playheadSeconds();
        if (mine()) app.stopPlayback();
        playing = false;
        playBtn.setButtonText ("Play");
        wave.setPlayhead (app.playheadFollows && ! wave.hasSelection() ? last : playStart);
        showPlayhead();
    }
    /** The playhead standing still: the time, and the meters show what is under it. */
    void showPlayhead()
    {
        timeLabel.setText (clockText (wave.playheadSeconds()), juce::dontSendNotification);
        if (! playing) meters.setPlayhead (wave.playheadSeconds(), false);
    }

    void updateButtons()
    {
        const bool busy = renderJob != nullptr || analysis != nullptr;
        progress.setVisible (busy);
        measureBtn.setEnabled (! busy && edit() != nullptr && problem.isEmpty());
        playBtn.setEnabled (profile != nullptr);
        copyBtn.setEnabled (profile != nullptr);
        owner.resized();
    }

    void tick()
    {
        pollPlay();
        if (renderJob != nullptr) { progressValue = renderJob->getProgress(); return; }
        if (analysis != nullptr) { progressValue = analysis->progress(); return; }
        if (! owner.isShowing()) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (wantRender && now - lastStoreLook > 700) { lastStoreLook = now; startRender(); return; }                        // (it was waiting for the Mastering window, or a recording)
        if (dirty && now - dirtyAt > 1200 && ! app.engine.isRecording())
        {
            dirty = false;
            if (auto* e = edit())
            {
                RenderParams rp;
                const auto perr = makeRenderParams (app.project, app.project.mastering, rp);
                if (perr != problem || (perr.isEmpty() && masterRenderFingerprint (app.project, *e, rp) != lastFp)) { staleAfterRender = false; lookAtPiece(); }
            }
            else if (! editId.isNull()) { setPiece (juce::Uuid::null()); return; }              // (the piece has gone from the project)
            if (! pieceBox.isPopupActive()) fillPieces();
            return;
        }
        if (now - lastStoreLook > 2500 && masterbusy::holds() == 0 && problem.isEmpty() && edit() != nullptr)                   // (the Mastering window may have rendered it meanwhile)
        {
            lastStoreLook = now;
            MasterRenders store (MasterRenders::folderFor (app.project));
            const auto info = store.check (editId, lastFp);
            const bool ready = info.state == MasterRenders::State::Ready || info.state == MasterRenders::State::Previous;
            if (ready && (info.file != file || info.file.getLastModificationTime() != fileTime || needsRender)) lookAtPiece();
        }
    }
};

//==================================================================================================================================
SpecEditComponent::SpecEditComponent (AppContext& a, const juce::Uuid& editId) : app (a), impl (std::make_unique<Impl> (*this, a))
{
    auto& m = *impl;
    auto cap = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification); l.setFont (juce::FontOptions (12.0f)); l.setColour (juce::Label::textColourId, theme::dimText);
        l.setJustificationType (juce::Justification::centredRight); addAndMakeVisible (l);
    };
    cap (m.pieceCaption, "Piece"); cap (m.rmsCaption, "RMS windows"); cap (m.refCaption, "RMS 0 dB =");
    addAndMakeVisible (m.pieceBox);
    m.pieceBox.setTooltip ("The piece that is measured: one of the Mastering window's pieces. What is measured is its render - the piece through its mixer, as the Mastering window plays and exports it "
                           "(before the export's own level and dither).");
    m.pieceBox.onChange = [this] { auto& i = *impl; if (i.updating) return; const int k = i.pieceBox.getSelectedId() - 1; if (juce::isPositiveAndBelow (k, (int) i.pieceIds.size())) i.setPiece (i.pieceIds[(size_t) k]); };
    addAndMakeVisible (m.playBtn);
    m.playBtn.setTooltip ("Plays the render from the playhead - or the selected part - straight to the outputs, as it is (Space). The meters follow it.");
    m.playBtn.onClick = [this] { if (impl->playing) impl->stopPlay(); else impl->startPlay(); };
    m.timeLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold)); m.timeLabel.setJustificationType (juce::Justification::centred);
    m.timeLabel.setColour (juce::Label::backgroundColourId, theme::field); m.timeLabel.setColour (juce::Label::outlineColourId, theme::border);
    addAndMakeVisible (m.timeLabel);
    m.status.setFont (juce::FontOptions (13.0f)); m.status.setMinimumHorizontalScale (1.0f); addAndMakeVisible (m.status);
    fit::markMayShrink (m.status);
    addChildComponent (m.progress);
    addAndMakeVisible (m.measureBtn);
    m.measureBtn.setTooltip ("Renders the piece through its mixer again (if it has changed) and measures the new render.");
    m.measureBtn.onClick = [this] { impl->staleAfterRender = false; impl->wantRender = true; impl->lookAtPiece(); };
    addAndMakeVisible (m.autoToggle);
    m.autoToggle.setTooltip ("On: when the piece changes - an edit, a fader, a plug-in - its render is made again and measured again by itself, a moment after the change. "
                             "Off: only when Measure again is pressed (the figures then say they are of the last render).");
    addAndMakeVisible (m.fitBtn);
    m.fitBtn.setTooltip ("Shows the whole piece again (the mouse wheel zooms in and out where the mouse is; Shift + wheel moves along; double-click: the whole piece).");
    m.fitBtn.onClick = [this] { impl->wave.zoomToAll(); };
    addAndMakeVisible (m.copyBtn);
    m.copyBtn.setTooltip ("Copies the figures - the whole piece's and the selection's - as text, to paste into a note or an e-mail.");
    m.copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (impl->stats.asText (impl->pieceName())); impl->say ("The figures were copied."); };
    addAndMakeVisible (m.rmsBox); addAndMakeVisible (m.refBox);
    { int i = 0; for (auto* t : { "10 ms", "20 ms", "50 ms", "100 ms", "300 ms", "400 ms", "1 s", "3 s" }) m.rmsBox.addItem (t, ++i); }
    m.rmsBox.setTooltip ("How long the windows of Max. and Min. RMS level are (a new window every 10 ms).");
    m.refBox.addItem ("a full-scale sine (AES17)", 1); m.refBox.addItem ("a full-scale square wave", 2);
    m.refBox.setTooltip ("What an RMS level of 0 dB is. AES17: a sine wave that just reaches full scale reads 0 dB (3 dB more than the plain RMS). The other: a square wave at full scale reads 0 dB. "
                         "Set it as your other meter has it (iZotope RX has the same choice in its preferences).");
    {
        auto* st = app.props.getUserSettings();
        m.updating = true;
        m.options.rmsWindowMs = st != nullptr ? st->getIntValue ("specEditRmsMs", 50) : 50;
        m.options.aes17 = st == nullptr || st->getBoolValue ("specEditAes17", true);
        static const int windows[] = { 10, 20, 50, 100, 300, 400, 1000, 3000 };
        int pick = 3; for (int i = 0; i < 8; ++i) if (windows[i] == m.options.rmsWindowMs) pick = i + 1;
        m.rmsBox.setSelectedId (pick, juce::dontSendNotification); m.refBox.setSelectedId (m.options.aes17 ? 1 : 2, juce::dontSendNotification);
        m.meters.aes17 = m.options.aes17;
        m.autoToggle.setToggleState (st == nullptr || st->getBoolValue ("specEditAuto", true), juce::dontSendNotification);
        m.updating = false;
    }
    m.rmsBox.onChange = [this] { impl->optionsChanged(); }; m.refBox.onChange = [this] { impl->optionsChanged(); };
    m.autoToggle.onClick = [this]
    {
        if (auto* st = app.props.getUserSettings()) { st->setValue ("specEditAuto", impl->autoToggle.getToggleState()); app.props.saveIfNeeded(); }
        if (impl->autoToggle.getToggleState() && impl->needsRender) { impl->staleAfterRender = false; impl->lookAtPiece(); }
    };

    addAndMakeVisible (m.wave); addAndMakeVisible (m.stats); addAndMakeVisible (m.meters);
    m.wave.samples = &m.samples; m.meters.samples = &m.samples;
    m.wave.onPlayhead = [this] (double s)
    {
        auto& i = *impl;
        if (i.playing) { i.wave.setPlayhead (s); i.startPlay(); }           // (a click while it plays: it carries on from there)
        else { i.wave.setPlayhead (s); i.showPlayhead(); }
    };
    m.wave.onSelection = [this] { impl->showFigures(); };
    m.stats.onGoTo = [this] (juce::int64 frame)
    {
        auto& i = *impl;
        if (i.profile == nullptr || frame < 0) return;
        const double s = (double) frame / i.profile->rate;
        i.wave.setPlayhead (s); i.wave.follow (s);
        if (i.playing) i.startPlay(); else i.showPlayhead();
    };

    app.project.addChangeListener (this);
    setWantsKeyboardFocus (true);
    keysStayWithWindow (*this);
    setSize (1280, 760);
    startTimerHz (30);
    m.fillPieces();
    m.setPiece (editId);
}

SpecEditComponent::~SpecEditComponent()
{
    app.project.removeChangeListener (this);
    stopTimer();
    impl->stopPlay();
    impl->analysis.reset();
    impl->renderJob.reset();
    impl->hold.reset();
}

void SpecEditComponent::setPiece (const juce::Uuid& editId) { impl->setPiece (editId); }
void SpecEditComponent::timerCallback() { impl->tick(); }
void SpecEditComponent::changeListenerCallback (juce::ChangeBroadcaster*) { impl->dirty = true; impl->dirtyAt = juce::Time::getMillisecondCounter(); }

void SpecEditComponent::paint (juce::Graphics& g) { g.fillAll (theme::window); }

void SpecEditComponent::resized()
{
    auto& m = *impl;
    auto r = getLocalBounds().reduced (8, 6);
    {   // the top line
        auto top = r.removeFromTop (30); r.removeFromTop (6);
        top.removeFromRight (grid::cornerReserve);                              // (the pin and sun / moon buttons)
        m.pieceCaption.setBounds (top.removeFromLeft (40)); top.removeFromLeft (6);
        m.pieceBox.setBounds (top.removeFromLeft (juce::jlimit (150, 260, top.getWidth() / 4)).withSizeKeepingCentre (juce::jlimit (150, 260, top.getWidth() / 4), grid::btnH)); top.removeFromLeft (10);
        m.playBtn.setBounds (top.removeFromLeft (64).withSizeKeepingCentre (64, grid::btnH + 2)); top.removeFromLeft (6);
        m.timeLabel.setBounds (top.removeFromLeft (116).withSizeKeepingCentre (116, grid::btnH + 2)); top.removeFromLeft (10);
        m.autoToggle.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, grid::btnH)); top.removeFromRight (6);
        m.measureBtn.setBounds (top.removeFromRight (120).withSizeKeepingCentre (120, grid::btnH)); top.removeFromRight (8);
        if (m.progress.isVisible()) { m.progress.setBounds (top.removeFromRight (juce::jmin (150, top.getWidth() / 3)).withSizeKeepingCentre (juce::jmin (150, top.getWidth() / 3), 16)); top.removeFromRight (8); }
        m.status.setBounds (top);
    }
    const int statsW = juce::jlimit (300, 380, getWidth() * 28 / 100);
    {   // the figures, down the right
        auto right = r.removeFromRight (statsW); r.removeFromRight (8);
        auto opts = right.removeFromBottom (60); right.removeFromBottom (6);
        m.stats.setBounds (right);
        auto a = opts.removeFromTop (27), b = opts.removeFromTop (27);
        m.rmsCaption.setBounds (a.removeFromLeft (84)); a.removeFromLeft (6);
        m.rmsBox.setBounds (a.removeFromLeft (84).withSizeKeepingCentre (84, grid::btnH)); a.removeFromLeft (8);
        m.copyBtn.setBounds (a.withSizeKeepingCentre (a.getWidth(), grid::btnH));
        m.refCaption.setBounds (b.removeFromLeft (84)); b.removeFromLeft (6);
        m.refBox.setBounds (b.withSizeKeepingCentre (b.getWidth(), grid::btnH));
    }
    const int metersH = juce::jlimit (150, 250, r.getHeight() * 38 / 100);
    m.meters.setBounds (r.removeFromBottom (metersH)); r.removeFromBottom (6);
    m.wave.setBounds (r);
    m.fitBtn.setBounds (r.getRight() - 104, r.getY() + 22, 98, 20);
    m.fitBtn.toFront (false);
}

bool SpecEditComponent::keyPressed (const juce::KeyPress& k)
{
    auto& m = *impl;
    if (k == juce::KeyPress::spaceKey) { if (m.playing) m.stopPlay(); else m.startPlay(); return true; }
    if (k == juce::KeyPress::escapeKey) { if (m.wave.hasSelection()) { m.wave.clearSelection(); return true; } return false; }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0)) { m.wave.selectAll(); return true; }
    if (k == juce::KeyPress::homeKey) { m.wave.setPlayhead (0.0); m.wave.follow (0.0); if (m.playing) m.startPlay(); else m.showPlayhead(); return true; }
    return false;
}
} // namespace td
