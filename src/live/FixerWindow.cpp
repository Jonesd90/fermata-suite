#include "FixerWindow.h"
#include "LiveTakeView.h"
#include "NotePanel.h"
#include "StationNotes.h"
#include "SecondSeat.h"
#include "LiveDesk.h"
#include "LiveLook.h"
#include "../core/AudioOps.h"
#include "../ui/SpectralWindow.h"                 // fermata's ReTouch window (shared: COMPATIBILITY.md)
#include <mutex>
#include <set>

namespace td::live
{
static juce::var obj() { return juce::var (new juce::DynamicObject()); }
static void put (juce::var& o, const juce::Identifier& k, const juce::var& v) { o.getDynamicObject()->setProperty (k, v); }
static juce::String hmsF (double seconds, bool ms = true)
{
    if (seconds < 0.0) seconds = 0.0;
    const auto total = (juce::int64) std::floor (seconds);
    juce::String t = juce::String ((int) (total / 3600)).paddedLeft ('0', 2) + ":" + juce::String ((int) ((total / 60) % 60)).paddedLeft ('0', 2) + ":" + juce::String ((int) (total % 60)).paddedLeft ('0', 2);
    if (ms) t << "." << juce::String ((int) std::floor ((seconds - (double) total) * 1000.0)).paddedLeft ('0', 3);
    return t;
}

/** A black display with a caption over a time (as the Primary's take window). */
class FixerTimeBox final : public juce::Component
{
public:
    explicit FixerTimeBox (juce::String cap, float size) : caption (std::move (cap)), fontSize (size) {}
    void set (const juce::String& t, juce::Colour c = juce::Colours::white) { if (t != text || c != colour) { text = t; colour = c; repaint(); } }
    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0e13)); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::border); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (caption, getLocalBounds().reduced (8, 3).removeFromTop (12), juce::Justification::centredLeft, false);
        g.setColour (colour); g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), fontSize, juce::Font::bold)));
        g.drawFittedText (text, getLocalBounds().reduced (8, 2).withTrimmedTop (12), juce::Justification::centred, 1, 0.6f);
    }
private:
    juce::String caption, text; juce::Colour colour { juce::Colours::white }; float fontSize;
};

/** What one ReTouch try needs, gathered on the message thread; the work reads the audio itself (its own readers) on the job's thread. */
struct FixTry
{
    struct Track { juce::Uuid id; juce::String name; int channels = 1; int sessionIndex = -1; juce::File file; };
    int takeNumber = 0; juce::int64 from = 0, to = 0, takeLength = 0, sessionStart = 0; double rate = 48000.0;
    bool session = false; juce::File sessionFolder; juce::int64 writtenTo = 0;
    std::vector<Track> tracks;
    FixSpec spec; juce::String what;
    // the result
    std::map<juce::Uuid, std::vector<std::vector<float>>> fixed;
};

/** The session's chunks, ready to read (a take of the session); nothing for a take of the project. */
static void openTimeline (const FixTry& t, ChunkTimeline& timeline)
{
    if (t.session) { timeline.setSession (t.sessionFolder); timeline.setWrittenTo (t.writtenTo); }
}
/** 0.8.0 (asked for on 10 November 2026): why a part is too long to fix in one go ("" = it is not). Try it keeps every chosen track's fixed audio
    in memory - to be heard at once, and written as a fix version - and works on a track at a time in memory too; the ReTouch window holds the
    part as its picture as well ('window'). P round a whole long take on many tracks ran out of memory, and all the job could say was "Something
    went wrong while processing". The sum is a rough one, on the safe side: half the PC's memory is the most a part may ask for. */
static juce::String whyTooLong (const FixTry& t, bool window)
{
    juce::int64 chans = 0; int widest = 1;
    for (auto& tr : t.tracks) { chans += juce::jmax (1, tr.channels); widest = juce::jmax (widest, tr.channels); }
    const double rate = juce::jmax (1.0, t.rate), len = (double) juce::jmax ((juce::int64) 0, t.to - t.from);
    // for every sample of the part: 4 bytes for each channel kept, and the room to work on the widest track (its audio, its fix, the working copies)
    const double perSample = 4.0 * ((double) chans + 6.0 * (double) widest) * (window ? 2.0 : 1.0);
    const int ramMb = juce::SystemStats::getMemorySizeInMegabytes();
    const double room = (ramMb > 0 ? (double) ramMb * 0.5 : 4096.0) * 1024.0 * 1024.0;
    const double most = juce::jmin (room / perSample, 2.0e9);                                   // (samples; the part is counted in an int further on)
    if (len <= most) return {};
    auto span = [] (double seconds) { return seconds >= 90.0 ? juce::String ((int) (seconds / 60.0)) + " minutes" : juce::String (juce::jmax (1, (int) seconds)) + " seconds"; };
    return "That part is too long to fix in one go: " + span (len / rate) + " on " + juce::String ((int) t.tracks.size()) + (t.tracks.size() == 1 ? " track" : " tracks")
         + " needs about " + juce::String (len * perSample / 1.0e9, 1) + " GB of memory"
         + (ramMb > 0 ? ", and this PC has " + juce::String (juce::roundToInt ((double) ramMb / 1024.0)) + " GB" : juce::String())
         + ". Choose a shorter part (about " + span (most / rate) + " at most on these tracks), or fewer tracks. Nothing was changed.";
}
/** One track's take samples [at, at + n), every channel: from the session's chunks for a take of the session, else from the take's file. False when
    it could not be read (then 'buf' holds silence). */
static bool readTakeSpan (const FixTry& t, const FixTry::Track& tr, ChunkTimeline& timeline, juce::int64 at, int n, std::vector<std::vector<float>>& buf)
{
    buf.assign ((size_t) juce::jmax (1, tr.channels), std::vector<float> ((size_t) juce::jmax (0, n), 0.0f));
    if (n <= 0) return true;
    std::vector<float*> dest; for (auto& b : buf) dest.push_back (b.data());
    bool got = false;
    if (t.session && tr.sessionIndex >= 0 && timeline.valid()) got = timeline.read (tr.sessionIndex, t.sessionStart + at, n, dest.data());
    if (! got && tr.file.existsAsFile())
    {
        RawWavReader r;
        if (r.open (tr.file)) got = r.read (at, n, dest.data(), tr.channels) > 0;
    }
    return got;
}

static juce::String runFix (FixTry& t, AudioJob& job)
{
    const juce::int64 ctx = (juce::int64) std::llround ((t.spec.kind == FixSpec::Kind::Dehum ? 2.0 : 1.0) * t.rate);     // (a de-hum's filters settle over two seconds)
    const juce::int64 c0 = juce::jmax ((juce::int64) 0, t.from - ctx), c1 = juce::jmin (t.takeLength > 0 ? t.takeLength : t.to + ctx, t.to + ctx);
    if (c1 <= c0 || t.to <= t.from) return "Nothing to fix there.";
    ChunkTimeline timeline;
    openTimeline (t, timeline);
    const int n = (int) (c1 - c0);
    if (t.spec.kind == FixSpec::Kind::Dehum)                          // 0.7.0: the hums are found first, in the part itself (every track's mono mix), and the ticked ones taken out
    {
        dehum::Source src; src.channels = (int) t.tracks.size(); src.length = t.to - t.from;
        src.read = [&t, &timeline] (int ch, juce::int64 at, int cnt, float* dst)
        {
            std::vector<std::vector<float>> buf;
            if (ch < 0 || ch >= (int) t.tracks.size() || ! readTakeSpan (t, t.tracks[(size_t) ch], timeline, t.from + at, cnt, buf) || buf.empty()) return false;
            const float inv = 1.0f / (float) buf.size();
            for (int i = 0; i < cnt; ++i) { float v = 0.0f; for (auto& c : buf) v += i < (int) c.size() ? c[(size_t) i] : 0.0f; dst[i] = v * inv; }
            return true;
        };
        auto found = dehum::find (src, t.rate, t.spec.f0, t.spec.f1, [&job] { return job.cancelled(); });
        if (job.cancelled()) return "Cancelled.";
        t.spec.hum.hums = found.hums;
        if (! t.spec.hum.notches().empty())
        {
            juce::String names; int k = 0;
            for (auto& h : t.spec.hum.hums) if (h.on) names << (k++ > 0 ? ", " : "") << h.text() << " (" << (int) std::lround (h.aboveDb) << " dB above the room)";
            t.what << ": " << names;
        }
        else return found.note.isNotEmpty() ? found.note : juce::String ("No steady hum stands 15 dB above the room in that part. Open the ReTouch window to see the fainter ones and tick them yourself.");
    }
    int done = 0;
    for (auto& tr : t.tracks)
    {
        if (job.cancelled()) return "Cancelled.";
        std::vector<std::vector<float>> buf;
        if (! readTakeSpan (t, tr, timeline, c0, n, buf)) return "The audio of " + tr.name + " could not be read there (is it on the disk yet?).";
        auto spec = t.spec;
        if (spec.kind == FixSpec::Kind::Denoise)                     // the room tone learned from the quietest moments around the box, per channel
        {
            auto profiles = std::make_shared<std::vector<denoise::Profile>>();
            auto p = spec.dn; p.quietest = true;
            for (auto& ch : buf) profiles->push_back (denoise::learnProfile ({ ch }, t.rate, p));
            spec.dnProfiles = profiles;
        }
        audioops::applyFix (buf, t.rate, (long) (t.from - c0), (long) (t.to - c0), spec, (long) std::lround (0.004 * t.rate));
        std::vector<std::vector<float>> span;
        for (auto& ch : buf) span.emplace_back (ch.begin() + (std::ptrdiff_t) (t.from - c0), ch.begin() + (std::ptrdiff_t) (t.to - c0));
        t.fixed[tr.id] = std::move (span);
        job.setProgress ((float) ++done / (float) juce::jmax ((size_t) 1, t.tracks.size()));
    }
    return {};
}

/** 0.4.0, fermata's ReTouch window in the Fixer: every change made in the window (its ops, in samples from the start of the window's area 't.from'),
    put on each track's audio of the take one after the other - as fermata makes its new files (AudioOps' fixWholeFile): the take's own samples round
    the changes are read (8 s of context for a ReTouch or a denoise, 1 s for the rest), a denoise gets its room tone (learned from its room-tone box,
    from the take itself, or the project's for the track), a pasted consonant its track's part. 'out' holds what changed: each track's channels from
    the first changed sample to the last. Job thread. */
static juce::String renderOps (const FixTry& t, const std::vector<fixtools::RepairOp>& ops, const std::shared_ptr<const Project::RoomTone>& room, AudioJob& job, Player::Audition& out)
{
    if (ops.empty()) return "Nothing to do.";
    const double sr = t.rate;
    const juce::int64 len = t.takeLength;
    std::vector<audioops::FixOp> fo;
    juce::int64 lo = len, hi = 0, margin = 0;
    for (auto& o : ops)
    {
        audioops::FixOp op { o.spec, t.from + o.t0, t.from + o.t1 };
        if (op.spec.kind == FixSpec::Kind::Paste)                                  // a pasted consonant keeps its own start
        {
            const auto a = juce::jlimit ((juce::int64) 0, len, op.s0), b = juce::jlimit ((juce::int64) 0, len, op.s1);
            if (b <= a) continue;
            lo = juce::jmin (lo, a); hi = juce::jmax (hi, b);
            fo.push_back (op);
            continue;
        }
        op.s0 = juce::jlimit ((juce::int64) 0, len, op.s0); op.s1 = juce::jlimit ((juce::int64) 0, len, op.s1);
        if (op.s1 <= op.s0) continue;
        const auto reach = audioops::findsLost (op.spec, sr, op.s0, op.s1) ? audioops::lostReach (sr) : 0;       // (putting lost samples back reaches past the box)
        lo = juce::jmin (lo, juce::jmax ((juce::int64) 0, op.s0 - reach)); hi = juce::jmax (hi, juce::jmin (len, op.s1 + reach));
        margin = juce::jmax (margin, (juce::int64) ((op.spec.kind == FixSpec::Kind::Patch || op.spec.kind == FixSpec::Kind::Denoise || op.spec.kind == FixSpec::Kind::Dehum ? 8.0 : 1.0) * sr));
        fo.push_back (op);
    }
    if (fo.empty() || hi <= lo) return "Nothing to do.";
    const juce::int64 r0 = juce::jmax ((juce::int64) 0, lo - margin), r1 = juce::jmin (len, hi + margin);
    ChunkTimeline timeline;
    openTimeline (t, timeline);
    out = {}; out.takeNumber = t.takeNumber; out.from = lo;
    int done = 0;
    for (auto& tr : t.tracks)
    {
        if (job.cancelled()) return "Cancelled.";
        std::vector<std::vector<float>> seg;
        if (! readTakeSpan (t, tr, timeline, r0, (int) (r1 - r0), seg)) return "The audio of " + tr.name + " could not be read (is it on the disk yet?).";
        std::map<juce::String, std::shared_ptr<const std::vector<denoise::Profile>>> boxes;     // a denoise's room tone from its box: per channel of this track
        for (auto op : fo)
        {
            if (op.spec.kind == FixSpec::Kind::Denoise)
            {
                op.spec.dnProfiles = nullptr;
                if (op.spec.dnBoxT1 > op.spec.dnBoxT0)
                {
                    const auto key = op.spec.roomKey();
                    if (boxes.count (key) == 0)
                    {
                        std::vector<std::vector<std::vector<float>>> regions ((size_t) juce::jmax (1, tr.channels));      // [channel][stretch of the box]
                        juce::int64 total = 0;
                        for (auto& sp : op.spec.roomSpans())
                        {
                            const juce::int64 a = juce::jmax ((juce::int64) 0, t.from + (juce::int64) std::llround (sp.first * sr));
                            const juce::int64 b = juce::jmin (juce::jmin (len, t.from + (juce::int64) std::llround (sp.second * sr)), a + (juce::int64) (60.0 * sr));   // (a minute says everything)
                            if (b <= a) continue;
                            std::vector<std::vector<float>> ch;
                            if (! readTakeSpan (t, tr, timeline, a, (int) (b - a), ch)) return "The room tone of " + tr.name + " could not be read.";
                            for (size_t c = 0; c < ch.size() && c < regions.size(); ++c) regions[c].push_back (std::move (ch[c]));
                            total += b - a;
                        }
                        if (total < (juce::int64) (0.5 * sr)) return "The marked room tone is too short (mark at least half a second, better two or more).";
                        auto prof = std::make_shared<std::vector<denoise::Profile>>();
                        for (auto& r : regions)
                        {
                            prof->push_back (denoise::learnProfile (r, sr, op.spec.dn));
                            if (! prof->back().valid()) return "The marked room tone is too short for the analysis window.";
                        }
                        boxes[key] = prof;
                    }
                    op.spec.dnProfiles = boxes[key];
                }
                else if (room != nullptr)
                {
                    if (const auto* rt = room->forTrack (tr.id); rt != nullptr && ! rt->perChannel.empty())
                        op.spec.dnProfiles = std::make_shared<const std::vector<denoise::Profile>> (rt->perChannel);
                }
            }
            if (op.spec.kind == FixSpec::Kind::Paste) audioops::resolvePaste (op.spec, tr.id);
            audioops::applyFix (seg, sr, (long) (op.s0 - r0), (long) (op.s1 - r0), op.spec, (long) (0.02 * sr));
        }
        std::vector<std::vector<float>> span;
        for (auto& ch : seg) span.emplace_back (ch.begin() + (std::ptrdiff_t) (lo - r0), ch.begin() + (std::ptrdiff_t) (hi - r0));
        out.tracks[tr.id] = std::move (span);
        job.setProgress ((float) ++done / (float) juce::jmax ((size_t) 1, t.tracks.size()));
    }
    return {};
}

/** What a version made in the ReTouch window holds, for the versions list and fermata's review ("ReTouch window: ReTouch x3, De-click"). */
static juce::String describeOps (const std::vector<fixtools::RepairOp>& ops)
{
    std::map<juce::String, int> count; std::vector<juce::String> order;
    for (auto& o : ops)
    {
        juce::String k;
        switch (o.spec.kind)
        {
            case FixSpec::Kind::Declick: k = "De-click"; break;
            case FixSpec::Kind::Denoise: k = "Denoise"; break;
            case FixSpec::Kind::Paste:   k = "consonant pasted"; break;
            case FixSpec::Kind::Dehum:   k = "De-hum"; break;                                    // (0.7.0: fermata 3.44.0's De-Hummer)
            case FixSpec::Kind::Patch:   k = o.spec.repair.mode == RepairMode::Gain ? "Gain" : "ReTouch"; break;
            default:                     k = "fix"; break;
        }
        if (count[k]++ == 0) order.push_back (k);
    }
    juce::String s ("ReTouch window: ");
    for (size_t i = 0; i < order.size(); ++i) s << (i > 0 ? ", " : "") << order[i] << (count[order[i]] > 1 ? " x" + juce::String (count[order[i]]) : juce::String());
    return s;
}

/** fermata's ReTouch window in a window of the Fixer's own: modal, as in fermata (the Fixer's window behind it keeps showing the flags as they come);
    closing it with fixes not kept asks first. Its size and place are remembered. */
class ReTouchWindow final : public juce::DialogWindow
{
public:
    ReTouchWindow (const juce::String& title, std::unique_ptr<fixtools::RepairDialog> content, juce::PropertiesFile* s, juce::Component* around)
        : juce::DialogWindow (title, theme::window, true, true), settings (s)
    {
        dlg = content.get();
        setContentOwned (content.release(), true);
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (760, 520, 8000, 6000);
        centreAroundComponent (around, getWidth(), getHeight());
        if (settings != nullptr)
        {
            auto saved = juce::Rectangle<int>::fromString (settings->getValue ("retouchWindow"));
            const auto& displays = juce::Desktop::getInstance().getDisplays();
            if (saved.getWidth() >= 760 && saved.getHeight() >= 520)
                if (const auto* d = displays.getDisplayForRect (saved); d != nullptr && d->userArea.contains (saved.getCentre()))
                    setBounds (saved.getIntersection (d->userArea));
        }
    }
    ~ReTouchWindow() override { if (settings != nullptr) settings->setValue ("retouchWindow", getBounds().toString()); }
    void closeButtonPressed() override { if (dlg == nullptr || dlg->mayClose()) setVisible (false); }   // (not visible = out of its modal state, and deleted)
    bool escapeKeyPressed() override { closeButtonPressed(); return true; }
private:
    juce::Component::SafePointer<fixtools::RepairDialog> dlg;
    juce::PropertiesFile* settings = nullptr;
};

/** The flags with their notes and replies, newest first - the session's, with a reply box for the one chosen, and those the project already has
    from earlier sessions (its take window's flag markers: shown, and gone to, but not replied to - a reply goes through the session). */
class FixerFlags final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FixerFlags (FixerApp& a) : app (a)
    {
        caption.setText ("THE PRIMARY'S FLAGS", juce::dontSendNotification); caption.setFont (juce::FontOptions (11.0f, juce::Font::bold)); caption.setColour (juce::Label::textColourId, theme::dimText);
        list.setModel (this); list.setRowHeight (46); list.setColour (juce::ListBox::backgroundColourId, theme::field);
        detail.setMultiLine (true); detail.setReadOnly (true); detail.setCaretVisible (false); detail.setFont (juce::FontOptions (12.5f));
        replyBox.setTextToShowWhenEmpty ("Type a reply for the Primary, then Enter", theme::dimText.withAlpha (0.6f));
        replyBox.onReturnKey = [this] { sendReply(); };
        send.onClick = [this] { sendReply(); };
        goThere.onClick = [this] { if (onGo != nullptr) if (auto* x = find (chosen)) onGo (chosen, x->ws); };
        goThere.setTooltip ("To the flag's place (Follow is unticked: the playhead is yours)");
        for (auto* c : std::initializer_list<juce::Component*> { &caption, &list, &detail, &replyBox, &send, &goThere }) addAndMakeVisible (c);
        send.setWantsKeyboardFocus (false); goThere.setWantsKeyboardFocus (false);
    }
    std::function<void (const juce::String& flagId, double windowSeconds)> onGo;      // (seconds < 0: not on this window's timeline)
    std::function<void (const juce::String& flagId)> onChoose;
    juce::String chosen;
    void choose (const juce::String& id)
    {
        chosen = id;
        for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i].f.id == id) list.selectRow (i, false, true);
        showDetail();
        if (onChoose) onChoose (id);
    }
    juce::Component* focusReply() { replyBox.grabKeyboardFocus(); return &replyBox; }
    void refresh()
    {
        std::vector<Shown> now;
        const auto L = app.layout();
        std::set<juce::String> have;
        for (auto& f : app.model.flags)
        {
            if (f.isNote) continue;                                         // (0.5.0: notes are their station's own)
            have.insert (f.id);
            if (! f.removed) now.push_back ({ f, L != nullptr ? L->windowSecondsOf (f.sample) : -1.0, false });
        }
        if (auto* w = app.pieceWindow())
            for (auto& m : w->markers)
                if (m.flag && have.count (m.id.toString()) == 0)
                {
                    Shown x; x.f.id = m.id.toString(); x.f.note = m.note; x.f.pinnedAt = m.pinnedAt; x.f.by = m.by.isNotEmpty() ? m.by : juce::String ("Primary");
                    x.ws = L != nullptr ? L->shownOfWindow (m.seconds) : m.seconds; x.fromProject = true;
                    for (auto& r : m.replies) x.f.replies.push_back ({ r.text, r.by, r.at });
                    now.push_back (x);
                }
        std::sort (now.begin(), now.end(), [] (const Shown& x, const Shown& y) { return x.f.pinnedAt > y.f.pinnedAt; });
        bool same = now.size() == shown.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].f.id == shown[i].f.id && now[i].f.note == shown[i].f.note && now[i].f.replies.size() == shown[i].f.replies.size() && std::abs (now[i].ws - shown[i].ws) < 1.0e-6;
        for (auto& x : now) if (! x.fromProject && x.f.by.isNotEmpty()) primaryName = x.f.by;
        caption.setText (primaryName.isNotEmpty() ? "FLAGS FROM " + primaryName.toUpperCase() : juce::String ("THE PRIMARY'S FLAGS"), juce::dontSendNotification);
        if (same) return;
        shown = now; list.updateContent(); list.repaint();
        for (int i = 0; i < (int) shown.size(); ++i) if (shown[(size_t) i].f.id == chosen) list.selectRow (i, false, true);
        showDetail();
    }
    /** The chosen flag is the session's (so a reply, or a fix version made for it, reaches the Primary). */
    bool chosenIsLive() const { auto* x = find (chosen); return x != nullptr && ! x->fromProject; }
    void resized() override
    {
        auto r = getLocalBounds();
        caption.setBounds (r.removeFromTop (18));
        auto bottom = r.removeFromBottom (30);
        send.setBounds (bottom.removeFromRight (60)); bottom.removeFromRight (4);
        replyBox.setBounds (bottom.reduced (0, 2));
        r.removeFromBottom (4);
        auto mid = r.removeFromBottom (110);
        goThere.setBounds (mid.removeFromBottom (26).removeFromLeft (110));
        mid.removeFromBottom (4);
        detail.setBounds (mid);
        r.removeFromBottom (4);
        list.setBounds (r);
    }
private:
    struct Shown { FlagRec f; double ws = -1.0; bool fromProject = false; };
    const Shown* find (const juce::String& id) const { for (auto& x : shown) if (x.f.id == id) return &x; return nullptr; }
    void sendReply()
    {
        if (chosen.isEmpty()) { app.say ("Choose a flag first (click it in the list or on the timeline)"); return; }
        if (! chosenIsLive()) { app.say ("That flag is from an earlier session (it is in the project): a reply goes to a flag of the session that is running"); return; }
        app.reply (chosen, replyBox.getText());
        replyBox.clear();
    }
    void showDetail()
    {
        const auto* x = find (chosen);
        if (x == nullptr) { detail.setText (shown.empty() ? "No flags yet. The Primary pins one with Enter; it shows here at once." : "Click a flag to reply to it."); return; }
        const auto& f = x->f;
        juce::String t = "Pinned " + f.pinnedAt.formatted ("%H:%M:%S") + " by " + f.by + ":\n" + (f.note.isNotEmpty() ? f.note : juce::String ("(no note yet)"));
        for (auto& r : f.replies) t << "\n" << r.by << " (" << r.at.formatted ("%H:%M:%S") << "): " << r.text;
        if (x->fromProject) t << "\n\n(From an earlier session: it is in the project. Replies go to the flags of the session that is running.)";
        detail.setText (t);
    }
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
        const auto& f = shown[(size_t) row].f;
        const double ws = shown[(size_t) row].ws;
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (shown[(size_t) row].fromProject ? juce::Colour (0xffb06a5c) : juce::Colour (0xffff5a3c)); g.fillRect (0, 0, 3, h);
        const double we = app.writtenEndSeconds();
        const bool onDisk = shown[(size_t) row].fromProject || (ws >= 0.0 && (we < 0.0 || ws < we));
        const double rate = app.model.sessionRate > 0.0 ? app.model.sessionRate : 48000.0;
        g.setColour (theme::dimText); g.setFont (juce::FontOptions (10.5f));
        const auto L = app.layout();
        juce::String at = hmsF (ws, false);
        if (L != nullptr && L->timeOfDay && ws >= 0.0) { if (auto* t = L->takeAt (ws)) at = "take " + juce::String (t->number) + " " + hmsF (ws - t->start, false); else at = L->timeText (ws, false); }
        const juce::String where = ws < 0.0 ? "  - between takes (" + hmsF ((double) f.sample / rate, false) + " of the session)"
                                 : "  at " + at + (onDisk ? juce::String() : juce::String ("  - its audio is on its way to the disk"));
        g.drawText (f.pinnedAt.formatted ("%H:%M:%S") + where, 8, 2, w - 10, 13, juce::Justification::centredLeft, true);
        g.setColour (theme::text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (f.note.isNotEmpty() ? f.note : juce::String ("(no note yet)"), 8, 15, w - 10, 15, juce::Justification::centredLeft, true);
        if (! f.replies.empty())
        {
            g.setColour (juce::Colour (0xff7ee08a)); g.setFont (juce::FontOptions (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb3 ")) + f.replies.back().by + ": " + f.replies.back().text, 8, 30, w - 10, 14, juce::Justification::centredLeft, true);
        }
    }
    void listBoxItemClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) shown.size())) choose (shown[(size_t) row].f.id); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { if (juce::isPositiveAndBelow (row, (int) shown.size()) && onGo) onGo (shown[(size_t) row].f.id, shown[(size_t) row].ws); }
    FixerApp& app;
    juce::Label caption;
    juce::ListBox list;
    juce::TextEditor detail, replyBox;
    juce::TextButton send { "Send" }, goThere { "Go to the flag" };
    std::vector<Shown> shown;
    juce::String primaryName;
};

/** 0.6.0: the second seat's panel - the Fixer's own keyboard and mouse (SecondSeat.h): which they are, on and off, the pointer's speed. */
class SeatPanel final : public juce::Component, private juce::Timer
{
public:
    explicit SeatPanel (SecondSeat& s) : seat (s)
    {
        for (auto* l : { &intro, &status, &kbLabel, &mouseLabel, &speedLabel, &hint })
        {
            l->setFont (juce::FontOptions (13.0f)); l->setColour (juce::Label::textColourId, theme::text); l->setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (l);
        }
        intro.setText ("A keyboard and a mouse of the Fixer's own, on this PC. Windows never sees them: the mouse moves the orange pointer on the Fixer's "
                       "screen and works only the Fixer's windows, the keyboard types only into the Fixer. The Primary's keyboard and mouse are not touched.",
                       juce::dontSendNotification);
        hint.setText ("To give them back at once: Esc five times on the Fixer's keyboard, or Let go here (either mouse can click it), or close the Fixer.",
                      juce::dontSendNotification);
        hint.setColour (juce::Label::textColourId, theme::dimText);
        speedLabel.setText ("Pointer speed", juce::dontSendNotification);
        for (auto* b : { &learnKb, &forgetKb, &learnMouse, &forgetMouse, &onOff })
        { addAndMakeVisible (b); b->setWantsKeyboardFocus (false); }
        learnKb.onClick = [this] { seat.learn (SecondSeat::Learn::keyboard); };
        learnMouse.onClick = [this] { seat.learn (SecondSeat::Learn::mouse); };
        forgetKb.onClick = [this] { seat.forget (true, false); };
        forgetMouse.onClick = [this] { seat.forget (false, true); };
        onOff.onClick = [this]
        {
            if (seat.isOn()) { seat.stop (true); return; }
            juce::String why;
            message = seat.start (why) ? juce::String() : why;
            refresh();
        };
        speed.setRange (0.3, 5.0, 0.1); speed.setValue (seat.speed, juce::dontSendNotification);
        speed.setSliderStyle (juce::Slider::LinearHorizontal); speed.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        speed.onValueChange = [this] { seat.setSpeed (speed.getValue()); };
        addAndMakeVisible (speed);
        refresh();
        startTimer (300);
    }
    void refresh()
    {
        juce::String st;
        if (! seat.driverPresent()) st = "Not available: " + seat.driverProblem();
        else if (seat.learning() == SecondSeat::Learn::keyboard) st = "Press any key on the FIXER'S keyboard now (everything still reaches Windows meanwhile)...";
        else if (seat.learning() == SecondSeat::Learn::mouse) st = "Click the left button of the FIXER'S mouse now...";
        else st = seat.isOn() ? "ON: the Fixer's keyboard and mouse work the Fixer only." : "Off: the Fixer's keyboard and mouse are ordinary ones.";
        if (message.isNotEmpty()) st << "\n" << message;
        status.setText (st, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, seat.isOn() ? juce::Colour (0xffffa040) : theme::text);
        kbLabel.setText ("Keyboard:  " + (seat.keyboard().valid() ? seat.keyboard().describe() : seat.keyboard().hardwareId.isNotEmpty() ? juce::String ("chosen, not plugged in") : juce::String ("not chosen")), juce::dontSendNotification);
        mouseLabel.setText ("Mouse:  " + (seat.mouse().valid() ? seat.mouse().describe() : seat.mouse().hardwareId.isNotEmpty() ? juce::String ("chosen, not plugged in") : juce::String ("not chosen")), juce::dontSendNotification);
        const bool can = seat.driverPresent() && ! seat.isOn() && seat.learning() == SecondSeat::Learn::none;
        for (auto* b : { &learnKb, &forgetKb, &learnMouse, &forgetMouse }) b->setEnabled (can);
        onOff.setEnabled (seat.driverPresent());
        onOff.setButtonText (seat.isOn() ? "Let go" : "Start the second seat");
        onOff.setColour (juce::TextButton::buttonColourId, seat.isOn() ? juce::Colour (0xffb04a20) : theme::accent);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        intro.setBounds (r.removeFromTop (54)); r.removeFromTop (6);
        status.setBounds (r.removeFromTop (40)); r.removeFromTop (6);
        auto row = [&r] (juce::Label& l, juce::Button& a, juce::Button& b)
        {
            auto x = r.removeFromTop (28); b.setBounds (x.removeFromRight (80)); x.removeFromRight (6); a.setBounds (x.removeFromRight (80)); x.removeFromRight (8);
            l.setBounds (x); r.removeFromTop (6);
        };
        row (kbLabel, learnKb, forgetKb);
        row (mouseLabel, learnMouse, forgetMouse);
        { auto x = r.removeFromTop (26); speedLabel.setBounds (x.removeFromLeft (110)); speed.setBounds (x); }
        r.removeFromTop (10);
        onOff.setBounds (r.removeFromTop (32).removeFromLeft (220));
        r.removeFromTop (8);
        hint.setBounds (r.removeFromTop (40));
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }
private:
    void timerCallback() override { refresh(); }
    SecondSeat& seat;
    juce::Label intro, status, kbLabel, mouseLabel, speedLabel, hint;
    juce::TextButton learnKb { "Learn" }, forgetKb { "Forget" }, learnMouse { "Learn" }, forgetMouse { "Forget" }, onOff { "Start the second seat" };
    juce::Slider speed;
    juce::String message;
};

/** 0.6.0: the contents of one of the Fixer's own small windows (its mixer, the seat panel), with the keys it has no use for itself (M, N, Space,
    the arrows ...) going on to the Fixer's main window, as if typed there. (A tool window's forwardKeys hands them to the last take window: the
    Fixer has none, so M in the Fixer's mixer did nothing - with either keyboard.) */
class KeysToFixer final : public juce::Component
{
public:
    KeysToFixer (juce::Component* inner, std::function<bool (const juce::KeyPress&)> to) : content (inner), onward (std::move (to))
    {
        addAndMakeVisible (*content);
        setSize (content->getWidth(), content->getHeight());
    }
    void resized() override { content->setBounds (getLocalBounds()); }
    bool keyPressed (const juce::KeyPress& k) override { return content->keyPressed (k) || (onward && onward (k)); }
    juce::Component* inner() const noexcept { return content.get(); }

private:
    std::unique_ptr<juce::Component> content;
    std::function<bool (const juce::KeyPress&)> onward;
};

class FixerComponent final : public juce::Component, private juce::Timer, private juce::ChangeListener, private juce::ListBoxModel
{
public:
    explicit FixerComponent (FixerApp& a) : app (a), flags (a)
    {
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &badge, &pieceLabel, &followBtn, &delayLabel, &delayBox, &backBtn, &playBtn, &fwdBtn, &playhead, &behind, &mixerBtn,
                                                                 &status, &flags, &view, &fixTitle, &selLabel, &fixKind, &amount, &amountLabel, &tryBtn, &abBtn, &saveBtn, &fixNote, &tellPrimary, &retouchBtn,
                                                                 &versions, &progressLabel, &noteBtn, &todBtn, &nameLabel, &nameBox, &seatBtn, &lookBtn })
            addAndMakeVisible (c);
        addChildComponent (notePanel);
        // 0.5.0: notes, the name, the time of day
        noteBtn.setTooltip ("A note [N]: your own, at the playhead (or at the flag point, if you clicked one in the flag band). A box opens at the side to write in; the Primary does not see it. The report lists every note.");
        noteBtn.onClick = [this] { pinNote(); };
        todBtn.setClickingTogglesState (true); todBtn.setToggleState (app.timeOfDay(), juce::dontSendNotification);
        todBtn.setTooltip ("Time of day: the timeline is the clock - every take at the time it was recorded, on past midnight (the date at each midnight). Off: fermata's timeline.");
        todBtn.onClick = [this] { switchTimeOfDay (todBtn.getToggleState()); };
        nameLabel.setText ("Your name:", juce::dontSendNotification); nameLabel.setFont (juce::FontOptions (12.0f)); nameLabel.setColour (juce::Label::textColourId, theme::dimText);
        nameBox.setText (app.settings->getValue ("stationName"), false);
        nameBox.setTextToShowWhenEmpty ("Fixer", theme::dimText.withAlpha (0.6f));
        nameBox.setTooltip ("The name your replies, notes and fix versions carry (\"by ...\"), which the Primary sees. Empty: \"Fixer\".");
        nameBox.onReturnKey = [this] { app.setStationName (nameBox.getText()); grabKeyboardFocus(); if (seat != nullptr && seat->dispatching()) seat->setKeyTarget (this); };
        nameBox.onFocusLost = [this] { app.setStationName (nameBox.getText()); };
        notePanel.onSave = [this] (const juce::String& id, const juce::String& text) { app.setNoteText (id, text); };
        notePanel.onDelete = [this] (const juce::String& id) { app.removeNote (id); };
        notePanel.onClosed = [this] { view.selectedNote.clear(); resized(); grabKeyboardFocus(); if (seat != nullptr && seat->dispatching()) seat->setKeyTarget (this); view.repaint(); };
        for (juce::Button* b : std::initializer_list<juce::Button*> { &noteBtn, &todBtn, &seatBtn, &lookBtn }) b->setWantsKeyboardFocus (false);
        // 0.9.0: fermata's looks, for the Fixer's own windows (the Primary has them in its Project menu; each station keeps its own)
        lookBtn.setTooltip ("The look of the Fixer's windows: the UI style (fermata's desk, or the Manuscript UI) and the desk's colour. The sun / moon in a window's corner switches between light and dark.");
        lookBtn.onClick = [this] { showLookMenu(); };
        // 0.6.0: the second seat - the Fixer's own keyboard and mouse on the Primary's PC (SecondSeat.h)
        seat = std::make_unique<SecondSeat> (*app.settings);
        seat->label = app.stationName();
        seat->home = [this]() -> juce::Component* { return this; };
        seat->area = [this]
        {
            auto* top = getTopLevelComponent();
            const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (top != nullptr ? top->getScreenBounds() : getScreenBounds());
            return d != nullptr ? d->totalArea : juce::Rectangle<int> (0, 0, 1920, 1080);
        };
        seat->say = [this] (const juce::String& t) { app.say (t); };
        seat->onChanged = [this] { if (auto* w = dynamic_cast<KeysToFixer*> (seatWindow != nullptr ? seatWindow->getContentComponent() : nullptr)) if (auto* p = dynamic_cast<SeatPanel*> (w->inner())) p->refresh(); refreshSeatButton(); };
        seatBtn.onClick = [this] { openSeatPanel(); };
        seatBtn.setTooltip ("The second seat: a keyboard and a mouse of the Fixer's own on this PC (the Interception driver). Its mouse moves an orange pointer that works only the Fixer.");
        refreshSeatButton();
        pieceLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold)); pieceLabel.setColour (juce::Label::textColourId, desk::pal().cream);
        followBtn.setToggleState (app.follow, juce::dontSendNotification);
        followBtn.setTooltip ("Follow Primary: the playhead stays a few seconds behind the audio on the disk and cannot be moved. Untick it to move about.");
        followBtn.onClick = [this] { app.setFollow (followBtn.getToggleState()); };
        delayLabel.setText ("seconds behind the disk:", juce::dontSendNotification); delayLabel.setFont (juce::FontOptions (12.0f)); delayLabel.setColour (juce::Label::textColourId, theme::dimText);
        delayBox.setText (juce::String (app.delaySeconds, 1), false); delayBox.setInputRestrictions (4, "0123456789.");
        delayBox.onReturnKey = [this] { app.delaySeconds = juce::jlimit (1.0, 30.0, delayBox.getText().getDoubleValue()); delayBox.setText (juce::String (app.delaySeconds, 1), false); grabKeyboardFocus(); };
        auto tool = [] (IconButton& b, Icon ic, const juce::String& name, const juce::String& tip) { b.setIcon (ic, name); b.setTooltip (tip); b.setWantsKeyboardFocus (false); };
        tool (backBtn, Icon::Rewind, "Back", "Back 5 seconds (Follow unticked)");
        tool (playBtn, Icon::Play, "Play / Stop", "Play / Stop [Space] through the Fixer's own mixer");
        tool (fwdBtn, Icon::Forward, "Forward", "Forward 5 seconds (Follow unticked)");
        backBtn.onClick = [this] { seek (app.playheadSeconds() - 5.0); };
        fwdBtn.onClick = [this] { seek (app.playheadSeconds() + 5.0); };
        playBtn.onClick = [this] { togglePlay(); };
        mixerBtn.onClick = [this] { toggleMixer(); };
        mixerBtn.setTooltip ("The Fixer's own mixer and the outputs it plays to");
        status.setFont (juce::FontOptions (13.0f)); status.setColour (juce::Label::textColourId, desk::pal().cream); status.setMinimumHorizontalScale (0.7f);
        // the timeline
        view.model = &app.model; view.project = &app.project; view.showArm = false;
        view.allTracksByDefault = true;                                    // (0.5.0: a drag chooses every track of the take; Alt + drag only those dragged over)
        view.canSeek = [this] { return ! app.follow; };
        view.onSeek = [this] (double s) { seek (s); };
        view.onLaneClick = [this] (double) { view.flagPoint = -1.0; };
        view.onFlagBandClick = [this] (double s) { view.flagPoint = s; app.say ("Flag point set: N pins a note there (zooming centres on it). A click on the tracks takes it away."); view.repaint(); };
        view.onTakeClick = [this] (int number) { chosenTake = number; if (number != 0) app.say ("Take " + juce::String (number) + " chosen: P makes all of it the part to fix (every track)."); };
        view.onNoteClick = [this] (const juce::String& id) { openNotePanel (id); };
        view.onSelect = [this] (double a, double b, const std::vector<juce::Uuid>& tr) { selA = a; selB = b; selTracks = tr; lastTry.reset(); app.setAudition (nullptr); refreshFixControls(); };
        view.onFlagClick = [this] (const juce::String& id, bool) { flags.choose (id); };
        view.colourOf = [this] (const juce::Uuid& id) { return app.hasProject() ? chan::of (app.project, id) : juce::Colour (0xff3f7fc4); };
        view.isMuteSolo = [this] (const juce::Uuid& id, bool solo) { auto it = app.mix.find (id.toString()); return it != app.mix.end() && (solo ? it->second.solo : it->second.mute); };
        view.toggleMuteSolo = [this] (const juce::Uuid& id, bool solo) { auto& m = app.mix[id.toString()]; if (solo) m.solo = ! m.solo; else m.mute = ! m.mute; app.mixChanged(); };
        flags.onGo = [this] (const juce::String& id, double seconds) { goToFlag (id, seconds); };
        flags.onChoose = [this] (const juce::String& id) { view.selectedFlag = id; view.repaint(); };
        // ReTouch
        fixTitle.setText ("RETOUCH (the fix is a new version: the recording is never changed)", juce::dontSendNotification);
        fixTitle.setFont (juce::FontOptions (11.0f, juce::Font::bold)); fixTitle.setColour (juce::Label::textColourId, theme::dimText);
        selLabel.setFont (juce::FontOptions (12.5f)); selLabel.setColour (juce::Label::textColourId, theme::text);
        fixKind.addItem ("De-click", 1); fixKind.addItem ("Repair: pull down what sticks out (a cough, a pencil, a click)", 2); fixKind.addItem ("Repair: rebuild from the sound around it", 3);
        fixKind.addItem ("Denoise (the room tone of the quietest moments around it)", 4); fixKind.addItem ("Gain", 5);
        fixKind.addItem ("De-hum: find the hums in the part and take them out", 6);          // 0.7.0 (fermata 3.44.0's De-Hummer)
        fixKind.setSelectedId (2, juce::dontSendNotification);
        fixKind.setTooltip ("What Try does to the part. De-hum looks through the part for tones that never change - the mains, a buzz and its harmonics, a whistle - and takes out every one "
                            "that stands 15 dB or more above the room (the slider says how hard: 6 dB off, gently, to 40 dB, all of it); what it found is said when it plays. "
                            "To see the hums, tick and tune them yourself, open the ReTouch window: its De-hum tab (key H).");
        fixKind.onChange = [this] { lastTry.reset(); app.setAudition (nullptr); refreshFixControls(); };
        amount.setSliderStyle (juce::Slider::LinearHorizontal); amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
        amount.onValueChange = [this] { lastTry.reset(); app.setAudition (nullptr); };
        amountLabel.setFont (juce::FontOptions (12.0f)); amountLabel.setColour (juce::Label::textColourId, theme::dimText);
        tryBtn.onClick = [this] { tryFix(); };
        tryBtn.setTooltip ("Works the fix out and plays the part with it (from a second before). Nothing is saved yet.");
        abBtn.setClickingTogglesState (true); abBtn.setToggleState (true, juce::dontSendNotification);
        abBtn.onClick = [this] { app.setAudition (abBtn.getToggleState() ? lastTry : nullptr); abBtn.setButtonText (abBtn.getToggleState() ? "Hearing: FIXED" : "Hearing: ORIGINAL"); };
        abBtn.setTooltip ("Switch between the fixed sound and the original while it plays");
        saveBtn.onClick = [this] { saveVersion(); };
        retouchBtn.onClick = [this] { openReTouchWindow(); };
        retouchBtn.setColour (juce::TextButton::buttonColourId, theme::accent);
        retouchBtn.setTooltip ("fermata's own ReTouch window on the part chosen (and 2 seconds either side), all its tracks as one picture: draw a box round the noise in time and pitch, "
                               "ReTouch, Gain, De-click, Denoise, harmonics, brush and feathering - then keep it as a fix version (the recording is never changed).");
        saveBtn.setTooltip ("Keeps the fix as a new version (its own files; the recording is not changed). fermata's review takes it from there.");
        fixNote.setTextToShowWhenEmpty ("a note for the review (optional)", theme::dimText.withAlpha (0.6f));
        tellPrimary.setToggleState (true, juce::dontSendNotification);
        tellPrimary.setTooltip ("Also tell the Primary (a reply to the chosen flag)");
        versions.setModel (this); versions.setRowHeight (20); versions.setColour (juce::ListBox::backgroundColourId, theme::field);
        progressLabel.setFont (juce::FontOptions (12.0f)); progressLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffd070));
        for (juce::Button* b : std::initializer_list<juce::Button*> { &followBtn, &tellPrimary, &mixerBtn, &tryBtn, &abBtn, &saveBtn, &retouchBtn })
            b->setWantsKeyboardFocus (false);                         // (a button with the keyboard presses itself on Enter or Space)
        refreshFixControls();
        app.addChangeListener (this);
        startTimerHz (30);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FixerComponent> (this)]
        {   // (0.5.0: on the time of day the playhead starts at the first take, not at midnight)
            if (safe == nullptr) return;
            if (const auto L = safe->app.layout(); L != nullptr && L->timeOfDay && safe->app.cursorSeconds < L->firstSeconds()) safe->app.cursorSeconds = L->firstSeconds();
            safe->view.layout = safe->app.layout(); safe->view.playhead = safe->app.cursorSeconds;
            safe->view.zoomToFit();
            if (safe->seat != nullptr) safe->seat->startIfWanted();        // (0.6.0: the second seat, if it was on when the Fixer closed)
        });
    }
    ~FixerComponent() override
    {
        seatWindow.reset();
        seat.reset();                                                     // (first: the Fixer's keyboard and mouse are Windows' again before anything else goes)
        app.removeChangeListener (this);
        notePanel.onClosed = nullptr;
        if (notePanel.isOpen()) notePanel.close();
        if (retouchWindow != nullptr) delete retouchWindow.getComponent();      // (its fixes not kept go with it; it plays through the app, which goes next)
        if (job != nullptr) job->release();
        mixerWindow.reset();
    }
    void flagArrived (const FlagRec& f)
    {
        flags.refresh();
        // a flag just pinned lights the window and is chosen - once (its note and the replies that follow are the same flag), and not while a fix is
        // being tried or worked out: the version kept then, and the reply it sends, belong to the flag that was chosen for it
        const bool fresh = ! f.removed && seenFlags.insert (f.id).second && (juce::Time::getCurrentTime() - f.pinnedAt).inSeconds() < 15.0;
        if (! fresh) return;
        flashUntil = juce::Time::getMillisecondCounter() + 2500;
        const bool fixing = lastTry != nullptr || (job != nullptr && job->isRunning()) || retouchWindow != nullptr;
        if (! fixing) flags.choose (f.id);
        else app.say ("FLAG from the Primary" + (f.note.isNotEmpty() ? ": " + f.note : juce::String()) + " - not chosen, as a fix is being tried (click it in the list to answer it)");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        auto top = r.removeFromTop (64);
        {
            auto left = top.removeFromLeft (260);
            badge.setBounds (left.removeFromTop (28));
            pieceLabel.setBounds (left.removeFromTop (26));
        }
        top.removeFromLeft (10);
        {
            auto col = top.removeFromLeft (250);
            followBtn.setBounds (col.removeFromTop (28));
            auto x = col.removeFromTop (26); delayLabel.setBounds (x.removeFromLeft (160)); delayBox.setBounds (x.removeFromLeft (50).reduced (0, 2));
        }
        top.removeFromLeft (10);
        { auto x = top.removeFromLeft (130).withSizeKeepingCentre (130, 36); backBtn.setBounds (x.removeFromLeft (40)); x.removeFromLeft (4); playBtn.setBounds (x.removeFromLeft (40)); x.removeFromLeft (4); fwdBtn.setBounds (x.removeFromLeft (40)); }
        top.removeFromLeft (10);
        playhead.setBounds (top.removeFromLeft (230).withHeight (58));
        top.removeFromLeft (8);
        behind.setBounds (top.removeFromLeft (170).withHeight (58));
        top.removeFromLeft (10);
        { auto col = top.removeFromLeft (100); mixerBtn.setBounds (col.removeFromTop (28)); col.removeFromTop (4); noteBtn.setBounds (col.removeFromTop (28)); }
        top.removeFromLeft (10);
        {
            auto col = top.removeFromLeft (220);
            { auto x = col.removeFromTop (28); todBtn.setBounds (x.removeFromLeft (104)); x.removeFromLeft (6); seatBtn.setBounds (x); }
            col.removeFromTop (4);
            auto x = col.removeFromTop (28); nameLabel.setBounds (x.removeFromLeft (72)); nameBox.setBounds (x.reduced (0, 2));
        }
        r.removeFromTop (4);
        { auto x = r.removeFromTop (22); lookBtn.setBounds (x.removeFromRight (66)); x.removeFromRight (8); status.setBounds (x); }     // (0.9.0: the Look key at the right end of the status line - the top row is full)
        r.removeFromTop (4);
        auto fix = r.removeFromBottom (150);
        {
            fixTitle.setBounds (fix.removeFromTop (18));
            auto right = fix.removeFromRight (juce::jmin (430, fix.getWidth() / 2));
            versions.setBounds (right.withTrimmedLeft (8));
            selLabel.setBounds (fix.removeFromTop (22));
            { auto x = fix.removeFromTop (26); retouchBtn.setBounds (x.removeFromRight (176).reduced (0, 1)); x.removeFromRight (6); fixKind.setBounds (x.reduced (0, 1)); }
            { auto x = fix.removeFromTop (26); amountLabel.setBounds (x.removeFromLeft (150)); amount.setBounds (x); }
            fix.removeFromTop (4);
            { auto x = fix.removeFromTop (28); tryBtn.setBounds (x.removeFromLeft (110)); x.removeFromLeft (6); abBtn.setBounds (x.removeFromLeft (150)); x.removeFromLeft (6); saveBtn.setBounds (x.removeFromLeft (190)); x.removeFromLeft (6); progressLabel.setBounds (x); }
            fix.removeFromTop (4);
            { auto x = fix.removeFromTop (26); tellPrimary.setBounds (x.removeFromRight (150)); fixNote.setBounds (x.reduced (0, 1)); }
        }
        r.removeFromBottom (6);
        auto right = r.removeFromRight (notePanel.isOpen() ? juce::jmin (420, r.getWidth() / 3) : juce::jmin (320, r.getWidth() / 4)).withTrimmedLeft (6);
        if (notePanel.isOpen()) { notePanel.setBounds (right.removeFromTop (juce::jmax (160, right.getHeight() / 2))); right.removeFromTop (6); }
        flags.setBounds (right);
        view.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        if (juce::Time::getMillisecondCounter() < flashUntil)                  // a new flag: the window's edge lights for a moment
        {
            g.setColour (juce::Colour (0xffff5a3c).withAlpha (0.8f)); g.drawRect (getLocalBounds(), 4);
        }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (delayBox.hasKeyboardFocus (false) || fixNote.hasKeyboardFocus (false) || nameBox.hasKeyboardFocus (false) || notePanel.typing()) return false;
        const auto code = k.getKeyCode();
        const bool plain = ! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown();
        if (code == juce::KeyPress::spaceKey) { togglePlay(); return true; }
        if (code == juce::KeyPress::leftKey)  { view.zoomCentred (1.0 / 1.4, zoomAnchor()); return true; }   // (0.5.0: fermata's arrows - left / right zoom round the
        if (code == juce::KeyPress::rightKey) { view.zoomCentred (1.4, zoomAnchor()); return true; }         //  playhead, put in the middle; up / down every track
        if (code == juce::KeyPress::upKey)    { view.changeLaneHeight (-1); return true; }                  //  smaller / bigger)
        if (code == juce::KeyPress::downKey)  { view.changeLaneHeight (+1); return true; }
        if (k == juce::KeyPress ('.') || k == juce::KeyPress ('+') || k == juce::KeyPress ('=')) { view.vzoom = juce::jmin (16.0f, view.vzoom * 1.4f); view.repaint(); return true; }
        if (k == juce::KeyPress (',') || k == juce::KeyPress ('-')) { view.vzoom = juce::jmax (0.25f, view.vzoom / 1.4f); view.repaint(); return true; }
        if (plain && (code == 'F' || code == 'f')) { followBtn.setToggleState (! followBtn.getToggleState(), juce::sendNotificationSync); return true; }
        if (plain && (code == 'N' || code == 'n')) { pinNote(); return true; }
        if (plain && (code == 'M' || code == 'm')) { toggleMixer(); return true; }
        if (plain && (code == 'P' || code == 'p')) { wholeTake(); return true; }
        if (plain && (code == 'C' || code == 'c')) { view.centreOn (zoomAnchor()); return true; }
        if (code == juce::KeyPress::returnKey) { auto* box = flags.focusReply(); if (seat != nullptr && seat->dispatching()) seat->setKeyTarget (box); return true; }
        return false;
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        flags.refresh();
        versions.updateContent(); versions.repaint();
        if (followBtn.getToggleState() != app.follow) followBtn.setToggleState (app.follow, juce::dontSendNotification);
        refreshNotes();
    }
    /** 0.5.0: the Fixer's own notes (the session's; and those of earlier sessions, in the project - read only here). */
    void refreshNotes()
    {
        const auto L = app.layout();
        auto* w = app.pieceWindow();
        const auto list = notes::collect (app.model, w, L.get(), "Fixer");
        view.notes = notes::forView (list, w, L.get());
        if (notePanel.isOpen())
            if (auto* x = notes::find (list, notePanel.noteId())) { notePanel.setHeading (notes::heading (*x, L.get())); notePanel.refreshText (x->text); }
    }
    void openNotePanel (const juce::String& id, bool fresh = false)
    {
        const auto L = app.layout();
        const auto list = notes::collect (app.model, app.pieceWindow(), L.get(), "Fixer");
        notes::Info x;
        if (auto* f = notes::find (list, id)) x = *f;
        else if (! fresh) return;
        else { x.id = id; x.by = app.stationName(); x.inSession = true; }
        view.selectedNote = id;
        notePanel.open (id, notes::heading (x, L.get()), x.text,
                        x.inSession || fresh ? juce::String() : juce::String ("From an earlier session: it is in the project, which the Fixer does not change (the Primary's fermata:Live or fermata can)."));
        if (seat != nullptr && seat->dispatching()) seat->setKeyTarget (&notePanel.textBox());   // (0.6.0: the seat's keyboard writes the note it opened)
        resized();
        view.repaint();
    }
    // ---- 0.6.0: the second seat's panel
    void openSeatPanel()
    {
        if (seatWindow != nullptr) { seatWindow->toFront (false); return; }
        juce::Component::SafePointer<FixerComponent> safe (this);
        seatWindow = std::make_unique<ToolWindow> ("Second seat - the Fixer's own keyboard and mouse", [safe] (ToolWindow*)
        { if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->seatWindow.reset(); }); });
        seat->label = app.stationName();
        auto* p = new SeatPanel (*seat);
        p->setSize (640, 330);
        seatWindow->setContentOwned (new KeysToFixer (p, [safe] (const juce::KeyPress& k) { return safe != nullptr && safe->keyPressed (k); }), true);
        if (auto* top = getTopLevelComponent()) seatWindow->setTopLeftPosition (top->getX() + 80, top->getY() + 80);
        else seatWindow->centreWithSize (seatWindow->getWidth(), seatWindow->getHeight());
        seatWindow->setVisible (true);
        if (auto* top = getTopLevelComponent()) makeOwnedBy (*seatWindow, *top);
    }
    void showLookMenu()
    {
        juce::PopupMenu m;
        const auto picked = std::make_shared<int> (-1);
        look::addToMenu (m, picked);
        juce::Component::SafePointer<FixerComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lookBtn), [safe, picked] (int r)
        {
            if (safe == nullptr || ! look::chosen (r, picked, *safe->app.settings)) return;
            safe->refreshSeatButton();                                          // (its colour is the look's key colour unless the seat is on)
            safe->resized(); safe->repaint();
        });
    }
    void refreshSeatButton()
    {
        seatBtn.setButtonText (seat != nullptr && seat->isOn() ? "Seat: ON" : "Second seat...");
        seatBtn.setColour (juce::TextButton::buttonColourId, seat != nullptr && seat->isOn() ? juce::Colour (0xffb05a20) : getLookAndFeel().findColour (juce::TextButton::buttonColourId));
    }
    void pinNote()
    {
        const double s = view.flagPoint >= 0.0 ? view.flagPoint : app.playheadSeconds();
        juce::String why;
        const auto id = app.pinNote (s, why);
        if (id.isEmpty()) { app.say (why); return; }
        view.flagPoint = -1.0;
        openNotePanel (id, true);
    }
    double zoomAnchor() const { return view.flagPoint >= 0.0 ? view.flagPoint : app.playheadSeconds(); }
    void toggleMixer()
    {
        if (mixerWindow != nullptr && mixerWindow->isVisible())
        {
            juce::Component::SafePointer<FixerComponent> safe (this);         // (later: the key may have come from the mixer window itself)
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->mixerWindow.reset(); });
            return;
        }
        openMixer();
    }
    /** P: the take chosen by its header becomes the part to fix, all of it, on every track recorded in it. */
    void wholeTake()
    {
        const auto L = app.layout();
        const ShownTake* t = L != nullptr && chosenTake != 0 ? L->findTake (chosenTake) : nullptr;
        if (t == nullptr) { app.say ("P chooses a whole take to fix: click its header (the strip with its number) first."); return; }
        const double we = app.writtenEndSeconds();
        selA = t->start; selB = t->recording && we >= 0.0 ? juce::jmax (t->start, we) : t->shownEnd();
        selTracks = view.tracksIn (*t);
        view.selA = selA; view.selB = selB; view.selTracks = selTracks;
        lastTry.reset(); app.setAudition (nullptr);
        refreshFixControls();
        view.repaint();
    }
    void switchTimeOfDay (bool on)
    {
        if (app.isPlaying()) app.stop();
        const auto old = app.layout();
        const double ws = old != nullptr ? old->windowOfShown (app.cursorSeconds) : app.cursorSeconds;
        app.setTimeOfDay (on);
        const auto L = app.layout();
        if (L != nullptr) app.cursorSeconds = ws >= 0.0 ? L->shownOfWindow (ws) : L->firstSeconds();
        view.flagPoint = -1.0; selA = selB = -1.0; selTracks.clear(); view.selA = view.selB = -1.0; view.selTracks.clear();
        lastTry.reset(); app.setAudition (nullptr); refreshFixControls();
        view.layout = L;
        view.playhead = app.cursorSeconds;
        refreshNotes();
        timerCallback();                                                 // (the live end, now and the playhead on the new timeline before fitting)
        view.zoomToFit();
    }
    void timerCallback() override
    {
        const auto L = app.layout();
        view.layout = L;
        if (auto* w = app.pieceWindow()) { view.projectFlags.clear(); for (auto& m : w->markers) if (m.flag) view.projectFlags.push_back (m); }
        pieceLabel.setText (app.hasProject() ? app.project.name + "  -  " + app.pieceName() : juce::String ("No project yet"), juce::dontSendNotification);
        const double we = app.writtenEndSeconds(), live = app.liveEndSeconds();
        view.writtenEnd = we; view.liveEnd = live;
        view.playhead = app.playheadSeconds();
        const bool tod = L != nullptr && L->timeOfDay;
        {   // (0.5.0: the first layout - the project read -, or a new day on the clock: the view starts on the takes)
            const juce::int64 key = L == nullptr ? -1 : (tod ? L->originMs : 1);
            if (key != shownLayoutKey)
            {
                shownLayoutKey = key;
                if (L != nullptr)
                {
                    if (tod && app.cursorSeconds < L->firstSeconds()) app.cursorSeconds = L->firstSeconds();
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<FixerComponent> (this)] { if (safe != nullptr) safe->view.zoomToFit(); });
                }
            }
        }
        view.nowLine = tod ? (double) (juce::Time::currentTimeMillis() - L->originMs) / 1000.0 : -1.0;
        playhead.setCaption (tod ? "PLAYHEAD - TIME OF DAY" : "PLAYHEAD  H : M : S . MS");
        playhead.set (L != nullptr ? L->timeText (view.playhead) : hmsF (view.playhead));
        if (juce::Time::getMillisecondCounter() - lastNotesMs > 500) { lastNotesMs = juce::Time::getMillisecondCounter(); refreshNotes(); }
        if (live >= 0.0) behind.set (juce::String (live - view.playhead, 1) + " s", (live - view.playhead) > app.delaySeconds + 5.0 ? juce::Colour (0xffffb050) : juce::Colours::white);
        else behind.set ("-");
        playBtn.setIcon (app.isPlaying() ? Icon::Stop : Icon::Play, "Play / Stop");
        for (auto* b : { &backBtn, &fwdBtn }) b->setEnabled (! app.follow);
        if (app.follow) { if (app.isPlaying()) view.keepInView (view.playhead, true); else if (we >= 0.0) view.keepInView (we, true); }
        else if (app.isPlaying()) view.keepInView (view.playhead, true);
        juce::String s;
        if (! app.model.connected) s = "Waiting for the recorder: start fermata:Live (the Primary) on this PC.";
        else if (! app.model.sessionOn) s = "No session running: the takes of the project are shown; the live recording appears when the Primary starts the session.";
        else if (app.model.recordingTake != 0) s = "Take " + juce::String (app.model.recordingTake) + " recording. " + (app.follow ? "Following " + juce::String (app.delaySeconds, 1) + " s behind the disk." : juce::String ("Not following (F follows again)."));
        else s = "The session is on, no take recording.";
        if (app.lastMessage.isNotEmpty()) s << "   |   " << app.lastMessage;
        status.setText (s, juce::dontSendNotification);
        if (job != nullptr && job->isRunning()) progressLabel.setText ("Working it out... " + juce::String ((int) std::lround (job->progress() * 100.0f)) + " %", juce::dontSendNotification);
        if (juce::Time::getMillisecondCounter() < flashUntil + 100) repaint();
        view.repaintIfChanged();
    }
    void seek (double s)
    {
        if (app.follow) { app.say ("Following the Primary: untick Follow to move the playhead"); return; }
        s = juce::jmax (0.0, s);
        app.cursorSeconds = s;
        if (app.isPlaying()) app.play (s);
    }
    void togglePlay()
    {
        if (app.isPlaying()) { app.stop(); if (app.follow) { app.setFollow (false); followBtn.setToggleState (false, juce::dontSendNotification); } return; }
        app.play (app.follow ? juce::jmax (0.0, app.writtenEndSeconds() - app.delaySeconds) : app.cursorSeconds);
    }
    void goToFlag (const juce::String& id, double s)
    {
        if (s < 0.0) { app.say ("That flag is not on this piece's timeline (it was pinned between takes)"); return; }
        app.setFollow (false); followBtn.setToggleState (false, juce::dontSendNotification);
        app.cursorSeconds = juce::jmax (0.0, s - 2.0);
        if (app.isPlaying()) app.stop();
        view.keepInView (s, false);
        flags.choose (id);
    }
    void openMixer()
    {
        if (mixerWindow != nullptr) { mixerWindow->toFront (true); return; }
        mixModel = std::make_unique<FixerMixModel> (app);
        juce::Component::SafePointer<FixerComponent> safe (this);
        mixerWindow = std::make_unique<ToolWindow> ("Mixer - the Fixer's own", [safe] (ToolWindow*) { if (safe != nullptr) juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->mixerWindow.reset(); }); });
        auto* v = new LiveMixerView (*mixModel);
        v->setSize (1000, 600);
        mixerWindow->setContentOwned (new KeysToFixer (v, [safe] (const juce::KeyPress& k) { return safe != nullptr && safe->keyPressed (k); }), true);
        mixerWindow->centreWithSize (mixerWindow->getWidth(), mixerWindow->getHeight());
        mixerWindow->setVisible (true);
        if (auto* top = getTopLevelComponent()) makeOwnedBy (*mixerWindow, *top);
    }

    // ---- ReTouch
    const ShownTake* selectedTake() const
    {
        const auto L = app.layout();
        if (L == nullptr || selB <= selA) return nullptr;
        const auto* t = L->takeAt (selA + 1.0e-6);
        if (t == nullptr || selB > t->end() + 1.0e-6) return nullptr;
        return t;
    }
    void refreshFixControls()
    {
        const int k = fixKind.getSelectedId();
        if (k == 1) { amountLabel.setText ("Sensitivity (1 - 10)", juce::dontSendNotification); amount.setRange (1.0, 10.0, 0.5); amount.setValue (6.0, juce::dontSendNotification); }
        else if (k == 2 || k == 3) { amountLabel.setText ("Strength (%)", juce::dontSendNotification); amount.setRange (10.0, 100.0, 1.0); amount.setValue (100.0, juce::dontSendNotification); }
        else if (k == 4) { amountLabel.setText ("Amount (dB more / less)", juce::dontSendNotification); amount.setRange (-6.0, 6.0, 0.5); amount.setValue (0.0, juce::dontSendNotification); }
        else if (k == 6) { amountLabel.setText ("Soft <-> Hard (dB off)", juce::dontSendNotification); amount.setRange (6.0, 40.0, 1.0); amount.setValue (30.0, juce::dontSendNotification); }
        else { amountLabel.setText ("Gain (dB)", juce::dontSendNotification); amount.setRange (-30.0, 12.0, 0.5); amount.setValue (-6.0, juce::dontSendNotification); }
        const auto* t = selectedTake();
        if (selB <= selA) selLabel.setText ("Drag on the timeline to choose what to fix: every track of the take (Alt + drag: only the tracks you drag over; P: a whole take, its header clicked first).", juce::dontSendNotification);
        else if (t == nullptr) selLabel.setText ("The part chosen must lie inside one take.", juce::dontSendNotification);
        else selLabel.setText ("Take " + juce::String (t->number) + ", " + hmsF (selA - t->start) + " - " + hmsF (selB - t->start) + " (" + juce::String (selB - selA, 2) + " s) on " + juce::String ((int) selTracks.size()) + " track(s)", juce::dontSendNotification);
        saveBtn.setEnabled (lastTry != nullptr);
        abBtn.setEnabled (lastTry != nullptr);
        retouchBtn.setEnabled (t != nullptr && ! selTracks.empty());
        if (lastTry == nullptr) { abBtn.setToggleState (false, juce::dontSendNotification); abBtn.setButtonText ("Fixed / original"); }   // (lit only while a try can be heard)
    }
    void tryFix()
    {
        const auto* t = selectedTake();
        const auto L = app.layout();
        if (t == nullptr || L == nullptr || selTracks.empty()) { app.say ("Choose a part of one take first (drag over the tracks)"); return; }
        const double we = app.writtenEndSeconds();
        if (t->recording && we >= 0.0 && selB > we) { app.say ("That part is not on the disk yet: wait a moment"); return; }
        auto fx = std::make_shared<FixTry>();
        fx->takeNumber = t->number; fx->rate = t->rate; fx->session = t->session; fx->sessionStart = t->sessionStart;
        fx->sessionFolder = L->sessionFolder; fx->writtenTo = app.model.writtenTo();
        fx->from = (juce::int64) std::llround ((selA - t->start) * t->rate); fx->to = (juce::int64) std::llround ((selB - t->start) * t->rate);
        fx->takeLength = t->recording ? (juce::int64) std::llround (((we >= 0.0 ? we : t->end()) - t->start) * t->rate) : t->lengthSamples;
        for (auto& id : selTracks)
            if (auto* lt = L->findTrack (id))
            {
                FixTry::Track tr; tr.id = id; tr.name = lt->name; tr.channels = lt->channels; tr.sessionIndex = lt->sessionIndex;
                for (auto& f : t->files) if (f.trackId == id) tr.file = f.file;
                fx->tracks.push_back (tr);
            }
        if (const auto why = whyTooLong (*fx, false); why.isNotEmpty()) { app.say (why); return; }
        auto& spec = fx->spec;
        const double v = amount.getValue();
        switch (fixKind.getSelectedId())
        {
            case 1: spec.kind = FixSpec::Kind::Declick; spec.sensitivity = v; fx->what = "De-click (sensitivity " + juce::String (v, 1) + ")"; break;
            case 2: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Attenuate; spec.repair.strength = v / 100.0;
                    fx->what = "Repair, pulling down what sticks out (" + juce::String ((int) v) + " %)"; break;
            case 3: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Replace; spec.repair.strength = v / 100.0;
                    fx->what = "Repair, rebuilt from the sound around it (" + juce::String ((int) v) + " %)"; break;
            case 4: spec.kind = FixSpec::Kind::Denoise; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.dn.amountDb = v;
                    fx->what = "Denoise (" + juce::String (v, 1) + " dB)"; break;
            case 6: spec.kind = FixSpec::Kind::Dehum; spec.f0 = 20.0; spec.f1 = juce::jmin (20000.0, t->rate * 0.5); spec.hum.hardness = (v - 6.0) / 34.0;      // (the hums are found in runFix, from the part itself)
                    fx->what = "De-hum (" + juce::String ((int) v) + " dB off)"; break;
            default: spec.kind = FixSpec::Kind::Patch; spec.f0 = 20.0; spec.f1 = juce::jmin (24000.0, t->rate * 0.5); spec.repair.mode = RepairMode::Gain; spec.repair.gainDb = v;
                    fx->what = "Gain " + juce::String (v, 1) + " dB"; break;
        }
        if (job == nullptr) job = new AudioJob();
        tryBtn.setEnabled (false);
        juce::Component::SafePointer<FixerComponent> safe (this);
        const double from = selA;
        job->start ([fx] (AudioJob& j) { return runFix (*fx, j); }, [safe, fx, from] (const juce::String& err)
        {
            if (safe == nullptr) return;
            safe->tryBtn.setEnabled (true);
            safe->progressLabel.setText ({}, juce::dontSendNotification);
            if (err.isNotEmpty()) { safe->app.say ("ReTouch: " + err); return; }
            auto a = std::make_shared<Player::Audition>();
            a->takeNumber = fx->takeNumber; a->from = fx->from; a->tracks = fx->fixed;
            safe->lastTry = a; safe->lastFix = fx;
            safe->abBtn.setToggleState (true, juce::dontSendNotification); safe->abBtn.setButtonText ("Hearing: FIXED");
            safe->app.setAudition (a);
            if (safe->app.follow) { safe->app.setFollow (false); safe->followBtn.setToggleState (false, juce::dontSendNotification); }
            safe->app.play (juce::jmax (0.0, from - 1.0));
            safe->app.say ("Trying: " + fx->what + " - playing from a second before. Save it as a version, or change it and try again.");
            safe->refreshFixControls();
        });
    }
    /** Why a fix version cannot be kept now ("" = it can). */
    juce::String whyNoVersion()
    {
        if (app.pieceWindow() == nullptr || ! app.hasProject()) return "No project to keep the version in.";
        // 0.8.0: nor while the recorder is not answering - the Fixer still shows the session it last heard of, but there is nobody to keep the version:
        // its files were written and "Kept as fix version" said, and the recorder that came back (a new one, with no session yet) refused it
        if (! app.model.connected) return "The recorder is not answering, and a fix version is kept by its session. Nothing was saved - keep it again when it is back.";
        // the session keeps the fix versions (every drive's index) and the Primary puts them into the project: with no session there is nowhere
        // safe for one - nothing is written, and the fix stays to be kept once the Primary has started the session (Pre-take is enough)
        if (! app.model.sessionOn) return "A fix version is kept by the session: the Primary must start it first (Pre-take is enough). Nothing was saved - keep it again then.";
        return {};
    }
    /** Keeps a fix as a new version of the take - its own files in the take's Fixes folder (the recording is not changed) - and tells the recorder, which
        keeps it with the session and hands it to the Primary's window for the project. 'fixed': each track's channels for the take's samples [from, to). */
    bool keepVersion (int takeNumber, juce::int64 from, juce::int64 to, double rate, const std::vector<FixTry::Track>& tracks,
                      const std::map<juce::Uuid, std::vector<std::vector<float>>>& fixed, const juce::String& what)
    {
        if (const auto why = whyNoVersion(); why.isNotEmpty()) { app.say (why); return false; }
        auto* w = app.pieceWindow();
        // the attempt number: one more than the versions already made over this place of this take
        int n = 0; juce::String replaces;
        for (auto& v : versionRows())                                  // (the session's and the project's)
            if (v.take == takeNumber && v.from < to && v.to > from && v.version > n) { n = v.version; replaces = v.id; }
        const auto folder = app.project.takeFolder (*w).getChildFile ("Fixes");
        folder.createDirectory();
        juce::Array<juce::var> files;
        for (auto& tr : tracks)
        {
            auto it = fixed.find (tr.id);
            if (it == fixed.end()) continue;
            auto f = audioops::uniqueFile (folder.getChildFile (juce::String (takeNumber).paddedLeft ('0', 3) + " fix " + juce::String (n + 1) + " at " + hmsF ((double) from / rate).replaceCharacter (':', '-') + " - " + sanitiseForFile (tr.name) + ".wav"));
            if (! audioops::writeWav (f, it->second, rate)) { app.say ("The fix could not be written to " + f.getFullPathName()); return false; }
            auto x = obj(); put (x, "track", tr.id.toString()); put (x, "trackName", tr.name); put (x, "file", f.getFullPathName()); put (x, "channels", tr.channels);
            files.add (x);
        }
        auto v = obj();
        put (v, "id", juce::Uuid().toString()); put (v, "takeNumber", takeNumber); put (v, "from", from); put (v, "to", to); put (v, "rate", rate);
        put (v, "files", files); put (v, "what", what); put (v, "note", fixNote.getText().trim()); put (v, "by", app.stationName()); put (v, "madeAt", juce::Time::getCurrentTime().toMilliseconds());
        put (v, "version", n + 1); put (v, "window", w->name);
        if (replaces.isNotEmpty()) put (v, "replaces", replaces);
        if (flags.chosen.isNotEmpty()) put (v, "flag", flags.chosen);   // (a flag already in the project too: the review shows what it was for)
        app.announceFix (v);
        if (tellPrimary.getToggleState() && flags.chosenIsLive())
            app.reply (flags.chosen, "Fix version " + juce::String (n + 1) + " saved (take " + juce::String (takeNumber) + ", " + hmsF ((double) from / rate, false) + "): " + what
                                     + (fixNote.getText().trim().isNotEmpty() ? " - " + fixNote.getText().trim() : juce::String()));
        app.say ("Kept as fix version " + juce::String (n + 1) + " of take " + juce::String (takeNumber) + " (" + juce::String (files.size()) + " file(s) in " + folder.getFileName() + "). The recording is unchanged.");
        fixNote.clear();
        return true;
    }
    void saveVersion()
    {
        if (lastFix == nullptr || lastTry == nullptr) return;
        const auto& fx = *lastFix;
        if (! keepVersion (fx.takeNumber, fx.from, fx.to, fx.rate, fx.tracks, fx.fixed, fx.what)) return;
        lastTry.reset(); lastFix.reset(); app.setAudition (nullptr);
        refreshFixControls();
    }

    // ---- 0.4.0: fermata's ReTouch window, on the part chosen (the Fixer only: the Primary has none)
    void openReTouchWindow()
    {
        if (retouchWindow != nullptr) { retouchWindow->toFront (true); return; }
        const auto* t = selectedTake();
        const auto L = app.layout();
        if (t == nullptr || L == nullptr || selTracks.empty()) { app.say ("Choose a part of one take first (drag over the tracks), then open the ReTouch window"); return; }
        const double we = app.writtenEndSeconds();
        if (t->recording && we >= 0.0 && selB > we) { app.say ("That part is not on the disk yet: wait a moment"); return; }
        // the area: the part chosen and 2 seconds either side (the clean sound round a fix), inside the take and what is on the disk
        auto src = std::make_shared<FixTry>();
        src->takeNumber = t->number; src->rate = t->rate; src->session = t->session; src->sessionStart = t->sessionStart;
        src->sessionFolder = L->sessionFolder; src->writtenTo = app.model.writtenTo();
        src->takeLength = t->recording ? (juce::int64) std::llround (((we >= 0.0 ? we : t->end()) - t->start) * t->rate) : t->lengthSamples;
        constexpr double pad = 2.0;
        src->from = juce::jmax ((juce::int64) 0, (juce::int64) std::llround ((selA - t->start - pad) * t->rate));
        src->to = juce::jmin (src->takeLength, (juce::int64) std::llround ((selB - t->start + pad) * t->rate));
        if (src->to - src->from < (juce::int64) (0.2 * t->rate)) { app.say ("That part is too short for the ReTouch window"); return; }
        for (auto& id : selTracks)
            if (auto* lt = L->findTrack (id))
            {
                FixTry::Track tr; tr.id = id; tr.name = lt->name; tr.channels = lt->channels; tr.sessionIndex = lt->sessionIndex;
                for (auto& f : t->files) if (f.trackId == id) tr.file = f.file;
                src->tracks.push_back (tr);
            }
        if (src->tracks.empty()) { app.say ("None of the tracks chosen are in that take"); return; }
        if (const auto why = whyTooLong (*src, true); why.isNotEmpty()) { app.say (why); return; }
        lastTry.reset(); lastFix.reset(); app.setAudition (nullptr);           // (one thing heard at a time: what was being tried here goes)
        refreshFixControls();

        const double rate = t->rate;
        const double areaStart = t->start + (double) src->from / rate, areaEnd = t->start + (double) src->to / rate;     // (on the window's timeline)
        struct State
        {
            std::mutex lock; std::shared_ptr<Player::Audition> prepared; juce::String what;   // (made on the job's thread, used on the message thread)
            bool ours = false, looping = false; double playA = 0.0, playB = 0.0;               // what the ReTouch window asked to play (window seconds)
        };
        auto st = std::make_shared<State>();
        auto room = std::make_shared<const Project::RoomTone> (app.project.roomTone);
        FixerApp* ap = &app;
        juce::Component::SafePointer<FixerComponent> safe (this);

        fixtools::RepairContext c;
        c.rate = rate; c.length = src->to - src->from; c.refA = app.project.pitchPipe.refA;
        c.roomSource = room->source;
        for (auto& tr : src->tracks)
        {
            const auto* rt = room->forTrack (tr.id);
            c.roomMono.push_back (rt != nullptr ? rt->mono : denoise::Profile());
            c.trackIds.push_back (tr.id); c.trackNames.push_back (tr.name);
        }
        c.where = "take " + juce::String (t->number); c.areaStart = (double) src->from / rate;
        c.title = "ReTouch - take " + juce::String (t->number);
        c.what = "The " + juce::String ((int) src->tracks.size()) + (src->tracks.size() == 1 ? " track" : " tracks") + " chosen of take " + juce::String (t->number) + ", "
                 + hmsF ((double) src->from / rate, false) + " - " + hmsF ((double) src->to / rate, false) + " (what you chose and 2 seconds either side), as one picture. "
                 "Draw a box round the noise (time and pitch) and press ReTouch (or De-click for clicks, Denoise for the room noise), listen, then Keep as a fix version: "
                 "a new version for fermata's review. The recording is never changed.";
        c.load = [src] (AudioJob& j)
        {
            std::vector<std::vector<float>> tracks;
            ChunkTimeline timeline; openTimeline (*src, timeline);
            const int n = (int) (src->to - src->from);
            for (size_t i = 0; i < src->tracks.size(); ++i)
            {
                if (j.cancelled()) break;
                j.setProgress ((float) i / (float) juce::jmax ((size_t) 1, src->tracks.size()));
                std::vector<std::vector<float>> ch;
                readTakeSpan (*src, src->tracks[i], timeline, src->from, n, ch);           // (what cannot be read shows as silence)
                std::vector<float> mono ((size_t) n, 0.0f);
                for (auto& x : ch) for (size_t k = 0; k < mono.size() && k < x.size(); ++k) mono[k] += x[k] / (float) ch.size();
                tracks.push_back (std::move (mono));
            }
            return tracks;
        };
        c.readChannels = [src] (juce::int64 a, juce::int64 b, AudioJob& j)
        {
            std::vector<std::vector<std::vector<float>>> out;
            ChunkTimeline timeline; openTimeline (*src, timeline);
            for (auto& tr : src->tracks)
            {
                if (j.cancelled()) break;
                std::vector<std::vector<float>> ch;
                readTakeSpan (*src, tr, timeline, src->from + a, (int) juce::jmax ((juce::int64) 0, b - a), ch);
                out.push_back (std::move (ch));
            }
            return out;
        };
        c.prepare = [src, st, room] (const std::vector<fixtools::RepairOp>& ops, AudioJob& j) -> juce::String
        {
            auto a = std::make_shared<Player::Audition>();
            const auto err = renderOps (*src, ops, room, j, *a);
            if (err.isNotEmpty()) return err;
            const std::lock_guard<std::mutex> g (st->lock);
            st->prepared = a; st->what = describeOps (ops);
            return {};
        };
        auto prepared = [st] { const std::lock_guard<std::mutex> g (st->lock); return st->prepared; };
        c.apply = [ap, prepared] { auto a = prepared(); if (a == nullptr) return false; ap->setAudition (a); return true; };
        c.unapply = [ap] { ap->setAudition (nullptr); };
        c.discard = [st] { const std::lock_guard<std::mutex> g (st->lock); st->prepared.reset(); };
        c.acceptText = "Keep as a fix version";
        c.acceptTip = "Keeps all the fixes made here as one new fix version of this take (its own files: the recording and the project are never changed). fermata's review takes it "
                      "from there. The note and \"Tell the Primary\" under the Fixer's ReTouch go with it.";
        c.mayAccept = [safe] () -> juce::String { return safe == nullptr ? juce::String ("The Fixer's window has closed.") : safe->whyNoVersion(); };
        c.accept = [safe, src, st, prepared, ap]
        {
            if (auto a = prepared(); a != nullptr && safe != nullptr && ! a->tracks.empty() && ! a->tracks.begin()->second.empty())
            {
                const auto length = (juce::int64) a->tracks.begin()->second.front().size();
                juce::String what; { const std::lock_guard<std::mutex> g (st->lock); what = st->what; }
                safe->keepVersion (src->takeNumber, a->from, a->from + length, src->rate, src->tracks, a->tracks, what);
            }
            ap->setAudition (nullptr);                                         // (kept: it is a version now, for the review; the Fixer hears the recording again)
        };
        c.play = [ap, st, areaStart, safe] (double a, double b, bool loop)
        {
            if (ap->follow) { ap->setFollow (false); if (safe != nullptr) safe->followBtn.setToggleState (false, juce::dontSendNotification); }
            st->ours = true; st->looping = loop; st->playA = areaStart + a; st->playB = areaStart + b;
            if (ap->player != nullptr) ap->player->setLoop (loop, st->playA, st->playB);
            ap->play (st->playA);
        };
        c.setLooping = [ap, st] (bool on) { st->looping = on; if (ap->player != nullptr) ap->player->setLoop (on && st->ours, st->playA, st->playB); };
        c.position = [ap, st, areaStart, areaEnd] () -> double
        {
            if (! ap->isPlaying())
            {
                if (st->ours) { st->ours = false; if (ap->player != nullptr) ap->player->setLoop (false, 0.0, 0.0); }
                return -1.0;
            }
            const double p = ap->playheadSeconds();
            if (st->ours && ! st->looping && p >= st->playB) { ap->stop(); st->ours = false; return -1.0; }    // (the end of what is in view: as fermata stops there)
            return p >= areaStart && p <= areaEnd ? p - areaStart : -1.0;
        };
        c.stop = [ap, st]
        {
            if (ap->isPlaying()) ap->stop();
            st->ours = false;
            if (ap->player != nullptr) ap->player->setLoop (false, 0.0, 0.0);
        };
        // the consonants the project keeps can be pasted (read from the project; the Fixer never writes it - copying one is done in fermata)
        c.consonantList = [ap] { return ap->project.consonants; };
        c.loadConsonant = [ap] (const juce::Uuid& id, juce::String& err) -> std::shared_ptr<const ConsonantAudio>
        {
            const auto* d = ap->project.findConsonant (id);
            if (d == nullptr) { err = "That consonant is no longer kept in this project."; return nullptr; }
            return consonants::load (*d, fixtools::formats(), err);
        };
        fixtools::gSpecPrefs = app.settings.get();
        const auto title = c.title + "  -  fermata:Live Fixer";
        auto* w = new ReTouchWindow (title, std::make_unique<fixtools::RepairDialog> (std::move (c)), app.settings.get(), getTopLevelComponent());
        retouchWindow = w;
        w->setVisible (true);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int) { if (safe != nullptr) safe->refreshFixControls(); }), true);
        app.say ("ReTouch window open on take " + juce::String (t->number) + ": what is kept there becomes a fix version.");
    }
    // the versions list: the session's fix versions, and those the project already has (made in an earlier session; fermata's review state)
    struct VersionRow { juce::String id; int take = 0; juce::int64 from = 0, to = 0; double rate = 48000.0; int version = 1; juce::String what; int state = -1; };
    std::vector<VersionRow> versionRows() const
    {
        std::vector<VersionRow> out;
        std::set<juce::String> have;
        for (auto& v : app.model.fixes)
        {
            VersionRow r; r.id = v["id"].toString(); r.take = (int) v["takeNumber"]; r.from = (juce::int64) v["from"]; r.to = (juce::int64) v["to"];
            r.rate = (double) v["rate"] > 0 ? (double) v["rate"] : 48000.0; r.version = juce::jmax (1, (int) v["version"]); r.what = v["what"].toString();
            have.insert (r.id); out.push_back (r);
        }
        if (auto* w = const_cast<FixerApp&> (app).pieceWindow())
            for (auto& v : w->fixVersions)
                if (have.count (v.id.toString()) == 0)
                {
                    VersionRow r; r.id = v.id.toString(); r.take = v.takeNumber; r.from = v.startSample; r.to = v.endSample; r.rate = v.sampleRate > 0 ? v.sampleRate : 48000.0;
                    r.version = v.versionNumber; r.what = v.what; r.state = v.state;
                    out.push_back (r);
                }
        std::sort (out.begin(), out.end(), [] (const VersionRow& a, const VersionRow& b) { return a.take != b.take ? a.take < b.take : (a.from != b.from ? a.from < b.from : a.version < b.version); });
        return out;
    }
    int getNumRows() override { return (int) versionRows().size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        const auto rows = versionRows();
        if (! juce::isPositiveAndBelow (row, (int) rows.size())) return;
        const auto& v = rows[(size_t) row];
        g.fillAll (selected ? theme::selected : (row % 2 == 0 ? theme::row : theme::rowAlt));
        g.setColour (theme::text); g.setFont (juce::FontOptions (12.0f));
        const juce::String state = v.state == 1 ? "  (approved in the review)" : v.state == 2 ? "  (not used in the review)" : juce::String();
        g.drawText ("Take " + juce::String (v.take) + "  " + hmsF ((double) v.from / v.rate, false) + "  v" + juce::String (v.version) + "  " + v.what + state, 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }

    FixerApp& app;
    LiveBadge badge;
    juce::Label pieceLabel, delayLabel, status, fixTitle, selLabel, amountLabel, progressLabel;
    juce::ToggleButton followBtn { "Follow Primary" }, tellPrimary { "Tell the Primary" };
    juce::TextEditor delayBox, fixNote;
    IconButton backBtn, playBtn, fwdBtn;
    FixerTimeBox playhead { "PLAYHEAD  H : M : S . MS", 30.0f }, behind { "BEHIND LIVE", 26.0f };
    juce::TextButton noteBtn { "Note [N]" }, todBtn { "Time of day" };
    juce::Label nameLabel;
    juce::TextEditor nameBox;
    NotePanel notePanel;
    juce::TextButton seatBtn { "Second seat..." }, lookBtn { "Look..." };
    std::unique_ptr<SecondSeat> seat;                                     // (0.6.0: made in the constructor, gone first in the destructor)
    std::unique_ptr<ToolWindow> seatWindow;
    int chosenTake = 0;
    juce::int64 shownLayoutKey = -2;
    juce::uint32 lastNotesMs = 0;
    juce::TextButton mixerBtn { "Mixer [M]" }, tryBtn { "Try it" }, abBtn { "Hearing: FIXED" }, saveBtn { "Keep as a fix version" }, retouchBtn { "ReTouch window..." };
    juce::Component::SafePointer<ReTouchWindow> retouchWindow;                // (0.4.0: fermata's ReTouch window, while it is open)
    juce::ComboBox fixKind;
    juce::Slider amount;
    FixerFlags flags;
    LiveTakeView view;
    juce::ListBox versions;
    double selA = -1.0, selB = -1.0;
    std::vector<juce::Uuid> selTracks;
    AudioJob* job = nullptr;
    std::shared_ptr<const Player::Audition> lastTry;
    std::shared_ptr<FixTry> lastFix;
    std::unique_ptr<FixerMixModel> mixModel;
    std::unique_ptr<ToolWindow> mixerWindow;
    juce::uint32 flashUntil = 0;
    std::set<juce::String> seenFlags;
};

FixerWindow::FixerWindow (FixerApp& app) : juce::DocumentWindow ("fermata:Live " + app.version + " - Fixer", theme::window, juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    content = new FixerComponent (app);
    setContentOwned (content, true);
    setResizeLimits (900, 560, 6000, 4000);
    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
    centreWithSize (juce::jmin (1500, area.getWidth() - 40), juce::jmin (900, area.getHeight() - 40));
    setVisible (true);
}
FixerWindow::~FixerWindow() = default;
void FixerWindow::flagArrived (const FlagRec& f) { if (content != nullptr) content->flagArrived (f); }
} // namespace td::live
