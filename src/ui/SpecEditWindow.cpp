#include "SpecEditWindow.h"
#include "Uikit.h"
#include "FixToolsCommon.h"           // 3.75.0: the spectrogram view, the track readers and the repair jobs (ReTouch's Gain)
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
    /** 3.75.0: the selection set from outside (the spectrogram's box: its stretch of time), without a word to onSelection; b <= a: none. */
    void setSelection (double a, double b) { if (b > a) { selA = a; selB = b; } else selA = selB = -1.0; repaint(); }
    /** 3.75.0: the highest peaks, as small flags along the ruler ('strong': the one picked), and the part the spectrogram shows, as a bracket. */
    void setMarks (std::vector<double> times, int strong) { marks = std::move (times); strongMark = strong; repaint(); }
    void setBracket (double a, double b) { if (std::abs (a - brA) < 1.0e-9 && std::abs (b - brB) < 1.0e-9) return; brA = a; brB = b; repaint(); }
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
            if (loud.getHeight() >= 60)                                           // (3.75.0: a low lane keeps its room for the lines)
            {
                g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText ("LOUDNESS (LUFS):  short-term, momentary (faint), integrated (dashes)", 6, loud.getY() + 2, getWidth() - 44, 12, juce::Justification::centredLeft, true);
            }
        }
        // the selection and the playhead
        if (hasSelection())
        {
            const int a = xOf (selA), b = juce::jmax (a + 1, xOf (selB));
            g.setColour (theme::accent.withAlpha (0.22f)); g.fillRect (a, ruler.getBottom(), b - a, waves.getHeight() + loud.getHeight());
            g.setColour (theme::accent); g.fillRect (a, ruler.getY(), 1, getHeight() - kBarH - ruler.getY()); g.fillRect (b - 1, ruler.getY(), 1, getHeight() - kBarH - ruler.getY());
            g.fillRect (a, ruler.getBottom() - 3, b - a, 3);
        }
        // 3.75.0: the part the spectrogram shows (when it is not everything), and the highest peaks
        if (brB > brA && (brA > viewStart + 1.0e-6 || brB < viewStart + viewLen - 1.0e-6))
        {
            const int a = xOf (brA), b = juce::jmax (a + 2, xOf (brB));
            g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (a, ruler.getBottom(), b - a, waves.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.drawRect (a, ruler.getBottom(), b - a, waves.getHeight(), 1);
        }
        for (size_t i = 0; i < marks.size(); ++i)
        {
            if (marks[i] < viewStart || marks[i] > viewStart + viewLen) continue;
            const float x = (float) xOf (marks[i]) + 0.5f; const bool strong = (int) i == strongMark;
            juce::Path flag; flag.addTriangle (x - 4.5f, (float) ruler.getBottom() - 9.0f, x + 4.5f, (float) ruler.getBottom() - 9.0f, x, (float) ruler.getBottom());
            g.setColour (juce::Colour (0xffffd24a).withAlpha (strong ? 1.0f : 0.8f)); g.fillPath (flag);
            g.setColour (juce::Colour (0xffffd24a).withAlpha (strong ? 0.9f : 0.35f)); g.fillRect (x - 0.5f, (float) ruler.getBottom(), 1.0f, (float) waves.getHeight());
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
    int loudH() const { const int h = getHeight() - kRulerH - kBarH; return h >= 150 ? juce::jmin (86, h / 3) : h >= 96 ? h * 2 / 5 : 0; }      // (3.75.0: still there on a low waveform)
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
    std::vector<double> marks; int strongMark = -1; double brA = 0.0, brB = 0.0;        // 3.75.0
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

//==================================================================================================================================
// 3.75.0: the spectrogram of the render, its highest true peaks, and what brings them down
using fixtools::SpectrogramView;
using fixtools::PictureRequest;

/** One channel of a file over [start, start + n): silence outside the file. */
void readChannel (juce::AudioFormatReader& r, int c, juce::int64 start, int n, float* dst)
{
    std::fill (dst, dst + n, 0.0f);
    const juce::int64 a = juce::jmax<juce::int64> (0, start), b = juce::jmin<juce::int64> (r.lengthInSamples, start + n);
    if (b <= a || c < 0 || c >= (int) r.numChannels) return;
    std::vector<float*> chans ((size_t) r.numChannels, nullptr);
    chans[(size_t) c] = dst + (a - start);
    r.read (chans.data(), (int) chans.size(), a, (int) (b - a));
}

/** The render's quick picture - every channel of it in one, the louder of them at each point (SpectralPyramid) - built on a thread of its own.
    'done' comes back on the message thread (not at all once this has gone). */
class PictureBuild : private juce::Thread
{
public:
    using Done = std::function<void (std::shared_ptr<SpectralPyramid>, juce::String)>;
    PictureBuild (const juce::File& f, Done d) : juce::Thread ("fermata render picture"), file (f), done (std::move (d)) { startThread(); }
    ~PictureBuild() override { alive->store (false); stopThread (10000); }

private:
    void run() override
    {
        std::unique_ptr<juce::AudioFormatReader> r (fixtools::formats().createReaderFor (file));
        std::shared_ptr<SpectralPyramid> py; juce::String err;
        if (r == nullptr || r->lengthInSamples <= 0 || r->numChannels == 0) err = "The render could not be read.";
        else
        {
            py = std::make_shared<SpectralPyramid>();
            py->setUp (r->sampleRate, (double) r->lengthInSamples / r->sampleRate);
            if (py->levels.empty()) err = "The render is too short for a picture.";
            else
            {
                const int frames = py->levels[0].frames, nc = juce::jmin (8, (int) r->numChannels);
                auto* rd = r.get();
                for (int c = 0; c < nc && ! threadShouldExit(); ++c)
                    if (! py->addTrack (0, frames, [rd, c] (juce::int64 start, int n, float* dst) { readChannel (*rd, c, start, n, dst); }, c == 0, [this] { return threadShouldExit(); })) break;
                if (! threadShouldExit()) py->settle();
            }
        }
        if (threadShouldExit()) return;
        if (err.isNotEmpty()) py.reset();
        juce::MessageManager::callAsync ([a = alive, d = done, py, err] { if (a->load()) d (py, err); });
    }
    juce::File file; Done done;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};

/** The sharp picture of one view of the render (as Noise mode's renderSharp, read from the render's channels). Any thread. */
std::shared_ptr<DetailPicture> sharpOfRender (const juce::File& file, const PictureRequest& rq, const std::shared_ptr<std::atomic<bool>>& cancel)
{
    std::unique_ptr<juce::AudioFormatReader> r (fixtools::formats().createReaderFor (file));
    if (r == nullptr || r->numChannels == 0) return nullptr;
    const double sr = r->sampleRate;
    const int W = juce::jmax (1, rq.w); const double pxSec = (rq.t1 - rq.t0) / (double) W;
    const int c0 = juce::jlimit (0, W, rq.c0), c1 = rq.c1 < 0 ? W : juce::jlimit (c0, W, rq.c1);
    const juce::int64 A0 = picture::firstColumn (rq.t0, rq.t1, W);
    const double margin = picture::reach (sr, rq.t0, rq.t1, W, rq.opt) + 4.0 * pxSec + 0.05;
    const juce::int64 s0 = juce::jmax<juce::int64> (0, (juce::int64) std::floor (((double) (A0 + c0) * pxSec - margin) * sr));
    const juce::int64 s1 = juce::jmax (s0, (juce::int64) std::ceil (((double) (A0 + c1) * pxSec + margin) * sr));
    if (s1 - s0 > (juce::int64) (sr * 400.0)) return nullptr;                    // (far too much to read at once: the quick picture stays)
    std::vector<std::vector<float>> tracks ((size_t) juce::jmin (8, (int) r->numChannels));
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        if (cancel->load()) return nullptr;
        tracks[i].assign ((size_t) (s1 - s0), 0.0f);
        readChannel (*r, (int) i, s0, (int) (s1 - s0), tracks[i].data());
    }
    picture::RenderPart part; part.firstSample = s0; part.c0 = rq.c0; part.c1 = rq.c1;
    auto pic = picture::render (tracks, sr, rq.t0, rq.t1, rq.w, rq.h, rq.axis, rq.opt, [cancel] { return cancel->load(); }, {}, part);
    pic.id = rq.id;
    if (cancel->load()) return nullptr;
    return std::make_shared<DetailPicture> (std::move (pic));
}

/** The level of [f0, f1] Hz in n samples: the RMS of that band in dB, a full-scale sine reading 0 (as the window's other RMS figures do by default).
    The whole range (f0 at or under 20 Hz, f1 at or over 0.49 of the rate) is the plain RMS. */
double bandLevelDb (const float* x, int n, double rate, double f0, double f1)
{
    if (n <= 0 || rate <= 0.0) return wavestats::kNone;
    if (f0 <= 20.0 && f1 >= rate * 0.49)
    {
        double sq = 0.0; for (int i = 0; i < n; ++i) sq += (double) x[i] * (double) x[i];
        return wavestats::powerToDb (2.0 * sq / (double) n);
    }
    const int N = 4096, hop = N / 2;
    static const dsp::FastFFT fft (N); static const auto window = dsp::hann (N);
    std::vector<std::complex<double>> X ((size_t) N);
    const int k0 = juce::jlimit (0, N / 2, (int) std::ceil (f0 * (double) N / rate)), k1 = juce::jlimit (0, N / 2, (int) std::floor (f1 * (double) N / rate));
    double band = 0.0, wsum = 0.0;
    for (int at = n <= N ? 0 : -hop; at < n; at += hop)                          // (windows that overlap by half, the first and the last reaching past the ends)
    {
        double w2 = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const int q = at + i; const bool in = q >= 0 && q < n;
            // (a stretch shorter than one window has a window of its own length, then silence)
            const double w = ! in ? 0.0 : n < N ? 0.5 - 0.5 * std::cos (2.0 * wavestats::kPi * ((double) q + 0.5) / (double) n) : window[(size_t) i];
            X[(size_t) i] = { in ? (double) x[q] * w : 0.0, 0.0 };
            w2 += w * w;
        }
        if (w2 <= 0.0) continue;
        fft.forward (X);
        for (int k = juce::jmax (1, k0); k <= k1; ++k) band += 2.0 * std::norm (X[(size_t) k]) / (double) N;
        wsum += w2;
        if (n <= N) break;
    }
    return wsum > 0.0 ? wavestats::powerToDb (2.0 * band / wsum) : wavestats::kNone;
}

/** Which pitches a peak is made of: the sample at the peak is the sum of what every third of an octave adds to it there, so the band that adds most
    (in the peak's own direction) is the one to bring down. 'share': how much of the peak's height that band carries (1 = all of it; the others may
    add to it or take from it). */
struct PeakBand { double lo = 0.0, hi = 0.0, share = 0.0; bool valid() const noexcept { return hi > lo; } };

PeakBand bandAtPeak (const FileSamples& samples, const Profile& p, juce::int64 frame, int channel)
{
    PeakBand out;
    const int N = p.rate < 80000.0 ? 8192 : p.rate < 160000.0 ? 16384 : 32768;
    juce::AudioBuffer<float> buf (p.channels, N);
    if (! samples.read (frame - N / 2, N, buf.getArrayOfWritePointers(), p.channels)) return out;
    const float* x = buf.getReadPointer (juce::jlimit (0, p.channels - 1, channel));
    const double x0 = (double) x[N / 2];
    if (std::abs (x0) < 1.0e-6) return out;
    const dsp::FastFFT fft (N);
    std::vector<std::complex<double>> X ((size_t) N);
    for (int i = 0; i < N; ++i)                                                  // (faded at its two ends only: the middle, where the peak is, is as it is)
    {
        const int e = juce::jmin (i, N - 1 - i);
        const double w = e >= N / 4 ? 1.0 : 0.5 - 0.5 * std::cos (wavestats::kPi * (double) e / (double) (N / 4));
        X[(size_t) i] = { (double) x[i] * w, 0.0 };
    }
    fft.forward (X);
    std::vector<double> edge { 0.0 };
    const double nyq = p.rate * 0.5, third = std::pow (2.0, 1.0 / 3.0);
    for (double f = 20.0 * std::pow (2.0, 1.0 / 6.0); f < nyq * 0.98; f *= third) edge.push_back (f);
    edge.push_back (nyq);
    const int nb = (int) edge.size() - 1;
    std::vector<double> adds ((size_t) nb, 0.0);
    int b = 0;
    for (int k = 0; k <= N / 2; ++k)                                             // bin k adds (2 / N) Re (X[k]) (-1)^k to the middle sample (half that for the first and the last)
    {
        const double f = (double) k * p.rate / (double) N;
        while (b < nb - 1 && f >= edge[(size_t) b + 1]) ++b;
        adds[(size_t) b] += ((k == 0 || k == N / 2) ? 1.0 : 2.0) / (double) N * X[(size_t) k].real() * ((k & 1) != 0 ? -1.0 : 1.0);
    }
    const double s = x0 > 0.0 ? 1.0 : -1.0;
    int best = 0;
    for (int i = 1; i < nb; ++i) if (s * adds[(size_t) i] > s * adds[(size_t) best]) best = i;
    const double top = s * adds[(size_t) best];
    if (top <= 0.0) return out;
    int lo = best, hi = best;
    while (lo > 0 && s * adds[(size_t) lo - 1] >= 0.5 * top) --lo;               // the bands beside it that add at least half as much belong to the same sound
    while (hi < nb - 1 && s * adds[(size_t) hi + 1] >= 0.5 * top) ++hi;
    double sum = 0.0; for (int i = lo; i <= hi; ++i) sum += s * adds[(size_t) i];
    out.lo = edge[(size_t) lo]; out.hi = edge[(size_t) hi + 1]; out.share = sum / std::abs (x0);
    return out;
}

juce::String hzText (double f) { return f >= 10000.0 ? juce::String (f / 1000.0, 1) + " k" : f >= 1000.0 ? juce::String (f / 1000.0, 2) + " k" : juce::String (juce::roundToInt (f)); }
juce::String bandText (const PeakBand& b)
{
    if (! b.valid()) return {};
    return (b.lo < 1.0 ? juce::String ("under ") + hzText (b.hi) : hzText (b.lo) + " - " + hzText (b.hi)) + " Hz";
}

/** How finely ReTouch's Gain looks at pitch for a box this tall (RepairParams::bands; its window is twice that). The gain slopes in over three of
    its bins at each edge of the box (SpectralRepair::boxGain), so with the usual 1024 bands - bins 23 Hz apart at 48 kHz - a low, narrow box, as
    one round a bass note is, would get about half the gain asked for in its middle and spill 70 Hz either side. Finer bins for such a box. */
int bandsForBox (double f0, double f1, double rate)
{
    const double height = juce::jmax (1.0, f1 - f0);
    int n = 2048;
    while (n < 8192 && 6.0 * rate / (double) n > height) n <<= 1;
    return n / 2;
}
/** About how much of 'gainDb' reaches the middle of a hard-edged box (dB), worked out as boxGain makes it: its window (shorter for a short box)
    and the slopes at the box's lower and upper edge. (Against boxGain itself on a 160 Hz tone, -3 dB asked for in 143 - 180 Hz at 48 kHz:
    1024 bands, 0.2 s: -1.2 dB there, -1.3 here; 4096 bands, 0.2 s: -2.9 and -2.9; 4096 bands, 0.06 s: -1.6 and -1.5.) */
double gainReachingMiddle (double gainDb, double seconds, double f0, double f1, double rate, int bands)
{
    int N = 256; while (N < 2 * bands && N < 8192) N <<= 1;
    while (N > 256 && (double) N > 2.0 * seconds * rate) N >>= 1;
    const double hz = rate / (double) N, f = 0.5 * (f0 + f1);
    auto edge = [&] (double e, bool lowEdge)
    {
        if (e <= 0.0) return 1.0;
        const double w = juce::jmax (3.0 * hz, e * 0.12);
        const double u = juce::jlimit (0.0, 1.0, ((lowEdge ? f - e : e - f) + w) / (2.0 * w));
        return u * u * (3.0 - 2.0 * u);
    };
    // (and in time: a box shorter than the window never has a whole window inside it. Measured with a steady tone: about three quarters of the
    //  gain from a box 0.7 of the window long, all of it from 1.2 windows on)
    const double inTime = juce::jlimit (0.0, 1.0, 0.35 + 0.57 * seconds * rate / (double) N);
    const double inside = edge (f0, true) * (f1 >= rate * 0.49 ? 1.0 : edge (f1, false)) * inTime;
    const double g = gainDb <= -120.0 ? 0.0 : std::pow (10.0, gainDb / 20.0);
    return wavestats::ampToDb (1.0 + (g - 1.0) * inside);
}

/** One of the highest peaks, as the list shows it. */
struct PeakRow { wavestats::PeakEvent ev; PeakBand band; };

/** The list of the highest true peaks: a click picks one. */
class PeaksList : public juce::Component, private juce::ListBoxModel
{
public:
    std::function<void (int)> onPick;
    std::vector<PeakRow> rows;
    double rate = 48000.0;
    int picked = -1;

    PeaksList()
    {
        addAndMakeVisible (list); list.setModel (this); list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, theme::field); list.setColour (juce::ListBox::outlineColourId, theme::border); list.setOutlineThickness (1);
        list.setWantsKeyboardFocus (false);
    }
    void refresh() { list.updateContent(); list.repaint(); if (picked >= 0 && picked < (int) rows.size()) list.scrollToEnsureRowIsOnscreen (picked); }
    void resized() override { list.setBounds (getLocalBounds()); }

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        const auto& r = rows[(size_t) row];
        if (row == picked) { g.setColour (theme::selected); g.fillRect (0, 0, w, h); }
        const bool over = r.ev.db > 0.0;
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold)); g.setColour (juce::Colour (0xffffd24a));
        g.drawText (juce::String (row + 1), 4, 0, 20, h, juce::Justification::centredRight, false);
        g.setFont (juce::FontOptions (12.5f)); g.setColour (theme::text);
        g.drawText (clockText ((double) r.ev.frame / rate), 30, 0, 78, h, juce::Justification::centredLeft, false);
        g.setColour (over ? juce::Colour (0xffff6a5a) : theme::text);
        g.drawText ((r.ev.db > 0.0 ? "+" : "") + juce::String (r.ev.db, 2), 108, 0, 50, h, juce::Justification::centredRight, false);
        g.setColour (theme::dimText);
        g.drawText (r.ev.channel == 0 ? "L" : r.ev.channel == 1 ? "R" : juce::String (r.ev.channel + 1), 162, 0, 16, h, juce::Justification::centred, false);
        if (r.band.valid())
            g.drawText (bandText (r.band) + "  " + juce::String (juce::jmin (999, juce::roundToInt (r.band.share * 100.0))) + " %", 182, 0, w - 186, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) rows.size()) && onPick) onPick (row); }
    juce::String getTooltipForRow (int row) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return {};
        const auto& r = rows[(size_t) row];
        juce::String t = "Peak " + juce::String (row + 1) + ": " + juce::String (r.ev.db, 2) + " dBTP at " + clockText ((double) r.ev.frame / rate) + ". Click: the playhead and the picture go there.";
        if (r.band.valid()) t << " Most of its height comes from " << bandText (r.band) << " (about " << juce::jmin (999, juce::roundToInt (r.band.share * 100.0)) << " % of it): \"Box it\" draws a box round that band.";
        return t;
    }
    juce::ListBox list;
};

/** A list of the piece's tracks, each with a tick (a click on the row switches it), a note at the right and - when it has one - its level as a bar. */
class TickList : public juce::Component, private juce::ListBoxModel
{
public:
    /** key (3.77.0): a small key at the right of the row ("Window"), empty for none; a click on it is onOpen. keyLit: drawn as switched on. */
    struct Row { juce::Uuid id; juce::String name, note, tip, key; bool on = false, enabled = true, hasLevel = false, keyLit = false; double level = wavestats::kNone; };
    std::vector<Row> rows;
    std::function<void (int)> onTick;                        // row: its tick was switched (the row already has its new state)
    std::function<void (int)> onOpen;                        // row: double-clicked
    bool wholeRowTicks = true;                               // false: only a click on the tick box switches it (the row's double-click then opens something)

    TickList()
    {
        addAndMakeVisible (list); list.setModel (this); list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, theme::field); list.setColour (juce::ListBox::outlineColourId, theme::border); list.setOutlineThickness (1);
        list.setWantsKeyboardFocus (false);
    }
    void refresh() { list.updateContent(); list.repaint(); }
    void repaintRows() { list.repaint(); }
    void resized() override { list.setBounds (getLocalBounds()); }
    std::vector<juce::Uuid> ticked() const { std::vector<juce::Uuid> v; for (auto& r : rows) if (r.on) v.push_back (r.id); return v; }
    int countOn() const { int n = 0; for (auto& r : rows) if (r.on) ++n; return n; }
    void setAll (bool on) { for (auto& r : rows) if (r.enabled) r.on = on; refresh(); }

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        const auto& r = rows[(size_t) row];
        const float alpha = r.enabled ? 1.0f : 0.45f;
        if (r.hasLevel && ! none (r.level))
        {
            const int bw = juce::roundToInt ((double) juce::jmin (110, w / 3) * juce::jlimit (0.0, 1.0, (r.level + 72.0) / 72.0));
            g.setColour (theme::accent.withAlpha (0.38f)); g.fillRect (w - bw, 2, bw, h - 4);
        }
        const juce::Rectangle<int> box (6, (h - 12) / 2, 12, 12);
        g.setColour (r.on ? theme::accent.withAlpha (alpha) : theme::wellDark); g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (theme::dimText.withAlpha (alpha)); g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 2.0f, 1.0f);
        if (r.on)
        {
            juce::Path tick; tick.startNewSubPath (8.5f, (float) h * 0.5f); tick.lineTo (11.0f, (float) h * 0.5f + 2.6f); tick.lineTo (15.5f, (float) h * 0.5f - 3.0f);
            g.setColour (juce::Colours::white.withAlpha (alpha)); g.strokePath (tick, juce::PathStrokeType (1.7f));
        }
        g.setFont (juce::FontOptions (12.5f));
        const int kw = keyWidth (r);
        if (kw > 0)
        {
            const auto kr = juce::Rectangle<int> (w - kw - 3, 2, kw, h - 4);
            g.setColour (r.keyLit ? theme::accent.withAlpha (0.85f) : theme::wellDark); g.fillRoundedRectangle (kr.toFloat(), 3.0f);
            g.setColour (theme::dimText.withAlpha (0.8f)); g.drawRoundedRectangle (kr.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setFont (juce::FontOptions (11.5f));
            g.setColour (r.keyLit ? juce::Colours::white : theme::text); g.drawText (r.key, kr, juce::Justification::centred, false);
            g.setFont (juce::FontOptions (12.5f));
            w -= kw + 6;
        }
        const int noteW = r.note.isEmpty() ? 0 : juce::jmin (w / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), r.note) + 10);
        g.setColour (theme::text.withAlpha (alpha)); g.drawText (r.name, 24, 0, w - 28 - noteW, h, juce::Justification::centredLeft, true);
        if (noteW > 0) { g.setColour ((r.hasLevel ? theme::text : theme::dimText).withAlpha (alpha)); g.drawText (r.note, w - noteW - 4, 0, noteW, h, juce::Justification::centredRight, true); }
    }
    static int keyWidth (const Row& r) { return r.key.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.5f)), r.key) + 14; }
    bool onKey (int row, const juce::MouseEvent& e) const { const int kw = keyWidth (rows[(size_t) row]); return kw > 0 && e.x >= list.getVisibleRowWidth() - kw - 6; }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        if (onKey (row, e)) { if (onOpen && e.getNumberOfClicks() == 1) onOpen (row); return; }      // (3.77.0: the row's key)
        if (! rows[(size_t) row].enabled || e.getNumberOfClicks() > 1) return;
        if (! wholeRowTicks && e.x > 24) return;
        rows[(size_t) row].on = ! rows[(size_t) row].on;
        list.repaintRow (row);
        if (onTick) onTick (row);
    }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override { if (juce::isPositiveAndBelow (row, (int) rows.size()) && onOpen && ! onKey (row, e)) onOpen (row); }
    juce::String getTooltipForRow (int row) override { return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row].tip : juce::String(); }
    juce::ListBox list;
};

/** 3.76.0: the list of the gain boxes that are in the piece (EditDef::specGains), the newest at the top: a click picks one. */
class ChangesList : public juce::Component, private juce::ListBoxModel
{
public:
    struct Row { juce::Uuid id; int number = 0; double seconds = 0.0; juce::String band, gain, tracks, tip; };
    std::vector<Row> rows;
    juce::Uuid picked;
    std::function<void (int)> onPick;
    std::function<void (int)> onOpen;                        // double-click

    ChangesList()
    {
        addAndMakeVisible (list); list.setModel (this); list.setRowHeight (20);
        list.setColour (juce::ListBox::backgroundColourId, theme::field); list.setColour (juce::ListBox::outlineColourId, theme::border); list.setOutlineThickness (1);
        list.setWantsKeyboardFocus (false);
    }
    void refresh() { list.updateContent(); list.repaint(); }
    void resized() override { list.setBounds (getLocalBounds()); }
    int pickedRow() const { for (size_t i = 0; i < rows.size(); ++i) if (rows[i].id == picked) return (int) i; return -1; }

private:
    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool) override
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        const auto& r = rows[(size_t) row];
        if (r.id == picked) { g.setColour (theme::selected); g.fillRect (0, 0, w, h); }
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold)); g.setColour (theme::accent);
        g.drawText (juce::String (r.number), 4, 0, 22, h, juce::Justification::centredRight, false);
        g.setFont (juce::FontOptions (12.5f)); g.setColour (theme::text);
        g.drawText (clockText (r.seconds), 32, 0, 78, h, juce::Justification::centredLeft, false);
        g.drawText (r.gain, 110, 0, 52, h, juce::Justification::centredRight, false);
        g.setColour (theme::dimText);
        const int tw = juce::jmin (w / 4, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), r.tracks) + 8);
        g.drawText (r.tracks, w - tw - 2, 0, tw, h, juce::Justification::centredRight, true);
        g.drawText (r.band, 170, 0, w - 174 - tw, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override { if (juce::isPositiveAndBelow (row, (int) rows.size()) && e.getNumberOfClicks() == 1 && onPick) onPick (row); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) rows.size()) && onOpen) onOpen (row); }
    juce::String getTooltipForRow (int row) override { return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(size_t) row].tip : juce::String(); }
    juce::ListBox list;
};
} // namespace

//==================================================================================================================================
struct SpecEditComponent::Impl
{
    Impl (SpecEditComponent& o, AppContext& a) : owner (o), app (a), prefsFirst (a) {}

    SpecEditComponent& owner;
    AppContext& app;
    struct PrefsFirst { explicit PrefsFirst (AppContext& a) { fixtools::gSpecPrefs = a.props.getUserSettings(); } } prefsFirst;     // (before the picture: it reads how it is drawn from them)
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
    // 3.75.0: the picture and its tools
    SpectrogramView view;
    fixtools::SelectToolsBar toolsBar { view };
    juce::TextButton specBtn { "Picture..." }, metersBtn { "Meters" };
    // 3.75.0: the right-hand side is one of three pages
    enum { kTabFigures, kTabPeaks, kTabChanges, kTabPlug, kTabCount };
    juce::TextButton tabBtn[kTabCount];
    int tab = kTabFigures;
    // ... the peaks, and the gain that brings them down
    juce::Label countCaption, peaksNote, gainCaption, tracksCaption;
    juce::ComboBox countBox;
    PeaksList peaks;
    juce::TextButton boxBtn { "Box it" }, reduceBtn { "Reduce" }, undoBtn { "Undo" }, allBtn { "All" }, noneBtn { "None" };
    juce::Slider gainSlider;
    TickList gainTracks;
    double nextAfter = wavestats::kNone;                     // the highest peak that is not in the list
    // ... 3.76.0: the list of the gain boxes that are in the piece
    juce::Label changesNote;
    ChangesList changes;
    juce::TextButton takeOffBtn { "Take it off" }, againBtn { "Change it..." };
    // ... the plug-in on chosen tracks
    juce::Label plugNote, plugName, plugDelay;
    juce::TextButton chooseBtn { "Choose plug-in..." }, openBtn { "Open" }, removeBtn { "Remove" }, plugAllBtn { "All" }, plugNoneBtn { "None" };
    juce::ToggleButton bypassToggle { "Bypass" }, linkToggle { "Link the settings" };
    TickList plugTracks;
    juce::String plugId;                                     // which plug-in the ticked tracks hold (PluginHost::identityOf); empty: none yet
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
    juce::uint32 dirtyAt = 0, dirtySince = 0, lastStoreLook = 0;
    bool changedWhileRendering = false;                      // 3.75.2: the piece was changed (from here, or the project said so) while its render was being made
    // 3.75.0: the picture
    std::shared_ptr<SpectralPyramid> pyramid;
    std::unique_ptr<PictureBuild> pictureBuild;
    std::shared_ptr<std::atomic<bool>> detailCancel;
    bool pictureSamePiece = false, selFromView = false;
    double lastCue = -1.0, lastPlayPos = -1.0;
    // 3.75.0: the gain boxes put into the edit from here (the newest last), each with what the edit was before it
    /** 3.76.0: what a gain box being made is noted down as when it is in (EditDef::SpecGain; its files and place are filled in then). */
    EditDef::SpecGain pendingGain;
    AudioJob& job = *new AudioJob();                         // (handed back with release() as the window goes: it may outlive it)
    bool fixing = false, renderAgainAfterCancel = false;
    juce::uint64 shownHash = 0;                              // the edit as the picture shows it (Project::undoHash): a box may only go into that edit
    // 3.75.0: which tracks are in the box
    std::shared_ptr<std::atomic<int>> breakdownGen = std::make_shared<std::atomic<int>> (0);
    juce::uint32 breakdownDue = 0;
    // 3.75.0: the plug-in's instances as they were last looked at (Link the settings: a control moved on one is moved on the others)
    struct Seen { juce::Uuid track; InsertProcessor* proc = nullptr; std::vector<float> vals; };
    std::vector<Seen> seen;
    InsertProcessor* linkFrom = nullptr;                     // 3.75.2: the instance being set by hand (its window is open and a control of it moved)
    InsertProcessor* linkCopiedFrom = nullptr;               // 3.77.0: the one the last copy was made from (its own moves count at once: see linkQuietUntil)
    juce::String openKeys;                                   // 3.77.0: which tracks' plug-in windows were open at the last look (the rows' Window keys)
    juce::uint32 linkDue = 0, linkRunStart = 0, linkQuietUntil = 0;
    int linkRuns = 0; bool linkResting = false;
    juce::uint32 lastPlugLook = 0, lastLevelsLook = 0;
    juce::uint64 levelsHash = 0;
    // playing
    bool playing = false;
    double playStart = 0.0;

    EditDef* edit() const { return app.project.findEdit (editId); }
    juce::String pieceName() const { if (auto* it = app.project.mastering.findItem (editId); it != nullptr && it->fileName.isNotEmpty()) return it->fileName; if (auto* e = edit()) return e->name; return "(no piece)"; }

    void say (const juce::String& t, bool warn = false) { status.setColour (juce::Label::textColourId, warn ? theme::warn : theme::text); status.setText (t, juce::dontSendNotification); }
    /** The piece may have changed: it is looked at a moment after the last such word (and, 3.75.2, six seconds after the first at the latest: changes
        that never stop coming must not put the look off for ever - in 3.75.0 they did, and the window went on saying "up to date"). */
    void markDirty()
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (! dirty) dirtySince = now;
        dirty = true; dirtyAt = now;
        if (renderJob != nullptr) changedWhileRendering = true;
    }

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
        pictureBuild.reset(); pyramid.reset(); if (detailCancel != nullptr) detailCancel->store (true);
        view.setData ({}, 0.001); view.setTimeMarks ({}); view.placeCue (-1.0); lastCue = view.cueTime();
        shownHash = 0; selFromView = false; seen.clear(); plugId.clear(); changes.picked = juce::Uuid::null();
        ++*breakdownGen;
        lastFp.clear(); problem.clear(); needsRender = false; staleAfterRender = false; wantRender = false;
        wave.emptyText = "Measuring..."; wave.setProfile (nullptr, false); wave.setMarks ({}, -1); meters.setProfile (nullptr);
        showFigures(); fillPeaks();
        fillPieces();
        fillTracks(); lookAtPlugs(); fillChanges();
        if (auto* tl = owner.getTopLevelComponent()) if (owner.isShowing()) tl->setName ("Mastering - " + pieceName());
        dirty = false;
        lookAtPiece();
    }

    /** Looks at the piece's render (never makes one): measures it when it is new, or says what is in the way. */
    void lookAtPiece()
    {
        lastStoreLook = juce::Time::getMillisecondCounter();
        if (renderJob != nullptr) return;
        auto* e = edit();
        if (e == nullptr || e->isEmpty()) { problem = e == nullptr ? "Choose a piece (the pieces are the Exports window's list)." : "This piece has no audio."; wave.emptyText = problem; wave.repaint(); say (problem, true); updateButtons(); return; }
        RenderParams rp;
        const auto perr = makeRenderParams (app.project, app.project.mastering, rp);
        if (perr.isNotEmpty()) { problem = perr; wave.emptyText = perr; wave.repaint(); say (perr, true); updateButtons(); return; }
        problem.clear();
        lastFp = masterRenderFingerprint (app.project, *e, rp);
        levelsHash = mixerLevelsHash();
        MasterRenders store (MasterRenders::folderFor (app.project));
        const auto info = store.check (editId, lastFp);
        if (info.state == MasterRenders::State::Ready || info.state == MasterRenders::State::Previous)
        {
            needsRender = false; staleAfterRender = false;
            if (info.state == MasterRenders::State::Ready) shownHash = Project::undoHash (*e);       // (the picture is of the edit as it is now)
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
            else say (never ? "Not rendered yet: press Measure again." : (staleAfterRender ? "The piece changed while it was rendered: press Measure again." : "The piece has changed: the picture and the figures are of its last render. Press Measure again."), true);
        }
        updateButtons();
    }
    juce::String measuredText (bool previous) const
    {
        return "Measured: the render of " + fileTime.toString (true, true, false, true) + (previous ? "  (the PREVIOUS render is in use: the Exports window's \"Use newest render\")" : juce::String())
               + (needsRender ? juce::String() : juce::String ("  -  up to date"));
    }

    void startRender()
    {
        wantRender = true;
        if (renderJob != nullptr) return;
        if (fixing) return;                                                      // (a gain box is being made: the render waits for it - see tick)
        if (masterbusy::holds() > 0) { say ("The Exports window is rendering or exporting: this piece is measured when it has finished."); return; }
        if (app.engine.isRecording()) { say ("Recording: the piece is measured when the recording has stopped."); return; }
        // 3.75.2: not while something plays - a render works flat out, and what is being listened to (the piece, live, while a plug-in is set) must not stutter for it
        if (app.isPlaying()) { say ("The piece has changed: it is rendered and measured again when playing stops."); return; }
        auto* e = edit(); if (e == nullptr) return;
        RenderParams rp;
        if (const auto perr = makeRenderParams (app.project, app.project.mastering, rp); perr.isNotEmpty()) { say (perr, true); wantRender = false; return; }
        auto batch = std::make_unique<MasterRenderBatch> (app.project, std::vector<juce::Uuid> { editId }, rp, false);
        if (batch->getPrepareError().isNotEmpty()) { say (batch->getPrepareError(), true); wantRender = false; return; }
        wantRender = false;
        if (batch->toRender() == 0) { needsRender = false; lookAtPiece(); return; }
        renderingParts = masterRenderParts (app.project, *e, rp);                 // (what the render is made from: see renderDone)
        changedWhileRendering = false;
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
        if (renderAgainAfterCancel)                                              // (3.75.0: a gain box or a plug-in from this window went in while it rendered: once more, with it)
        {
            renderAgainAfterCancel = false; staleAfterRender = false;
            lastFp.clear(); markDirty();                                         // (a moment after the last change: a control may still be moving)
            say ("The piece changed: it is rendered again in a moment.");
            updateButtons();
            return;
        }
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
                else if (changedWhileRendering && autoToggle.getToggleState())
                {   // 3.75.2: changed by hand meanwhile (a plug-in's control, a fader, an edit) and "Keep up to date" is on: simply once more. (3.75.0
                    // stopped here and waited for Measure again - and what played and showed was the render from before the change.)
                    renderingParts.clear(); changedWhileRendering = false;
                    lastFp.clear(); markDirty();
                    say ("The piece changed while it was rendered: it is rendered again in a moment.");
                    updateButtons();
                    return;
                }
                else { juce::Logger::writeToLog ("Mastering (spectrogram): \"" + e->name + "\" was changed while it was rendered; its render is out of date"); staleAfterRender = true; }
            }
        }
        renderingParts.clear();
        lookAtPiece();
    }
    /** 3.75.0: something this window did changed the piece's sound (a gain box, Undo, the plug-in): its render is made again - a moment later when
        "Keep up to date" is on - and one being made now is stopped, since it is of the piece as it was. */
    void soundChangedHere()
    {
        staleAfterRender = false;
        if (renderJob != nullptr && ! renderAgainAfterCancel) { renderAgainAfterCancel = true; renderJob->cancel(); }
        markDirty();
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
        profile = std::move (p);                                                // (3.75.2: playing goes on - it is the piece that plays now, not the render's file)
        whole = wavestats::measure (*profile, options);
        wave.setProfile (profile, samePieceNext); meters.setProfile (profile);
        if (wave.playheadSeconds() > profile->seconds()) setPlayheadAll (0.0);
        showFigures(); showPlayhead();
        fillPeaks();
        say (measuredText (false));
        startPicture (samePieceNext);
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

    // ---- 3.75.0: the picture of the render. The quick one is a slice of its pyramid for what is in view; the sharp one is made for the view on a thread.
    void startPicture (bool samePiece)
    {
        pictureBuild.reset();
        if (detailCancel != nullptr) detailCancel->store (true);
        pictureSamePiece = samePiece && pyramid != nullptr;
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        pictureBuild = std::make_unique<PictureBuild> (file, [safe] (std::shared_ptr<SpectralPyramid> py, juce::String err) { if (safe != nullptr) safe->impl->pictureDone (std::move (py), err); });
    }
    void pictureDone (std::shared_ptr<SpectralPyramid> py, const juce::String& err)
    {
        pictureBuild.reset();
        if (py == nullptr || profile == nullptr) { if (err.isNotEmpty()) say (err, true); return; }
        const bool keepView = pictureSamePiece && std::abs (py->duration - (pyramid != nullptr ? pyramid->duration : -1.0)) < 1.0e-6;
        pyramid = std::move (py);
        if (! keepView) { view.setData ({}, 0.001); view.placeCue (wave.playheadSeconds()); lastCue = view.cueTime(); }
        sliceForView();
        if (keepView) view.invalidateDetail();
        showMarks();
    }
    void sliceForView()
    {
        if (pyramid == nullptr || ! pyramid->valid() || profile == nullptr) return;
        const double secs = profile->seconds();
        const double t0 = juce::jmax (0.0, view.visT0()), t1 = juce::jmin (secs, view.visT1());
        if (t1 <= t0) return;
        double a = 0.0, b = 0.0;
        auto s = pyramid->slice (t0, t1, 2400, a, b);
        view.setDataRange (std::move (s), a, b, secs);
        wave.setBracket (view.visT0(), view.visT1());
    }
    void startDetail (const PictureRequest& rq)
    {
        if (pyramid == nullptr || ! file.existsAsFile() || rq.t1 - rq.t0 > 180.0) return;       // (zoomed far out: the quick picture is all there is)
        if (detailCancel != nullptr) detailCancel->store (true);
        auto cancel = std::make_shared<std::atomic<bool>> (false); detailCancel = cancel;
        const auto f = file; juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        std::thread ([f, rq, cancel, safe]
        {
            try                                                                  // (nothing may escape a thread: out of memory leaves the quick picture, nothing worse)
            {
                if (auto p = sharpOfRender (f, rq, cancel))
                    juce::MessageManager::callAsync ([safe, p, cancel] { if (safe != nullptr && ! cancel->load()) safe->impl->view.setDetail (std::move (*p)); });
            }
            catch (...) {}
        }).detach();
    }
    void openPictureSettings()
    {
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        fixtools::launchDialog (std::make_unique<fixtools::SpectroSettingsDialog> (view.settings(), [safe] (const fixtools::SpectroSettings& n)
        {
            if (safe == nullptr) return;
            safe->impl->view.setSettings (n); n.save (fixtools::gSpecPrefs);
            if (fixtools::gSpecPrefs != nullptr) fixtools::gSpecPrefs->saveIfNeeded();
        }), "Spectrogram settings", &owner);
    }

    // ---- 3.75.0: the highest true peaks
    int peakCount() const { return juce::jlimit (1, 100, countBox.getText().getIntValue()); }
    void fillPeaks()
    {
        const juce::int64 was = juce::isPositiveAndBelow (peaks.picked, (int) peaks.rows.size()) ? peaks.rows[(size_t) peaks.picked].ev.frame : -1;
        peaks.rows.clear(); peaks.picked = -1; nextAfter = wavestats::kNone;
        if (profile != nullptr)
        {
            const int n = peakCount();
            auto ev = wavestats::topPeaks (*profile, n + 1, 0.5);
            if ((int) ev.size() > n) { nextAfter = ev[(size_t) n].db; ev.resize ((size_t) n); }
            for (auto& e : ev) peaks.rows.push_back ({ e, bandAtPeak (samples, *profile, e.frame, e.channel) });
            peaks.rate = profile->rate;
            if (was >= 0) for (size_t i = 0; i < peaks.rows.size(); ++i) if (std::llabs (peaks.rows[i].ev.frame - was) < (juce::int64) (0.05 * profile->rate)) peaks.picked = (int) i;
        }
        peaks.refresh();
        const int n = (int) peaks.rows.size();
        if (n == 0) peaksNote.setText (profile != nullptr ? "No peaks: the piece is silent." : juce::String(), juce::dontSendNotification);
        else if (none (nextAfter)) peaksNote.setText ("These are all the separate peaks there are.", juce::dontSendNotification);
        else peaksNote.setText ("With " + juce::String (n == 1 ? "this one" : "these " + juce::String (n)) + " brought down, the highest peak left is " + juce::String (nextAfter, 2) + " dBTP: "
                                + juce::String (peaks.rows[0].ev.db - nextAfter, 2) + " dB more room.", juce::dontSendNotification);
        showMarks();
        scheduleBreakdown();
    }
    void showMarks()
    {
        std::vector<SpectrogramView::TimeMark> marks; std::vector<double> times;
        if (profile != nullptr)
            for (size_t i = 0; i < peaks.rows.size(); ++i)
            {
                const double t = (double) peaks.rows[i].ev.frame / profile->rate;
                marks.push_back ({ t, juce::String ((int) i + 1), juce::Colour (0xffffd24a), (int) i == peaks.picked });
                times.push_back (t);
            }
        view.setTimeMarks (std::move (marks)); wave.setMarks (std::move (times), peaks.picked);
    }
    void pickPeak (int i)
    {
        if (profile == nullptr || ! juce::isPositiveAndBelow (i, (int) peaks.rows.size())) return;
        peaks.picked = i; peaks.refresh(); showMarks();
        const double t = (double) peaks.rows[(size_t) i].ev.frame / profile->rate;
        const double span = juce::jmin (view.pageSpan(), 4.0);                   // (zoomed in to four seconds round it, or closer if the picture already is)
        if (pyramid != nullptr) view.showSpan (t - 0.5 * span, span);
        setPlayheadAll (t); wave.follow (t);
        if (playing) startPlay();
        scheduleBreakdown();
        updateButtons();
    }
    /** A box round the band that carries most of the picked peak, for the hand to move and resize. */
    void boxPicked()
    {
        if (profile == nullptr || pyramid == nullptr) { say ("The picture is still being made: a moment.", true); return; }
        if (! juce::isPositiveAndBelow (peaks.picked, (int) peaks.rows.size())) { say ("Click one of the peaks in the list first.", true); return; }
        const auto& r = peaks.rows[(size_t) peaks.picked];
        if (! r.band.valid()) { say ("No one band stands out at this peak: draw the box by hand.", true); return; }
        const double t = (double) r.ev.frame / profile->rate;
        // (a tenth of a second each side, more for a low band: a dozen of its waves. A box much shorter than that is shorter than the stretch of sound
        //  the gain itself works on at a time, and only a part of the gain asked for would come out of it)
        const double half = juce::jlimit (0.1, 0.5, 12.0 / juce::jmax (20.0, r.band.lo > 1.0 ? r.band.lo : r.band.hi));
        double lo = r.band.lo, hi = r.band.hi;
        const double least = 6.0 * profile->rate / 8192.0;                       // (and tall enough for the gain to reach its middle: bandsForBox)
        if (hi - lo < least) { const double mid = 0.5 * (lo + hi); lo = juce::jmax (0.0, mid - 0.5 * least); hi = lo + least; }
        if (view.pageSpan() > 3.0) view.showSpan (t - 1.0, 2.0);
        view.selectBox (t - half, t + half, lo, hi);
        say ("A box round " + bandText (r.band) + " at peak " + juce::String (peaks.picked + 1) + ": move or resize it if you like, set the Gain and press Reduce.");
    }

    // ---- 3.75.0: the tracks. The same list twice: the ones the gain goes into, and the ones that hold the plug-in.
    void fillTracks()
    {
        std::vector<TickList::Row> g, p;
        if (auto* e = edit())
        {
            const auto tp = fixtools::buildTrackParts (*e, e->audibleStart(), e->audibleEnd(), {});
            for (size_t i = 0; i < tp.ids.size(); ++i)
            {
                TickList::Row r; r.id = tp.ids[i]; r.name = tp.names[i]; r.on = true;
                for (auto& old : gainTracks.rows) if (old.id == r.id) { r.on = old.on; r.note = old.note; r.level = old.level; r.hasLevel = old.hasLevel; }
                g.push_back (r);
                TickList::Row q; q.id = tp.ids[i]; q.name = tp.names[i];
                for (auto& old : plugTracks.rows) if (old.id == q.id) q.on = old.on;
                p.push_back (q);
            }
        }
        if (! gainTracks.rows.empty())                                           // (kept in the order the last look at the box put them in)
            std::stable_sort (g.begin(), g.end(), [this] (const TickList::Row& a, const TickList::Row& b)
            {
                auto at = [this] (const juce::Uuid& id) { for (size_t i = 0; i < gainTracks.rows.size(); ++i) if (gainTracks.rows[i].id == id) return (int) i; return 100000; };
                return at (a.id) < at (b.id);
            });
        gainTracks.rows = std::move (g); gainTracks.refresh();
        plugTracks.rows = std::move (p); plugTracks.refresh();
    }
    /** The mixer the piece is rendered through (as the render itself chooses it: MasterRender.cpp). */
    MixerState* mixer() const
    {
        RenderParams rp; makeRenderParams (app.project, app.project.mastering, rp);
        MixerState* m = rp.ownMixers ? app.project.mixerOfEdit (editId) : nullptr;
        if (m == nullptr) for (auto& x : app.project.mixers) if (x->id == rp.mixerId) m = x.get();
        if (m == nullptr && ! app.project.mixers.empty()) m = app.project.mixers.front().get();
        return m;
    }
    StripState* stripOf (const juce::Uuid& trackId) const
    {
        if (auto* m = mixer()) for (auto& s : m->stripPool) if (s->trackId == trackId) return s.get();
        return nullptr;
    }
    /** The faders, pans and mutes of the piece's mixer in one number: they change without a word to anyone (a fader moved is not a change to the
        project's structure), so the window looks every few seconds whether the piece needs measuring again. */
    juce::uint64 mixerLevelsHash() const
    {
        juce::uint64 h = 1469598103934665603ull;
        auto add = [&h] (float v) { juce::uint32 u = 0; std::memcpy (&u, &v, sizeof (u)); h = (h ^ u) * 1099511628211ull; };
        if (auto* m = mixer())
        {
            for (auto& s : m->stripPool) { add (s->gainDb.get()); add (s->inGainDb.get()); add (s->pan.get()); add (s->panL.get()); add (s->panR.get()); add (s->mute.get() ? 1.0f : 0.0f); add (s->solo.get() ? 1.0f : 0.0f); add (s->slots[kPostSlot].bypass.get() ? 1.0f : 0.0f); }
            for (auto& b : m->busPool) { add (b->gainDb.get()); add (b->mute.get() ? 1.0f : 0.0f); }
        }
        return h;
    }

    // ---- 3.75.0: the gain box. ReTouch's Gain (FixSpec Patch + RepairMode::Gain), made on a job thread for every piece of the ticked tracks the box
    //      reaches and put into the edit as new piece files (the originals are never touched). The box is drawn on the render, whose first sample is
    //      the edit's first sound (EditDef::audibleStart), so its times are the edit's from there.
    void reduce()
    {
        auto* e = edit();
        if (e == nullptr || profile == nullptr || fixing) return;
        if (! view.hasSelection()) { say ("Draw a box round the loud part first (or click a peak in the list and press Box it).", true); return; }
        if (app.engine.isRecording()) { say ("Stop recording first.", true); return; }
        if (shownHash == 0 || Project::undoHash (*e) != shownHash) { say ("The edit has changed since this picture was made: press Measure again, then draw the box on the new picture.", true); return; }
        const double db = gainSlider.getValue();
        if (std::abs (db) < 0.05) { say ("Gain 0 dB: nothing to change. Set the amount beside Gain first.", true); return; }
        const auto only = gainTracks.ticked();
        if (only.empty()) { say ("No track is ticked: tick the tracks the gain is to go into.", true); return; }
        const bool everyTrack = only.size() == gainTracks.rows.size();
        const double rate = e->sampleRate > 0.0 ? e->sampleRate : profile->rate;
        FixSpec spec; spec.kind = FixSpec::Kind::Patch;
        spec.repair.mode = RepairMode::Gain; spec.repair.gainDb = db <= -59.95 ? -200.0 : db; spec.repair.strength = 1.0; spec.repair.consonant = false; spec.repair.linkChannels = true;
        spec.lostKnown = true;
        std::vector<fixtools::RepairOp> ops;
        for (auto& r : view.selectionParts())                                    // (a rectangle, or a piece of a brushed or feathered shape with its mask)
        {
            auto sp = spec; sp.f0 = r.f0; sp.f1 = r.f1; sp.mask = r.mask; sp.maskLead = (juce::int64) std::llround (r.lead * rate);
            sp.repair.bands = bandsForBox (r.f0, r.f1, rate);
            ops.push_back ({ sp, (juce::int64) std::llround (r.t0 * rate), (juce::int64) std::llround (r.t1 * rate) });
        }
        if (ops.empty()) return;
        juce::String reachNote;                                                  // (one plain box that is too small for its pitch: say how much of the gain it can take)
        if (ops.size() == 1 && ops[0].spec.mask == nullptr && db > -59.95)
        {
            const double got = gainReachingMiddle (db, (double) (ops[0].t1 - ops[0].t0) / rate, ops[0].spec.f0, ops[0].spec.f1, rate, ops[0].spec.repair.bands);
            if (std::abs (got) < 0.8 * std::abs (db))
                reachNote = " The box is small for its pitch: about " + juce::String (got, 1) + " dB of it reaches its middle (a taller box, or one a fifth of a second long or more, takes it all).";
        }
        {   // 3.76.0: the note of it for the list of changes - what was asked for, on the edit's own timeline (the picture's begins at the edit's first sound)
            pendingGain = {}; pendingGain.id = juce::Uuid(); pendingGain.when = juce::Time::currentTimeMillis(); pendingGain.gainDb = db;
            const double off = editOffset();
            for (auto& b : view.selectedBoxes()) pendingGain.boxes.push_back ({ b.t0 + off, b.t1 + off, b.f0, b.f1 });
            pendingGain.shaped = view.brushed() || view.feathered();
            if (pendingGain.boxes.empty()) pendingGain.boxes.push_back ({ view.selT0() + off, view.selT1() + off, view.selF0(), view.selF1() });     // (all brush: its outline)
            if (! everyTrack) pendingGain.tracks = only;
        }
        const auto from = e->audibleStart(), to = e->audibleEnd();
        auto regions = std::make_shared<std::vector<EditRegion>> (e->regions);
        auto jobs = std::make_shared<std::vector<RegionJob>>();
        const auto folder = app.project.audioFolder().getChildFile ("Fixes");
        const juce::String what = "Gain " + juce::String (db, 1) + " dB, " + (everyTrack ? juce::String ("every track") : juce::String ((int) only.size()) + (only.size() == 1 ? " track" : " tracks"));
        fixing = true; progressValue = 0.0;
        say (what + "...");
        updateButtons();
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        const auto onlyTracks = everyTrack ? std::vector<juce::Uuid>() : only;
        job.start ([folder, regions, from, to, rate, ops, onlyTracks, jobs] (AudioJob& j) { return fixtools::prepareRepairJobs (folder, *regions, from, to, rate, ops, onlyTracks, *jobs, j); },
                   [safe, jobs, what, reachNote] (const juce::String& err)
                   {
                       if (safe == nullptr) { for (auto& rj : *jobs) for (auto& r : rj.results) r.file.deleteFile(); return; }     // (the window was closed meanwhile: its new files go)
                       safe->impl->reduceDone (*jobs, what, reachNote, err);
                   });
    }
    void reduceDone (std::vector<RegionJob>& jobs, const juce::String& what, const juce::String& reachNote, const juce::String& err)
    {
        fixing = false; progressValue = 0.0;
        auto drop = [&jobs] { for (auto& rj : jobs) for (auto& r : rj.results) r.file.deleteFile(); jobs.clear(); };
        auto* e = edit();
        if (err.isNotEmpty() || e == nullptr) { drop(); say (err.isNotEmpty() ? err : juce::String ("The piece has gone."), true); updateButtons(); return; }
        if (Project::undoHash (*e) != shownHash) { drop(); say ("The edit was changed while the gain was being made: nothing was put in. Press Measure again and try once more.", true); updateButtons(); return; }
        const auto before = e->regions;
        std::vector<juce::File> madeFiles;
        for (auto& rj : jobs) for (auto& r : rj.results) madeFiles.push_back (r.file);
        if (! applyRegionJobs (*e, before, jobs, "gain"))
        {
            e->regions = before; drop();
            say ("The gain could not be put into the edit (" + gApplyWhy + ").", true); updateButtons(); return;
        }
        // 3.76.0: into the list of changes (the dead ones - undone some other way, no piece plays their files - go from it now)
        e->specGains.erase (std::remove_if (e->specGains.begin(), e->specGains.end(), [e] (const EditDef::SpecGain& g) { return e->piecesOf (g) == 0; }), e->specGains.end());
        pendingGain.files = madeFiles;
        const bool inPiece = e->piecesOf (pendingGain, &pendingGain.at) > 0;      // (a box too short to cut a piece for leaves nothing: applyRegionJob's sliver rule)
        if (inPiece) { changes.picked = pendingGain.id; e->specGains.push_back (pendingGain); }
        fixtools::setUndoForEdit (app, editId, before, "spectrogram gain");      // (the edit window's Undo fix takes it off too)
        app.project.changed();
        shownHash = Project::undoHash (*e);                                      // (the picture's times still hold: a gain moves nothing)
        soundChangedHere();
        if (! inPiece) say ("The box is too short to cut a piece for (under six hundredths of a second): nothing was changed. Make it longer.", true);
        else say (what + ": done and in the edit - Play plays it. " + (autoToggle.getToggleState() ? "The piece is rendered again to show and measure it." : "Press Measure again to see and measure it.")
                  + " It is in the list of Changes." + reachNote, reachNote.isNotEmpty());
        fillTracks(); scheduleBreakdown(); fillChanges();
        updateButtons();
    }

    // ---- 3.76.0: the list of changes. Every gain box in the piece is noted in the edit (EditDef::specGains) with the files it made, and every
    //      track of every piece remembers what it played before a fix swapped a file in (RegionFile::before). So ANY one box can be taken off again,
    //      whenever, whatever else has been done to the edit since: the pieces that play its files go back to what they played before it, and the
    //      cuts made for it heal where both sides are whole again (EditDef::takeOffFiles).
    const EditDef::SpecGain* findChange (const juce::Uuid& id) const { if (auto* e = edit()) for (auto& g : e->specGains) if (g.id == id) return &g; return nullptr; }
    /** Where a change is now, in the picture's seconds: its boxes move with its pieces when the edit before them is changed. */
    double changeShift (const EditDef& e, const EditDef::SpecGain& g) const
    {
        juce::int64 first = g.at;
        if (e.piecesOf (g, &first) == 0 || e.sampleRate <= 0.0) return 0.0;
        return (double) (first - g.at) / e.sampleRate;
    }
    void fillChanges()
    {
        changes.rows.clear();
        if (auto* e = edit())
        {
            const double off = editOffset();
            int number = 0;
            for (auto& g : e->specGains)
            {
                if (e->piecesOf (g) == 0) continue;                               // (dead: see reduceDone)
                ++number;
                ChangesList::Row r; r.id = g.id; r.number = number;
                const double shift = changeShift (*e, g);
                double t0 = 1.0e12, t1 = 0.0, f0 = 1.0e12, f1 = 0.0;
                for (auto& b : g.boxes) { t0 = juce::jmin (t0, b.t0); t1 = juce::jmax (t1, b.t1); f0 = juce::jmin (f0, b.f0); f1 = juce::jmax (f1, b.f1); }
                r.seconds = juce::jmax (0.0, t0 - off + shift);
                r.gain = (g.gainDb > 0.0 ? "+" : "") + juce::String (g.gainDb, 1) + " dB";
                const bool allPitches = f0 <= 20.0 && profile != nullptr && f1 >= profile->rate * 0.49;
                r.band = (allPitches ? juce::String ("all pitches") : (f0 < 1.0 ? "under " + hzText (f1) : hzText (f0) + " - " + hzText (f1)) + " Hz")
                         + (g.boxes.size() > 1 ? "  (" + juce::String ((int) g.boxes.size()) + " boxes)" : juce::String()) + (g.shaped ? "  shaped" : "");
                r.tracks = g.tracks.empty() ? juce::String ("every track") : juce::String ((int) g.tracks.size()) + (g.tracks.size() == 1 ? " track" : " tracks");
                juce::StringArray names;
                for (auto& t : g.tracks) for (auto& row : gainTracks.rows) if (row.id == t) names.add (row.name);
                r.tip = "Gain " + r.gain + " at " + clockText (r.seconds) + ", " + juce::String (t1 - t0, 2) + " s long, " + r.band + ", " + (names.isEmpty() ? r.tracks : names.joinIntoString (", "))
                        + ". Made " + juce::Time (g.when).toString (true, true, false, true) + ". Click: go there. Double-click: change it.";
                changes.rows.push_back (std::move (r));
            }
            std::reverse (changes.rows.begin(), changes.rows.end());              // the newest at the top
        }
        if (changes.pickedRow() < 0) changes.picked = juce::Uuid::null();
        changes.refresh();
        const int n = (int) changes.rows.size();
        changesNote.setText (n == 0 ? juce::String ("No gain boxes in this piece. Each one made with Reduce is listed here, and can be taken off again - or changed - at any time.")
                                    : juce::String (n) + (n == 1 ? " gain box is" : " gain boxes are") + " in this piece, the newest first. Click one to go to it.", juce::dontSendNotification);
        updateChangeButtons();
    }
    void updateChangeButtons()
    {
        const bool one = changes.pickedRow() >= 0 && ! fixing;
        takeOffBtn.setEnabled (one); againBtn.setEnabled (one && profile != nullptr && pyramid != nullptr);
        undoBtn.setEnabled (! changes.rows.empty() && ! fixing);
        undoBtn.setButtonText (changes.rows.empty() ? juce::String ("Undo") : "Undo (" + juce::String ((int) changes.rows.size()) + ")");
    }
    /** The boxes of a change as they lie in the picture now. */
    std::vector<denoise::SelRect> boxesNow (const EditDef& e, const EditDef::SpecGain& g) const
    {
        std::vector<denoise::SelRect> v; const double d = changeShift (e, g) - editOffset();
        for (auto& b : g.boxes) v.push_back ({ b.t0 + d, b.t1 + d, b.f0, b.f1, true });
        return v;
    }
    void goToChange (int row)
    {
        auto* e = edit();
        if (e == nullptr || ! juce::isPositiveAndBelow (row, (int) changes.rows.size())) return;
        changes.picked = changes.rows[(size_t) row].id; changes.refresh(); updateChangeButtons();
        const auto* g = findChange (changes.picked);
        if (g == nullptr || profile == nullptr) return;
        const auto bs = boxesNow (*e, *g);
        if (bs.empty()) return;
        double t0 = 1.0e12, t1 = 0.0; for (auto& b : bs) { t0 = juce::jmin (t0, b.t0); t1 = juce::jmax (t1, b.t1); }
        const double span = juce::jlimit (2.0, juce::jmax (2.0, profile->seconds()), (t1 - t0) * 4.0);
        if (pyramid != nullptr) view.showSpan (0.5 * (t0 + t1) - 0.5 * span, span);
        setPlayheadAll (juce::jmax (0.0, t0)); wave.follow (t0);
        if (playing) startPlay(); else showPlayhead();
    }
    /** Takes one change off again. False (and says why) when nothing could be put back. */
    bool takeOffChange (const juce::Uuid& id, bool sayIt = true)
    {
        auto* e = edit();
        if (e == nullptr || fixing) return false;
        const auto* found = findChange (id);
        if (found == nullptr) return false;
        if (app.engine.isRecording()) { say ("Stop recording first.", true); return false; }
        const auto g = *found;
        auto lookup = [this, e] (const EditRegion& r, const RegionFile& f, RegionFile::Before& o) { return app.project.originalFileOf (*e, r, f, o); };
        const int n = e->takeOffFiles (g.files, lookup);
        e->specGains.erase (std::remove_if (e->specGains.begin(), e->specGains.end(), [&id] (const EditDef::SpecGain& x) { return x.id == id; }), e->specGains.end());
        if (changes.picked == id) changes.picked = juce::Uuid::null();
        app.project.changed();                                                  // (one undo step: Ctrl + Z brings the box, and its entry here, back)
        shownHash = Project::undoHash (*e);
        soundChangedHere();
        if (sayIt)
            say (n > 0 ? "Gain " + juce::String (g.gainDb, 1) + " dB: taken off again - Play plays the piece without it." + (autoToggle.getToggleState() ? juce::String (" It is rendered again to show and measure it.") : juce::String (" Press Measure again."))
                       : juce::String ("That gain box was no longer in any piece: it is off the list."), n == 0);
        fillTracks(); scheduleBreakdown(); fillChanges();
        updateButtons();
        return n > 0;
    }
    /** Undo (the Peaks and gain page): the newest gain box that is in the piece. */
    void undoLast() { if (! changes.rows.empty()) takeOffChange (changes.rows.front().id); }
    /** Change it: the box is taken off and put back as the selection, with its gain and its tracks set as they were - alter what you like, Reduce. */
    void changeAgain (const juce::Uuid& id)
    {
        auto* e = edit();
        const auto* found = findChange (id);
        if (e == nullptr || found == nullptr || fixing) return;
        if (profile == nullptr || pyramid == nullptr) { say ("The picture is still being made: a moment.", true); return; }
        const auto g = *found; const auto bs = boxesNow (*e, g);
        if (! takeOffChange (id, false)) { say ("That gain box could not be taken off, so it cannot be changed.", true); return; }
        gainSlider.setValue (juce::jlimit (-24.0, 6.0, g.gainDb), juce::sendNotification);
        for (auto& row : gainTracks.rows) row.on = g.tracks.empty() || std::find (g.tracks.begin(), g.tracks.end(), row.id) != g.tracks.end();
        gainTracks.refresh();
        double t0 = 1.0e12, t1 = 0.0; for (auto& b : bs) { t0 = juce::jmin (t0, b.t0); t1 = juce::jmax (t1, b.t1); }
        if (t1 > t0) { const double span = juce::jlimit (2.0, juce::jmax (2.0, profile->seconds()), (t1 - t0) * 4.0); view.showSpan (0.5 * (t0 + t1) - 0.5 * span, span); }
        view.selectBoxes (bs);
        selectTab (kTabPeaks);
        say (juce::String ("The gain box is off, and its box is the selection again with its gain and tracks as they were: alter what you like and press Reduce.")
             + (g.shaped ? " (It was brushed or feathered: what is selected is its outline - feather it as you want it now.)" : ""));
    }

    // ---- 3.75.0: which tracks are in the box - or, with no box, at the picked peak: each track's own level there (its mono mix as the edit plays
    //      it, in the box's band), with its fader added. Before its plug-ins and its pan: a guide to where the sound comes from, not the render.
    void scheduleBreakdown() { breakdownDue = juce::Time::getMillisecondCounter() + 250; if (breakdownDue == 0) breakdownDue = 1; }
    void startBreakdown()
    {
        breakdownDue = 0;
        const int gen = ++*breakdownGen;
        auto* e = edit();
        double t0 = 0.0, t1 = 0.0, f0 = 0.0, f1 = 1.0e9; juce::String cap;
        if (e != nullptr && profile != nullptr && view.hasSelection())
        {
            t0 = view.selT0(); t1 = view.selT1(); f0 = view.selF0(); f1 = view.selF1();
            if (t1 - t0 > 30.0) { const double mid = 0.5 * (t0 + t1); t0 = mid - 15.0; t1 = mid + 15.0; }
            cap = "Each track's level in the box (loudest first)";
        }
        else if (e != nullptr && profile != nullptr && juce::isPositiveAndBelow (peaks.picked, (int) peaks.rows.size()))
        {
            const double t = (double) peaks.rows[(size_t) peaks.picked].ev.frame / profile->rate;
            t0 = t - 0.025; t1 = t + 0.025;
            cap = "Each track's level at peak " + juce::String (peaks.picked + 1) + " (loudest first)";
        }
        else
        {
            for (auto& r : gainTracks.rows) { r.hasLevel = false; r.note.clear(); r.level = wavestats::kNone; }
            gainTracks.refresh();
            tracksCaption.setText ("The tracks the gain goes into", juce::dontSendNotification);
            return;
        }
        tracksCaption.setText (cap, juce::dontSendNotification);
        const double rate = e->sampleRate > 0.0 ? e->sampleRate : profile->rate;
        const juce::int64 a = e->audibleStart() + (juce::int64) std::llround (juce::jmax (0.0, t0) * rate), b = e->audibleStart() + (juce::int64) std::llround (t1 * rate);
        if (b - a < 16) return;
        auto tp = std::make_shared<fixtools::TrackParts> (fixtools::buildTrackParts (*e, a, b, {}));
        const int n = (int) (b - a);
        auto counter = breakdownGen;
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        std::thread ([tp, n, rate, f0, f1, gen, counter, safe]
        {
            try
            {
                std::vector<double> lv (tp->parts.size(), wavestats::kNone);
                fixtools::ReaderCache cache; std::vector<float> buf ((size_t) n);
                for (size_t i = 0; i < tp->parts.size(); ++i)
                {
                    if (counter->load() != gen) return;
                    fixtools::readTrackMix (tp->parts[i], 0, n, buf.data(), &cache);
                    lv[i] = bandLevelDb (buf.data(), n, rate, f0, f1);
                }
                juce::MessageManager::callAsync ([safe, gen, ids = tp->ids, lv] { if (safe != nullptr) safe->impl->breakdownDone (gen, ids, lv); });
            }
            catch (...) {}
        }).detach();
    }
    void breakdownDone (int gen, const std::vector<juce::Uuid>& ids, const std::vector<double>& lv)
    {
        if (gen != breakdownGen->load()) return;
        for (auto& r : gainTracks.rows)
        {
            r.hasLevel = true; r.level = wavestats::kNone; r.note = "-"; r.tip.clear();
            for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == r.id && ! none (lv[i]))
            {
                auto* s = stripOf (r.id);
                const double fader = s != nullptr ? (double) s->gainDb.get() + (double) s->inGainDb.get() : 0.0;
                const bool muted = s != nullptr && s->mute.get();
                r.level = muted ? wavestats::kNone : lv[i] + fader;
                r.note = muted ? juce::String ("muted") : juce::String (lv[i] + fader, 1) + " dB";
                r.tip = r.name + ": " + juce::String (lv[i], 1) + " dB in the track itself" + (std::abs (fader) > 0.05 ? ", " + juce::String (lv[i] + fader, 1) + " dB with its fader and input gain" : juce::String())
                        + ". Before its plug-ins and its pan. A click switches whether the gain goes into this track.";
            }
        }
        std::stable_sort (gainTracks.rows.begin(), gainTracks.rows.end(), [] (const TickList::Row& a, const TickList::Row& b) { return a.level > b.level; });
        gainTracks.refresh();
    }

    // ---- 3.75.0: the mastering plug-in: one instance on every ticked track, in the track's post-fader slot (Common.h: kPostSlot) - after its fader,
    //      just before the mix. The project's post-fader slots are switched on when the first goes in. The engine lines the tracks up again by itself
    //      (its delay compensation counts the slot), in the render as in playing.
    InsertSlot* postSlot (const juce::Uuid& trackId) const { auto* s = stripOf (trackId); return s != nullptr ? &s->slots[kPostSlot] : nullptr; }
    std::vector<InsertSlot*> plugSlots() const               // the ticked tracks' slots that hold the plug-in
    {
        std::vector<InsertSlot*> v;
        if (plugId.isEmpty()) return v;
        for (auto& r : plugTracks.rows) if (auto* sl = postSlot (r.id)) if (sl->isLoaded() && app.plugins.identityOf (sl->getProcessorUnsafe()) == plugId) v.push_back (sl);
        return v;
    }
    /** Reads the slots as the project has them (the mixer window may have changed them): which plug-in most of the tracks hold is "the" plug-in. */
    void lookAtPlugs()
    {
        std::map<juce::String, int> count; std::map<juce::String, juce::String> nameOf;
        for (auto& r : plugTracks.rows) if (auto* sl = postSlot (r.id)) if (sl->isLoaded())
        {
            const auto id = app.plugins.identityOf (sl->getProcessorUnsafe());
            if (id.isNotEmpty()) { ++count[id]; nameOf[id] = sl->getName(); }
        }
        if (count.find (plugId) == count.end()) plugId.clear();
        if (plugId.isEmpty()) { int best = 0; for (auto& c : count) if (c.second > best) { best = c.second; plugId = c.first; } }
        const bool live = app.project.postFaderSlots;
        int n = 0, maxLat = 0; bool anyBypass = false;
        for (auto& r : plugTracks.rows)
        {
            auto* sl = postSlot (r.id);
            r.enabled = sl != nullptr; r.note.clear(); r.key.clear(); r.keyLit = false;
            r.tip = "A click on the tick box puts the plug-in on this track, or takes it off. Window (or a double-click) opens THIS track's own plug-in: what it is set to, and the sound of this track going through it as the piece plays.";
            if (sl == nullptr) { r.on = false; r.note = "not in this mixer"; r.tip = "This track has no strip in the piece's mixer."; continue; }
            const bool loaded = sl->isLoaded();
            const auto id = loaded ? app.plugins.identityOf (sl->getProcessorUnsafe()) : juce::String();
            if (loaded && id == plugId && plugId.isNotEmpty())
            {
                r.on = true; ++n; maxLat = juce::jmax (maxLat, sl->latencySamples()); anyBypass = anyBypass || sl->bypass.get();
                r.key = "Window"; r.keyLit = app.plugins.editorOpen (sl->getProcessorUnsafe());      // (3.77.0: each track's own plug-in window)
            }
            else if (slotInUse (*sl))
            {
                r.on = false; r.enabled = false; r.note = loaded ? sl->getName() : sl->savedName + " (not loaded)";
                r.tip = "This track's post-fader slot already holds another plug-in (" + r.note + "). Take it out in the mixer window to use the slot from here.";
            }
            else if (plugId.isNotEmpty()) r.on = false;                           // (with no plug-in chosen yet the ticks are simply which tracks are wanted)
        }
        plugTracks.refresh();
        plugName.setText (plugId.isEmpty() ? juce::String ("(none yet)") : nameOf[plugId] + "  -  on " + juce::String (n) + (n == 1 ? " track" : " tracks"), juce::dontSendNotification);
        plugDelay.setText (n == 0 ? juce::String() : ! live ? juce::String ("The project's post-fader slots are OFF: it is not heard")
                                  : maxLat > 0 ? "Delay " + juce::String (maxLat) + " samples: the mixer lines the tracks up again" : juce::String ("No delay"), juce::dontSendNotification);
        plugDelay.setColour (juce::Label::textColourId, n > 0 && ! live ? theme::warn : theme::dimText);
        bypassToggle.setToggleState (anyBypass, juce::dontSendNotification);
        chooseBtn.setButtonText (plugId.isEmpty() ? "Choose plug-in..." : "Change plug-in...");
        openBtn.setEnabled (n > 0); removeBtn.setEnabled (n > 0); bypassToggle.setEnabled (n > 0);
    }
    void slotsChanged()
    {
        app.project.markDirtyForUndo(); app.engine.checkLatency();
        app.project.structure.sendChangeMessage();                               // (the mixer windows draw their slots again)
        lookAtPlugs();
        soundChangedHere();
    }
    bool postSlotsOn()
    {
        if (app.project.postFaderSlots) return true;
        app.project.postFaderSlots = true;                                       // (as Project menu > Post-fader slots)
        app.engine.rebuildPlan();
        app.project.markDirty();
        app.project.structureChanged();
        app.setNotice ("Post-fader slots on: every audio track and bus has one more insert slot, under its fader - the Mastering window's plug-in sits there.");
        return true;
    }
    /** Puts an instance on one track: a copy of one that is already there (with its settings), or a new one of 'desc'.
        A plug-in that shows a window of its own while it loads runs a modal loop, in which this window can be closed: 'gone' is then set, and
        nothing of this window may be touched after it (as the mixer's slots do: MixerWindow.cpp). */
    bool putOn (const juce::Uuid& trackId, const juce::PluginDescription* desc, bool& gone)
    {
        gone = false;
        auto* sl = postSlot (trackId); if (sl == nullptr) return false;
        auto& a = app; juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        const double sr = a.engine.getSampleRate(); const int blk = a.engine.getMaxBlock();
        juce::String err; std::unique_ptr<InsertProcessor> p;
        const auto have = plugSlots();
        if (desc != nullptr) p = a.plugins.create (*desc, sr, blk, err);
        else if (! have.empty()) p = a.plugins.duplicate (have.front()->getProcessorUnsafe(), sr, blk, err);
        if (safe == nullptr) { gone = true; return false; }
        if (p == nullptr) { showError ("Plug-in", "Could not load the plug-in:\n" + (err.isNotEmpty() ? err : juce::String ("no reason given"))); return false; }
        postSlotsOn();
        app.plugins.closeEditorsFor (sl->getProcessorUnsafe());
        if (! sl->set (std::move (p), sr, blk, 2)) { showError ("Plug-in", "The slot is busy (a plug-in is stuck in its work): nothing was changed."); return false; }
        return true;
    }
    void takeOff (const juce::Uuid& trackId)
    {
        auto* sl = postSlot (trackId); if (sl == nullptr || ! sl->isLoaded()) return;
        app.plugins.closeEditorsFor (sl->getProcessorUnsafe());
        if (! sl->clear (app.engine.getSampleRate(), app.engine.getMaxBlock(), 2)) showError ("Plug-in", "The slot is busy (a plug-in is stuck in its work): nothing was changed.");
    }
    void plugTicked (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) plugTracks.rows.size())) return;
        if (plugId.isEmpty()) return;                                             // (nothing chosen yet: the tick is remembered for Choose)
        const auto id = plugTracks.rows[(size_t) row].id;
        bool gone = false;
        if (plugTracks.rows[(size_t) row].on) putOn (id, nullptr, gone); else takeOff (id);
        if (gone) return;
        slotsChanged();
    }
    void choosePlugin()
    {
        if (plugTracks.rows.empty()) return;
        if (plugId.isEmpty() && plugTracks.countOn() == 0) { say ("Tick the tracks the plug-in is to go on first (All ticks every track).", true); return; }
        auto descs = std::make_shared<std::vector<juce::PluginDescription>>();
        auto m = app.plugins.buildPluginMenu (1000, *descs);
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chooseBtn), [safe, descs] (int r)
        {
            if (safe == nullptr || r < 1000 || (size_t) (r - 1000) >= descs->size()) return;
            auto& i = *safe->impl;
            const auto desc = (*descs)[(size_t) (r - 1000)];
            std::vector<juce::Uuid> want;
            for (auto& row : i.plugTracks.rows) if (row.on && row.enabled) want.push_back (row.id);
            bool first = true, ok = true, gone = false;
            for (auto& id : want)                                                // (the first a new one, the others copies of it: one plug-in's settings on all of them)
            {
                if (! first && ! i.plugId.isEmpty()) { ok = i.putOn (id, nullptr, gone) && ok; if (gone) return; }
                else
                {
                    ok = i.putOn (id, &desc, gone) && ok;
                    if (gone) return;                                            // (the window was closed while the plug-in loaded)
                    if (ok) if (auto* sl = i.postSlot (id)) if (sl->isLoaded()) { i.plugId = i.app.plugins.identityOf (sl->getProcessorUnsafe()); first = false; }
                }
                if (! ok) break;
            }
            i.slotsChanged();
            if (ok) { i.openPlug (-1); i.say ("The plug-in is on " + juce::String ((int) want.size()) + (want.size() == 1 ? " track" : " tracks") + ", after each fader. Its window is the first track's; with Link the settings on, the others take its settings. Play plays the piece through it."); }
        });
    }
    void openPlug (int row)
    {
        InsertSlot* sl = nullptr;
        if (juce::isPositiveAndBelow (row, (int) plugTracks.rows.size())) { sl = postSlot (plugTracks.rows[(size_t) row].id); if (sl != nullptr && ! sl->isLoaded()) sl = nullptr; }
        else { const auto have = plugSlots(); if (! have.empty()) sl = have.front(); }
        if (sl != nullptr) app.plugins.showEditor (sl->getProcessorUnsafe());
    }
    void removePlug()
    {
        const int n = (int) plugSlots().size();
        if (n == 0) return;
        juce::Component::SafePointer<SpecEditComponent> safe (&owner);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Mastering")
                                          .withMessage ("Take the plug-in off " + juce::String (n == 1 ? "its track" : "all " + juce::String (n) + " tracks") + "? Its settings go with it.")
                                          .withButton ("Take it off").withButton ("Cancel"),
                                      [safe] (int r)
                                      {
                                          if (safe == nullptr || r != 1) return;
                                          auto& i = *safe->impl;
                                          for (auto& row : i.plugTracks.rows) if (auto* sl = i.postSlot (row.id)) if (sl->isLoaded() && i.app.plugins.identityOf (sl->getProcessorUnsafe()) == i.plugId) i.takeOff (row.id);
                                          i.plugId.clear();
                                          i.slotsChanged();
                                      });
    }
    void bypassPlugs (bool on)
    {
        for (auto* sl : plugSlots()) sl->bypass.set (on);
        app.project.markDirtyForUndo(); app.engine.checkLatency();
        lookAtPlugs(); soundChangedHere();
    }
    /** Link the settings. The instances' parameters are looked at a few times a second; the one whose own window is open and whose controls moved
        is the one being set by hand, and a quarter of a second after its last move ALL its settings are copied to the others (its whole state:
        PluginHost::copyState). Whatever was set - linked or not - the piece's sound changed, so it is measured again.

        3.75.2: 3.75.0 set each moved parameter on the others one by one. A plug-in only takes a parameter set from outside as its mixer runs, and
        an edit's own mixer rests unless that edit plays (AudioEngine: mixerIsLive) - so the others never took them, were saved and rendered with
        their old settings, and their parameters, read back, looked "moved" again for ever (seen on 28 November 2026: a 2 kHz high-pass on eleven
        tracks, heard on none but the first, and a window that went on saying "up to date"). A whole state is taken at once, mixer or no mixer. */
    void lookAtPlugParams()
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto slots = plugSlots();
        {   // 3.77.0: the rows' Window keys are lit for the tracks whose plug-in window is open
            juce::String open;
            for (auto& r : plugTracks.rows) { auto* sl = r.key.isNotEmpty() ? postSlot (r.id) : nullptr; open << (sl != nullptr && sl->isLoaded() && app.plugins.editorOpen (sl->getProcessorUnsafe()) ? '1' : '0'); }
            if (open != openKeys)
            {
                openKeys = open;
                for (int i = 0; i < (int) plugTracks.rows.size(); ++i) plugTracks.rows[(size_t) i].keyLit = open[i] == '1';
                plugTracks.repaintRows();
            }
        }
        bool same = slots.size() == seen.size();
        for (size_t i = 0; same && i < slots.size(); ++i) same = seen[i].proc == slots[i]->getProcessorUnsafe() && (int) seen[i].vals.size() == seen[i].proc->numParams();
        if (! same)
        {
            seen.clear(); linkFrom = nullptr; linkCopiedFrom = nullptr; linkDue = 0; linkRuns = 0; linkResting = false;
            for (auto* sl : slots) { Seen s; s.proc = sl->getProcessorUnsafe(); const int np = s.proc->numParams(); s.vals.resize ((size_t) np); for (int k = 0; k < np; ++k) s.vals[(size_t) k] = s.proc->getParamNorm (k); seen.push_back (std::move (s)); }
            return;
        }
        if (linkResting && (linkFrom == nullptr || ! app.plugins.editorOpen (linkFrom))) { linkResting = false; linkRuns = 0; }
        const bool quiet = (juce::int32) (now - linkQuietUntil) < 0;             // (just after a copy: the others' parameters are still arriving)
        for (auto& s : seen)
        {
            bool moved = false;
            for (int k = 0; k < (int) s.vals.size(); ++k)
            {
                const float v = s.proc->getParamNorm (k);
                if (std::abs (v - s.vals[(size_t) k]) <= 1.0e-6f) continue;
                s.vals[(size_t) k] = v;
                moved = moved || s.proc->paramAutomatable (k);                    // (a meter the plug-in shows as a parameter is no setting)
            }
            if (! moved || linkResting || ! app.plugins.editorOpen (s.proc)) continue;      // (only a plug-in whose window is open is being set by hand)
            // 3.77.0: the quiet moment after a copy is for the OTHERS (their settings, just written, read as "moved"). 3.75.2 kept it for the one
            // being set as well, so what was done to it in the 0.8 s after a copy was never copied: a band made, then at once switched to a
            // high-pass, left the other tracks with the band alone; and the end of a long drag was lost in the same way.
            if (quiet && s.proc != linkCopiedFrom) continue;
            if (linkDue == 0) linkRunStart = now;
            linkFrom = s.proc; linkDue = now + 250; if (linkDue == 0) linkDue = 1;
        }
        if (linkDue == 0 || ((juce::int32) (now - linkDue) < 0 && now - linkRunStart < 2000)) return;      // (still moving: at the latest every two seconds)
        const bool stillMoving = (juce::int32) (now - linkDue) < 0;
        linkDue = 0;
        // (a plug-in whose settings never stand still - they move by themselves - must not copy and render for ever: after half a minute of it, no more)
        linkRuns = stillMoving ? linkRuns + 1 : 0;
        if (linkRuns > 15) { linkResting = true; say ("This plug-in's settings keep moving by themselves: Link and the automatic measuring leave it alone until its window is closed. (Measure again measures.)", true); return; }
        bool there = false; std::vector<InsertProcessor*> others;
        for (auto* sl : slots) { auto* p = sl->getProcessorUnsafe(); if (p == linkFrom) there = true; else others.push_back (p); }
        if (! there) return;
        if (linkToggle.getToggleState() && ! others.empty())
        {
            const int n = app.plugins.copyState (linkFrom, others);
            for (auto& s : seen) if (s.proc != linkFrom) for (int k = 0; k < (int) s.vals.size(); ++k) s.vals[(size_t) k] = s.proc->getParamNorm (k);
            linkQuietUntil = now + 800; linkCopiedFrom = linkFrom;
            if (n > 0) say ("Link: the plug-in's settings were copied to the other " + juce::String (n) + (n == 1 ? " track." : " tracks.") + " (Window, on a track's row, shows that track's own.)");
        }
        app.project.markDirty(); app.engine.checkLatency();                      // (a setting may change how late the plug-in is)
        soundChangedHere();
    }

    // ---- playing. 3.75.2: the piece itself, live through its mixer, as the edit window plays it (AppContext::playEdit) - so a gain box and the
    //      plug-in are heard at once, and the plug-in is given the sound (its own display moves). 3.74.0 / 3.75.0 played the RENDER straight to the
    //      outputs: nothing went through the mixer, the plug-in showed nothing, and a change was heard only after the next render. The window's
    //      time is the render's, which starts at the edit's first sound: editOffset() is the difference.
    double editOffset() const { auto* e = edit(); return e != nullptr && e->sampleRate > 0.0 ? (double) e->audibleStart() / e->sampleRate : 0.0; }
    bool mine() const { return app.isPlaying() && app.playInfo.kind == AppContext::PlayInfo::Kind::Edit && app.playInfo.id == editId; }
    /** The playhead in both pictures. */
    void setPlayheadAll (double s)
    {
        wave.setPlayhead (s);
        view.placeCue (s); lastCue = view.cueTime();
    }
    void startPlay()
    {
        if (profile == nullptr || edit() == nullptr) return;
        if (app.engine.isRecording()) { showError ("Mastering", "Stop recording first."); return; }
        if (playing) { if (mine()) app.stopPlayback(); playing = false; }
        const bool sel = wave.hasSelection();
        double from = sel ? wave.selStart() : wave.playheadSeconds();
        const double to = sel ? wave.selEnd() : -1.0;
        if (! sel && from >= profile->seconds() - 0.05) from = 0.0;
        const double off = editOffset();
        const auto err = app.playEdit (editId, off + from, off + (sel ? to : profile->seconds()));
        if (err.isNotEmpty()) { say (err, true); return; }
        playing = true; playStart = from; lastPlayPos = from;
        playBtn.setButtonText ("Stop");
    }
    void stopPlay()
    {
        if (! playing) return;
        const bool m = mine();
        const double pos = m ? app.playheadSeconds() - editOffset() : -1.0;
        if (m) app.stopPlayback();
        playing = false;
        playBtn.setButtonText ("Play");
        setPlayheadAll (app.playheadFollows && pos >= 0.0 && ! wave.hasSelection() ? pos : playStart);
        showPlayhead();
    }
    void pollPlay()
    {
        if (! playing) return;
        if (mine() && ! app.engine.playbackFinished())
        {
            const double pos = juce::jmax (0.0, app.playheadSecondsSmooth() - editOffset());
            wave.setPlayhead (pos); wave.follow (pos);
            if (pos >= view.visT1() && lastPlayPos < view.visT1() && lastPlayPos >= view.visT0() && profile != nullptr && view.visT1() < profile->seconds() - 1.0e-6) view.showFrom (view.visT1());     // the page turns with it
            lastPlayPos = pos;
            meters.setPlayhead (pos, true);
            timeLabel.setText (clockText (pos), juce::dontSendNotification);
            return;
        }
        const double last = wave.playheadSeconds();
        if (mine()) app.stopPlayback();
        playing = false;
        playBtn.setButtonText ("Play");
        setPlayheadAll (app.playheadFollows && ! wave.hasSelection() ? last : playStart);
        showPlayhead();
    }
    /** The playhead standing still: the time, and the meters show what is under it. */
    void showPlayhead()
    {
        timeLabel.setText (clockText (wave.playheadSeconds()), juce::dontSendNotification);
        if (! playing) meters.setPlayhead (wave.playheadSeconds(), false);
    }

    void selectTab (int t)
    {
        tab = juce::jlimit (0, (int) kTabCount - 1, t);
        for (int i = 0; i < kTabCount; ++i) tabBtn[i].setToggleState (i == tab, juce::dontSendNotification);
        if (auto* st = app.props.getUserSettings()) { st->setValue ("specEditTab", tab); app.props.saveIfNeeded(); }
        const bool f = tab == kTabFigures, p = tab == kTabPeaks, g = tab == kTabPlug, c = tab == kTabChanges;
        for (auto* x : std::initializer_list<juce::Component*> { &changesNote, &changes, &takeOffBtn, &againBtn }) x->setVisible (c);
        if (c) fillChanges();
        for (auto* c : std::initializer_list<juce::Component*> { &stats, &rmsCaption, &rmsBox, &refCaption, &refBox, &copyBtn }) c->setVisible (f);
        for (auto* c : std::initializer_list<juce::Component*> { &countCaption, &countBox, &peaks, &peaksNote, &boxBtn, &gainCaption, &gainSlider, &reduceBtn, &undoBtn, &tracksCaption, &gainTracks, &allBtn, &noneBtn }) c->setVisible (p);
        for (auto* c : std::initializer_list<juce::Component*> { &plugNote, &plugName, &plugDelay, &chooseBtn, &openBtn, &removeBtn, &plugAllBtn, &plugNoneBtn, &bypassToggle, &linkToggle, &plugTracks }) c->setVisible (g);
        if (g) lookAtPlugs();
        owner.resized();
    }

    void updateButtons()
    {
        const bool busy = renderJob != nullptr || analysis != nullptr || fixing;
        progress.setVisible (busy);
        measureBtn.setEnabled (! busy && edit() != nullptr && problem.isEmpty());
        playBtn.setEnabled (profile != nullptr);
        copyBtn.setEnabled (profile != nullptr);
        reduceBtn.setEnabled (profile != nullptr && ! fixing && view.hasSelection());
        updateChangeButtons();
        boxBtn.setEnabled (profile != nullptr && juce::isPositiveAndBelow (peaks.picked, (int) peaks.rows.size()));
        owner.resized();
    }

    void tick()
    {
        pollPlay();
        const auto now = juce::Time::getMillisecondCounter();
        {   // a click in the picture (or on its ruler) moved the playhead there
            const double cue = view.cueTime();
            if (std::abs (cue - lastCue) > 1.0e-9)
            {
                lastCue = cue;
                if (cue >= 0.0 && profile != nullptr) { wave.setPlayhead (cue); if (playing) startPlay(); else showPlayhead(); }
            }
        }
        if (breakdownDue != 0 && (juce::int32) (now - breakdownDue) >= 0) startBreakdown();
        if (now - lastPlugLook > 180 && owner.isShowing()) { lastPlugLook = now; lookAtPlugParams(); }
        if (fixing) { progressValue = (double) job.progress(); return; }
        if (renderJob != nullptr) { progressValue = renderJob->getProgress(); return; }
        if (analysis != nullptr) { progressValue = analysis->progress(); return; }
        if (! owner.isShowing()) return;
        if (wantRender && now - lastStoreLook > 700) { lastStoreLook = now; startRender(); return; }                        // (it was waiting for the Mastering window, a recording or a gain box)
        if (! dirty && now - lastLevelsLook > 1500)                               // (a fader, a pan or a mute moved: see mixerLevelsHash)
        {
            lastLevelsLook = now;
            if (mixerLevelsHash() != levelsHash) markDirty();
        }
        if (dirty && (now - dirtyAt > 1200 || now - dirtySince > 6000) && ! app.engine.isRecording())
        {
            dirty = false;
            if (auto* e = edit())
            {
                RenderParams rp;
                const auto perr = makeRenderParams (app.project, app.project.mastering, rp);
                if (perr != problem || (perr.isEmpty() && masterRenderFingerprint (app.project, *e, rp) != lastFp)) { staleAfterRender = false; lookAtPiece(); }
                else
                {
                    levelsHash = mixerLevelsHash();
                    if (! needsRender && shownHash != 0) shownHash = Project::undoHash (*e);     // (a marker, a name: the sound is the same, so the picture is still this edit's)
                }
                fillTracks(); fillChanges();
                if (tab == kTabPlug) lookAtPlugs();
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
    auto cap = [this] (juce::Label& l, const juce::String& t, juce::Justification j = juce::Justification::centredRight)
    {
        l.setText (t, juce::dontSendNotification); l.setFont (juce::FontOptions (12.0f)); l.setColour (juce::Label::textColourId, theme::dimText);
        l.setJustificationType (j); l.setMinimumHorizontalScale (1.0f); addAndMakeVisible (l);
    };
    cap (m.pieceCaption, "Piece"); cap (m.rmsCaption, "RMS windows"); cap (m.refCaption, "RMS 0 dB =");
    addAndMakeVisible (m.pieceBox);
    m.pieceBox.setTooltip ("The piece that is shown and measured: one of the Exports window's pieces. What you see is its render - the piece through its mixer, as the Exports window plays and exports it "
                           "(before the export's own level and dither).");
    m.pieceBox.onChange = [this] { auto& i = *impl; if (i.updating) return; const int k = i.pieceBox.getSelectedId() - 1; if (juce::isPositiveAndBelow (k, (int) i.pieceIds.size())) i.setPiece (i.pieceIds[(size_t) k]); };
    addAndMakeVisible (m.playBtn);
    m.playBtn.setTooltip ("Plays the piece itself, live through its mixer - as the edit window plays it - from the playhead, or the selected part (Space). A gain box and the plug-in are "
                          "heard at once. The picture, the figures and the meters are of the piece's last render: they follow when it has been rendered again.");
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
    m.autoToggle.setTooltip ("On: when the piece changes - an edit, a fader, a plug-in, a gain box from this window - its render is made again and measured again by itself, a moment after the change. "
                             "Off: only when Measure again is pressed (the picture and the figures then say they are of the last render).");
    addAndMakeVisible (m.fitBtn);
    m.fitBtn.setTooltip ("Shows the whole piece again, in the waveform and in the spectrogram (the mouse wheel zooms in and out where the mouse is; Shift + wheel moves along; double-click: the whole piece).");
    m.fitBtn.onClick = [this] { impl->wave.zoomToAll(); if (impl->pyramid != nullptr) impl->view.resetView(); };
    addAndMakeVisible (m.copyBtn);
    m.copyBtn.setTooltip ("Copies the figures - the whole piece's and the selection's - as text, to paste into a note or an e-mail.");
    m.copyBtn.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (impl->stats.asText (impl->pieceName())); impl->say ("The figures were copied."); };
    addAndMakeVisible (m.rmsBox); addAndMakeVisible (m.refBox);
    { int i = 0; for (auto* t : { "10 ms", "20 ms", "50 ms", "100 ms", "300 ms", "400 ms", "1 s", "3 s" }) m.rmsBox.addItem (t, ++i); }
    m.rmsBox.setTooltip ("How long the windows of Max. and Min. RMS level are (a new window every 10 ms).");
    m.refBox.addItem ("a full-scale sine (AES17)", 1); m.refBox.addItem ("a full-scale square wave", 2);
    m.refBox.setTooltip ("What an RMS level of 0 dB is. AES17: a sine wave that just reaches full scale reads 0 dB (3 dB more than the plain RMS). The other: a square wave at full scale reads 0 dB. "
                         "Set it as your other meter has it (iZotope RX has the same choice in its preferences).");
    auto* st = app.props.getUserSettings();
    {
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
        if (auto* s = app.props.getUserSettings()) { s->setValue ("specEditAuto", impl->autoToggle.getToggleState()); app.props.saveIfNeeded(); }
        if (impl->autoToggle.getToggleState() && impl->needsRender) { impl->staleAfterRender = false; impl->lookAtPiece(); }
    };

    addAndMakeVisible (m.wave); addAndMakeVisible (m.stats); addAndMakeVisible (m.meters);
    m.wave.samples = &m.samples; m.meters.samples = &m.samples;
    m.wave.onPlayhead = [this] (double s)
    {
        auto& i = *impl;
        i.setPlayheadAll (s);
        if (i.pyramid != nullptr) i.view.showTime (s);
        if (i.playing) i.startPlay(); else i.showPlayhead();                 // (a click while it plays: it carries on from there)
    };
    m.wave.onSelection = [this] { impl->selFromView = false; impl->showFigures(); };
    m.stats.onGoTo = [this] (juce::int64 frame)
    {
        auto& i = *impl;
        if (i.profile == nullptr || frame < 0) return;
        const double s = (double) frame / i.profile->rate;
        i.setPlayheadAll (s); i.wave.follow (s);
        if (i.pyramid != nullptr) i.view.showTime (s);
        if (i.playing) i.startPlay(); else i.showPlayhead();
    };

    // ---- 3.75.0: the spectrogram and its tools
    addAndMakeVisible (m.view); addAndMakeVisible (m.toolsBar); addAndMakeVisible (m.specBtn); addAndMakeVisible (m.metersBtn);
    m.view.setMel();
    m.view.setReferenceA (app.project.pitchPipe.refA);
    m.view.onNote = [this] (const juce::String& t) { impl->say (t); };
    m.view.onNeedDetail = [this] (const PictureRequest& rq) { impl->startDetail (rq); };
    m.view.onView = [this] { impl->sliceForView(); };
    m.view.livePlayhead = [this] { auto& i = *impl; return i.playing && i.mine() ? juce::jmax (0.0, i.app.playheadSecondsSmooth() - i.editOffset()) : -1.0; };
    m.view.onSelection = [this]
    {
        auto& i = *impl;
        if (i.view.hasSelection()) { i.wave.setSelection (i.view.selT0(), i.view.selT1()); i.selFromView = true; }      // (the figures of the selection: the box's stretch of time)
        else if (i.selFromView) { i.wave.setSelection (-1.0, -1.0); i.selFromView = false; }
        i.showFigures(); i.scheduleBreakdown(); i.updateButtons();
    };
    m.specBtn.setTooltip ("How the picture is drawn: sharpness, colours, and the levels shown. (The round control at the picture's bottom left rolls through the colour looks.)");
    m.specBtn.onClick = [this] { impl->openPictureSettings(); };
    m.metersBtn.setClickingTogglesState (true); m.metersBtn.setColour (juce::TextButton::buttonOnColourId, theme::accent);
    m.metersBtn.setToggleState (st == nullptr || st->getBoolValue ("specEditMeters", true), juce::dontSendNotification);
    m.metersBtn.setTooltip ("Shows or hides the meters under the picture (hidden, the picture has their room).");
    m.metersBtn.onClick = [this]
    {
        if (auto* s = app.props.getUserSettings()) { s->setValue ("specEditMeters", impl->metersBtn.getToggleState()); app.props.saveIfNeeded(); }
        impl->meters.setVisible (impl->metersBtn.getToggleState()); resized();
    };
    m.meters.setVisible (m.metersBtn.getToggleState());

    // ---- the three pages at the right
    static const char* const tabNames[] = { "Figures", "Peaks and gain", "Changes", "Plug-in" };
    static const char* const tabTips[] = { "The figures of the whole piece, and of the selected part.",
                                           "The highest true peaks, marked along the picture - and the gain box that brings one down: draw a box round the loud part, set the gain, press Reduce.",
                                           "Every gain box that is in this piece: go to one, take it off again, or change it - at any time, in any order.",
                                           "One plug-in on the tracks you tick, after each track's fader: the last thing before the mix." };
    for (int i = 0; i < Impl::kTabCount; ++i)
    {
        auto& b = m.tabBtn[i];
        b.setButtonText (tabNames[i]); b.setTooltip (tabTips[i]); b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, theme::accent);
        b.setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : i == Impl::kTabCount - 1 ? juce::Button::ConnectedOnLeft : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        b.onClick = [this, i] { impl->selectTab (i); };
        addAndMakeVisible (b);
    }
    // the peaks
    cap (m.countCaption, "Highest peaks"); cap (m.gainCaption, "Gain");
    cap (m.tracksCaption, "The tracks the gain goes into", juce::Justification::centredLeft);
    cap (m.peaksNote, {}, juce::Justification::centredLeft); m.peaksNote.setMinimumHorizontalScale (0.85f);
    for (int n : { 3, 5, 8, 10, 15, 20, 30, 50 }) m.countBox.addItem (juce::String (n), n);
    m.countBox.setEditableText (true);
    m.countBox.setText (juce::String (juce::jlimit (1, 100, st != nullptr ? st->getIntValue ("specEditPeaks", 10) : 10)), juce::dontSendNotification);
    m.countBox.setTooltip ("How many of the highest true peaks are listed and marked (1 to 100: pick one or type a number). Peaks less than half a second from a higher one belong to it: "
                           "a loud chord is one thing to bring down, not thirty peaks in a row.");
    m.countBox.onChange = [this]
    {
        auto& i = *impl;
        const int n = i.peakCount();
        i.countBox.setText (juce::String (n), juce::dontSendNotification);
        if (auto* s = app.props.getUserSettings()) { s->setValue ("specEditPeaks", n); app.props.saveIfNeeded(); }
        i.fillPeaks();
    };
    m.peaks.onPick = [this] (int i) { impl->pickPeak (i); };
    m.boxBtn.setTooltip ("Draws a box round the band that carries most of the picked peak (the band and the share of the peak's height it carries are in the list), a few waves long. "
                         "Move or resize it, add to it, feather it - then Reduce.");
    m.boxBtn.onClick = [this] { impl->boxPicked(); };
    m.gainSlider.setSliderStyle (juce::Slider::LinearHorizontal); m.gainSlider.setRange (-24.0, 6.0, 0.1); m.gainSlider.setScrollWheelEnabled (false);
    m.gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22); m.gainSlider.setDoubleClickReturnValue (true, -3.0);
    m.gainSlider.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    m.gainSlider.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("-+0123456789.").getDoubleValue(); };
    m.gainSlider.setValue (juce::jlimit (-24.0, 6.0, st != nullptr ? st->getDoubleValue ("specEditGainDb", -3.0) : -3.0), juce::dontSendNotification);
    m.gainSlider.onValueChange = [this] { if (auto* s = app.props.getUserSettings()) s->setValue ("specEditGainDb", impl->gainSlider.getValue()); };
    m.gainSlider.setTooltip ("How much quieter (or louder) the box is made. Set it, then Reduce (or G) puts it into each box you draw. Double-click: -3 dB.");
    m.reduceBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
    m.reduceBtn.setTooltip ("Gain: makes the sound inside the box quieter by the amount beside it - fading in and out with the box's feathering - on every ticked track, as ReTouch's Gain does. "
                            "It goes into the edit at once, as new piece files (the recordings themselves are never touched); the piece is then rendered and measured again. Key: G");
    m.reduceBtn.onClick = [this] { impl->reduce(); };
    m.undoBtn.setTooltip ("Takes the newest gain box in this piece off again. The Changes page lists them all, and takes off - or changes - any one of them.");
    m.undoBtn.onClick = [this] { impl->undoLast(); };
    m.gainTracks.onTick = [this] (int) { impl->updateButtons(); };
    m.allBtn.onClick = [this] { impl->gainTracks.setAll (true); }; m.noneBtn.onClick = [this] { impl->gainTracks.setAll (false); };
    m.allBtn.setTooltip ("Ticks every track."); m.noneBtn.setTooltip ("Unticks every track (then tick the ones the sound is in).");
    for (auto* c : std::initializer_list<juce::Component*> { &m.countBox, &m.peaks, &m.boxBtn, &m.gainSlider, &m.reduceBtn, &m.undoBtn, &m.gainTracks, &m.allBtn, &m.noneBtn }) addAndMakeVisible (c);
    // 3.76.0: the list of changes
    cap (m.changesNote, {}, juce::Justification::topLeft); m.changesNote.setMinimumHorizontalScale (0.9f);
    m.changes.onPick = [this] (int row) { impl->goToChange (row); };
    m.changes.onOpen = [this] (int row) { auto& i = *impl; if (juce::isPositiveAndBelow (row, (int) i.changes.rows.size())) i.changeAgain (i.changes.rows[(size_t) row].id); };
    m.takeOffBtn.setTooltip ("Takes the picked gain box off again: the pieces it changed play what they played before it (an earlier fix under it stays), and the cuts made for it go where the "
                             "audio is whole again. Any one, in any order. Ctrl + Z in the edit window brings it back. In the edit window, right-click a piece > Revert to the original audio does "
                             "the same for any fx clip.");
    m.takeOffBtn.onClick = [this] { auto& i = *impl; if (! i.changes.picked.isNull()) i.takeOffChange (i.changes.picked); };
    m.againBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
    m.againBtn.setTooltip ("Takes the picked gain box off and puts its box back as the selection, with its gain and its tracks set as they were (the Peaks and gain page): "
                           "alter the gain, the box, the feathering or the tracks, and press Reduce to make it again. (Double-click the box in the list does the same.)");
    m.againBtn.onClick = [this] { auto& i = *impl; if (! i.changes.picked.isNull()) i.changeAgain (i.changes.picked); };
    for (auto* c : std::initializer_list<juce::Component*> { &m.changes, &m.takeOffBtn, &m.againBtn }) addAndMakeVisible (c);
    // the plug-in
    cap (m.plugNote, "Tick the tracks, then choose: one plug-in on each ticked track, after the track's fader - the last thing before the mix (its post-fader slot in the mixer). Each track has its own instance.", juce::Justification::topLeft);
    m.plugNote.setMinimumHorizontalScale (0.9f);
    cap (m.plugName, {}, juce::Justification::centredLeft); m.plugName.setColour (juce::Label::textColourId, theme::text);
    fit::markMayShrink (m.plugName); fit::markMayShrink (m.plugDelay); fit::markMayShrink (m.peaksNote);       // (a long plug-in name is cut off, never a reason to scroll the window)
    cap (m.plugDelay, {}, juce::Justification::centredRight); m.plugDelay.setMinimumHorizontalScale (0.8f);
    m.plugTracks.wholeRowTicks = false;
    m.plugTracks.onTick = [this] (int row) { impl->plugTicked (row); };
    m.plugTracks.onOpen = [this] (int row) { impl->openPlug (row); };
    m.chooseBtn.setTooltip ("The plug-in for the ticked tracks: the first track gets a new one, the others copies of it. With one already there, this replaces it on every track that has it.");
    m.chooseBtn.onClick = [this] { impl->choosePlugin(); };
    m.openBtn.setTooltip ("Opens the plug-in's window (the first ticked track's). Each track has its own: the Window key on its row opens it.");
    m.openBtn.onClick = [this] { impl->openPlug (-1); };
    m.removeBtn.setTooltip ("Takes the plug-in off every track (it asks first).");
    m.removeBtn.onClick = [this] { impl->removePlug(); };
    m.bypassToggle.setTooltip ("Bypasses the plug-in on every track that has it.");
    m.bypassToggle.onClick = [this] { impl->bypassPlugs (impl->bypassToggle.getToggleState()); };
    m.linkToggle.setToggleState (st == nullptr || st->getBoolValue ("specEditLink", true), juce::dontSendNotification);
    m.linkToggle.setTooltip ("On: set the plug-in in one track's window, and a moment after you let go ALL its settings are copied to every other track's - one window sets them all. "
                             "A track added later starts as a copy of the first. Off: each track's plug-in is set on its own (double-click a track for its window).");
    m.linkToggle.onClick = [this] { if (auto* s = app.props.getUserSettings()) { s->setValue ("specEditLink", impl->linkToggle.getToggleState()); app.props.saveIfNeeded(); } impl->seen.clear(); };
    m.plugAllBtn.onClick = [this]
    {
        auto& i = *impl;
        std::vector<juce::Uuid> add;
        for (auto& row : i.plugTracks.rows) if (row.enabled && ! row.on) { row.on = true; add.push_back (row.id); }
        i.plugTracks.refresh();
        if (i.plugId.isEmpty()) return;                                          // (nothing chosen yet: the ticks are for Choose)
        bool gone = false;
        for (auto& id : add) { if (! i.putOn (id, nullptr, gone)) { if (gone) return; break; } }
        i.slotsChanged();
    };
    m.plugNoneBtn.onClick = [this] { auto& i = *impl; if (i.plugId.isNotEmpty()) { i.removePlug(); return; } i.plugTracks.setAll (false); };
    m.plugAllBtn.setTooltip ("Ticks every track (with a plug-in chosen: puts it on every track)."); m.plugNoneBtn.setTooltip ("Unticks every track (with a plug-in chosen: takes it off every track - it asks first).");
    for (auto* c : std::initializer_list<juce::Component*> { &m.plugTracks, &m.chooseBtn, &m.openBtn, &m.removeBtn, &m.bypassToggle, &m.linkToggle, &m.plugAllBtn, &m.plugNoneBtn }) addAndMakeVisible (c);

    app.project.addChangeListener (this);
    setWantsKeyboardFocus (true);
    for (auto* ch : getChildren()) if (dynamic_cast<juce::Button*> (ch) != nullptr) ch->setWantsKeyboardFocus (false);
    keysStayWithWindow (*this);
    setSize (1320, 820);
    startTimerHz (30);
    m.fillPieces();
    m.selectTab (st != nullptr ? st->getIntValue ("specEditTab", 0) : 0);
    m.setPiece (editId);
}

SpecEditComponent::~SpecEditComponent()
{
    app.project.removeChangeListener (this);
    stopTimer();
    impl->stopPlay();
    ++*impl->breakdownGen;
    if (impl->detailCancel != nullptr) impl->detailCancel->store (true);
    impl->pictureBuild.reset();
    impl->analysis.reset();
    impl->renderJob.reset();
    impl->hold.reset();
    impl->job.release();
}

void SpecEditComponent::setPiece (const juce::Uuid& editId) { impl->setPiece (editId); }
void SpecEditComponent::checkStep (const juce::String& what) { if (what == "pick") impl->pickPeak (0); }
void SpecEditComponent::timerCallback() { impl->tick(); }
void SpecEditComponent::changeListenerCallback (juce::ChangeBroadcaster*) { impl->markDirty(); }

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
    const int sideW = juce::jlimit (330, 390, getWidth() * 28 / 100);
    {   // the right-hand side: three pages
        auto right = r.removeFromRight (sideW); r.removeFromRight (8);
        {
            auto tabs = right.removeFromTop (24); right.removeFromTop (6);
            const int w = tabs.getWidth();
            m.tabBtn[0].setBounds (tabs.removeFromLeft (w * 20 / 100)); m.tabBtn[3].setBounds (tabs.removeFromRight (w * 20 / 100));
            m.tabBtn[2].setBounds (tabs.removeFromRight (w * 24 / 100)); m.tabBtn[1].setBounds (tabs);
        }
        if (m.tab == Impl::kTabFigures)
        {
            auto opts = right.removeFromBottom (60); right.removeFromBottom (6);
            m.stats.setBounds (right);
            auto a = opts.removeFromTop (27), b = opts.removeFromTop (27);
            m.rmsCaption.setBounds (a.removeFromLeft (84)); a.removeFromLeft (6);
            m.rmsBox.setBounds (a.removeFromLeft (84).withSizeKeepingCentre (84, grid::btnH)); a.removeFromLeft (8);
            m.copyBtn.setBounds (a.withSizeKeepingCentre (a.getWidth(), grid::btnH));
            m.refCaption.setBounds (b.removeFromLeft (84)); b.removeFromLeft (6);
            m.refBox.setBounds (b.withSizeKeepingCentre (b.getWidth(), grid::btnH));
        }
        else if (m.tab == Impl::kTabPeaks)
        {
            {
                auto row = right.removeFromTop (grid::btnH); right.removeFromTop (4);
                m.countCaption.setBounds (row.removeFromLeft (88)); row.removeFromLeft (6);
                m.countBox.setBounds (row.removeFromLeft (64));
                m.boxBtn.setBounds (row.removeFromRight (76));
            }
            const int fixed = 32 + 4 + grid::btnH + 4 + grid::btnH + 8 + 18 + 2 + 4 + 22;
            const int flex = juce::jmax (0, right.getHeight() - fixed - 4);
            const int listH = juce::jlimit (juce::jmin (64, flex / 2), juce::jmax (64, flex * 55 / 100), 20 * juce::jmax (3, (int) m.peaks.rows.size()) + 2);
            m.peaks.setBounds (right.removeFromTop (listH)); right.removeFromTop (4);
            m.peaksNote.setBounds (right.removeFromTop (32));
            {
                auto row = right.removeFromTop (grid::btnH); right.removeFromTop (4);
                m.gainCaption.setBounds (row.removeFromLeft (42)); row.removeFromLeft (6);
                m.gainSlider.setBounds (row);
            }
            {
                auto row = right.removeFromTop (grid::btnH); right.removeFromTop (8);
                m.reduceBtn.setBounds (row.removeFromLeft (juce::jmin (130, row.getWidth() / 2))); row.removeFromLeft (6);
                m.undoBtn.setBounds (row.removeFromLeft (juce::jmin (96, row.getWidth())));
            }
            m.tracksCaption.setBounds (right.removeFromTop (18)); right.removeFromTop (2);
            {
                auto row = right.removeFromBottom (22); right.removeFromBottom (4);
                m.allBtn.setBounds (row.removeFromLeft (60)); row.removeFromLeft (6); m.noneBtn.setBounds (row.removeFromLeft (60));
            }
            m.gainTracks.setBounds (right);
        }
        else if (m.tab == Impl::kTabChanges)
        {
            m.changesNote.setBounds (right.removeFromTop (34)); right.removeFromTop (2);
            auto row = right.removeFromBottom (grid::btnH); right.removeFromBottom (6);
            m.againBtn.setBounds (row.removeFromLeft (juce::jmin (120, row.getWidth() / 2))); row.removeFromLeft (6);
            m.takeOffBtn.setBounds (row.removeFromLeft (juce::jmin (110, row.getWidth())));
            m.changes.setBounds (right);
        }
        else
        {
            m.plugNote.setBounds (right.removeFromTop (48)); right.removeFromTop (2);
            {
                auto row = right.removeFromTop (grid::btnH); right.removeFromTop (4);
                m.chooseBtn.setBounds (row.removeFromLeft (132)); row.removeFromLeft (8);
                m.plugName.setBounds (row);
            }
            {
                auto row = right.removeFromBottom (grid::btnH); right.removeFromBottom (4);
                m.openBtn.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
                m.removeBtn.setBounds (row.removeFromLeft (80)); row.removeFromLeft (10);
                m.bypassToggle.setBounds (row.removeFromLeft (juce::jmin (84, row.getWidth())));
            }
            {
                auto row = right.removeFromBottom (grid::btnH); right.removeFromBottom (4);
                m.linkToggle.setBounds (row.removeFromLeft (juce::jmin (150, row.getWidth())));
                m.plugDelay.setBounds (row);
            }
            {
                auto row = right.removeFromBottom (22); right.removeFromBottom (4);
                m.plugAllBtn.setBounds (row.removeFromLeft (60)); row.removeFromLeft (6); m.plugNoneBtn.setBounds (row.removeFromLeft (60));
            }
            m.plugTracks.setBounds (right);
        }
    }
    // the left: the waveform, the tools, the picture and the meters
    if (m.meters.isVisible()) { const int metersH = juce::jlimit (130, 230, r.getHeight() * 27 / 100); m.meters.setBounds (r.removeFromBottom (metersH)); r.removeFromBottom (6); }
    const int waveH = juce::jlimit (92, 170, r.getHeight() * 26 / 100);
    const auto waveR = r.removeFromTop (waveH); r.removeFromTop (4);
    m.wave.setBounds (waveR);
    m.fitBtn.setBounds (waveR.getRight() - 104, waveR.getY() + 22, 98, 20);
    m.fitBtn.toFront (false);
    {
        auto row = r.removeFromTop (grid::btnH); r.removeFromTop (4);
        m.metersBtn.setBounds (row.removeFromRight (66)); row.removeFromRight (6);
        m.specBtn.setBounds (row.removeFromRight (76)); row.removeFromRight (10);
        m.toolsBar.setBounds (row.removeFromLeft (juce::jmin ((int) fixtools::SelectToolsBar::kWidth, row.getWidth())));
    }
    m.view.setBounds (r);
}

bool SpecEditComponent::keyPressed (const juce::KeyPress& k)
{
    auto& m = *impl;
    if (k == juce::KeyPress::spaceKey) { if (m.playing) m.stopPlay(); else m.startPlay(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (m.view.hasSelection()) { m.view.clearSelection(); return true; }
        if (m.wave.hasSelection()) { m.wave.clearSelection(); return true; }
        return false;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::ctrlModifier, 0)) { m.selFromView = false; m.wave.selectAll(); return true; }
    if (k == juce::KeyPress::homeKey) { m.setPlayheadAll (0.0); m.wave.follow (0.0); if (m.pyramid != nullptr) m.view.showTime (0.0); if (m.playing) m.startPlay(); else m.showPlayhead(); return true; }
    if (m.pyramid != nullptr)
    {
        if (! k.getModifiers().isAnyModifierKeyDown() && (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G')) { m.reduce(); return true; }
        if (m.toolsBar.key (k)) return true;                                     // S, T, B, [ and ]
        if (! k.getModifiers().isAnyModifierKeyDown() && m.view.zoomKey (k)) return true;      // the arrows
    }
    return false;
}
} // namespace td
