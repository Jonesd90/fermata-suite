#pragma once
#include "Common.h"
#include <algorithm>

namespace td
{
/** The channel format of an Int Bus or an Ext Bus (3.39.0). Every bus was stereo before, and a project that says nothing still means stereo.

    The value is also the "layout" an insert is prepared with (InsertProcessor::prepare's third argument): 1 = mono, 2 = stereo (what every
    caller always passed), anything else one of these formats. Saved by key ("9.1.6"), never by number.

    The two monitor formats are for the Ext Bus that carries the Dolby Atmos Composer: it plays its speaker channels first and then, when
    headphone monitoring is on as well, a binaural pair (Composer manual, "Monitoring"), so its bus needs both. */
enum class BusFormat : int
{
    Stereo = 2,
    S51    = 51,
    S71    = 71,
    S712   = 712,
    S514   = 514,
    S714   = 714,
    S914   = 914,
    S916   = 916,
    S714Hp = 7140,          // Ext Bus only: 7.1.4 speakers + headphone pair (14 channels)
    S916Hp = 9160           // Ext Bus only: 9.1.6 speakers + headphone pair (18 channels)
};

static_assert (kMaxBusChannels >= kMaxTrackChannels, "a bus must be able to carry the widest track");    // (kMaxBusChannels: Common.h)

namespace surround
{
/** One loudspeaker (or the headphone pair's sides). Names as in the Dolby Atmos Composer's manual: Ls / Rs are 5.1's surrounds; Lss / Rss the side
    and Lrs / Rrs the rear surrounds of 7.1 and up; Lw / Rw the wides; Ltf, Ltm, Ltr (and right) the top front, top middle and top rear. */
enum class Spk : juce::uint8 { L, R, C, LFE, Ls, Rs, Lss, Rss, Lrs, Rrs, Lw, Rw, Ltf, Rtf, Ltm, Rtm, Ltr, Rtr, HpL, HpR, count };

struct SpeakerInfo
{
    const char* shortName;       // meters, menus ("Ltf")
    const char* longName;        // tooltips ("Left top front")
    float az, el;                // degrees, as the Composer's table (positive azimuth = left)
    float x, y, z;               // where the panner puts it: x -1 left .. +1 right, y +1 front .. -1 back, z 0 ear level .. 1 overhead
    bool lfe, phones;
};

inline const SpeakerInfo& info (Spk s) noexcept
{
    // Positions: azimuth / elevation from the Composer's table; x / y / z are the same speakers on the room's grid, as Dolby's own panners place
    // bed channels (front row y = +1, side row 0, rear row -1; wides half way between front and side; the tops above the corners and sides).
    static const SpeakerInfo table[(int) Spk::count] =
    {
        { "L",   "Left",                  30.0f,   0.0f,  -1.0f,  1.0f, 0.0f, false, false },
        { "R",   "Right",                -30.0f,   0.0f,   1.0f,  1.0f, 0.0f, false, false },
        { "C",   "Centre",                 0.0f,   0.0f,   0.0f,  1.0f, 0.0f, false, false },
        { "LFE", "LFE",                    0.0f,   0.0f,   0.0f,  1.0f, 0.0f, true,  false },
        { "Ls",  "Left surround",        110.0f,   0.0f,  -1.0f, -1.0f, 0.0f, false, false },
        { "Rs",  "Right surround",      -110.0f,   0.0f,   1.0f, -1.0f, 0.0f, false, false },
        { "Lss", "Left side surround",    90.0f,   0.0f,  -1.0f,  0.0f, 0.0f, false, false },
        { "Rss", "Right side surround",  -90.0f,   0.0f,   1.0f,  0.0f, 0.0f, false, false },
        { "Lrs", "Left rear surround",   135.0f,   0.0f,  -1.0f, -1.0f, 0.0f, false, false },
        { "Rrs", "Right rear surround", -135.0f,   0.0f,   1.0f, -1.0f, 0.0f, false, false },
        { "Lw",  "Left wide",             45.0f,   0.0f,  -1.0f,  0.5f, 0.0f, false, false },
        { "Rw",  "Right wide",           -45.0f,   0.0f,   1.0f,  0.5f, 0.0f, false, false },
        { "Ltf", "Left top front",        45.0f,  45.0f,  -1.0f,  1.0f, 1.0f, false, false },
        { "Rtf", "Right top front",      -45.0f,  45.0f,   1.0f,  1.0f, 1.0f, false, false },
        { "Ltm", "Left top middle",       90.0f,  45.0f,  -1.0f,  0.0f, 1.0f, false, false },
        { "Rtm", "Right top middle",     -90.0f,  45.0f,   1.0f,  0.0f, 1.0f, false, false },
        { "Ltr", "Left top rear",        135.0f,  45.0f,  -1.0f, -1.0f, 1.0f, false, false },
        { "Rtr", "Right top rear",      -135.0f,  45.0f,   1.0f, -1.0f, 1.0f, false, false },
        { "HpL", "Headphones left",        0.0f,   0.0f,  -1.0f,  0.0f, 0.0f, false, true  },
        { "HpR", "Headphones right",       0.0f,   0.0f,   1.0f,  0.0f, 0.0f, false, true  },
    };
    return table[juce::jlimit (0, (int) Spk::count - 1, (int) s)];
}

/** A format's channels, in fermata's order: the Dolby / SMPTE order the Dolby Atmos Renderer and Composer use (L R C LFE, the surrounds, the
    wides, then the tops front to back), headphones last. This is the order of a bus's channels, its meters, its driver outputs and its files.
    (A plug-in is handed them in its own host order: see juceIndex.) */
struct Layout
{
    BusFormat format;
    const char* key;             // in the project file
    const char* name;            // in the windows
    int count;
    Spk spk[kMaxBusChannels];
    int indexOf (Spk s) const noexcept { for (int i = 0; i < count; ++i) if (spk[i] == s) return i; return -1; }
    bool has (Spk s) const noexcept    { return indexOf (s) >= 0; }
    bool hasTop() const noexcept       { for (int i = 0; i < count; ++i) if (info (spk[i]).z > 0.5f) return true; return false; }
};

using S = Spk;
inline const Layout* allLayouts (int& n) noexcept
{
    static const Layout table[] =
    {
        { BusFormat::Stereo, "stereo",   "Stereo", 2,  { S::L, S::R } },
        { BusFormat::S51,    "5.1",      "5.1",    6,  { S::L, S::R, S::C, S::LFE, S::Ls, S::Rs } },
        { BusFormat::S71,    "7.1",      "7.1",    8,  { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs } },
        { BusFormat::S712,   "7.1.2",    "7.1.2",  10, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Ltm, S::Rtm } },
        { BusFormat::S514,   "5.1.4",    "5.1.4",  10, { S::L, S::R, S::C, S::LFE, S::Ls, S::Rs, S::Ltf, S::Rtf, S::Ltr, S::Rtr } },
        { BusFormat::S714,   "7.1.4",    "7.1.4",  12, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Ltf, S::Rtf, S::Ltr, S::Rtr } },
        { BusFormat::S914,   "9.1.4",    "9.1.4",  14, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Lw, S::Rw, S::Ltf, S::Rtf, S::Ltr, S::Rtr } },
        { BusFormat::S916,   "9.1.6",    "9.1.6",  16, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Lw, S::Rw, S::Ltf, S::Rtf, S::Ltm, S::Rtm, S::Ltr, S::Rtr } },
        { BusFormat::S714Hp, "7.1.4+hp", "7.1.4 + headphones", 14, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Ltf, S::Rtf, S::Ltr, S::Rtr, S::HpL, S::HpR } },
        { BusFormat::S916Hp, "9.1.6+hp", "9.1.6 + headphones", 18, { S::L, S::R, S::C, S::LFE, S::Lss, S::Rss, S::Lrs, S::Rrs, S::Lw, S::Rw, S::Ltf, S::Rtf, S::Ltm, S::Rtm, S::Ltr, S::Rtr, S::HpL, S::HpR } },
    };
    n = (int) (sizeof (table) / sizeof (table[0]));
    return table;
}

/** The layout of a format; stereo's for anything unknown. */
inline const Layout& layout (BusFormat f) noexcept
{
    int n = 0;
    const auto* t = allLayouts (n);
    for (int i = 0; i < n; ++i) if (t[i].format == f) return t[i];
    return t[0];
}

inline int channels (BusFormat f) noexcept            { return layout (f).count; }
inline bool isSurround (BusFormat f) noexcept         { return f != BusFormat::Stereo; }
/** The Composer's monitor formats (speakers + headphones): offered for Ext Buses only. */
inline bool isMonitorFormat (BusFormat f) noexcept    { return f == BusFormat::S714Hp || f == BusFormat::S916Hp; }
/** How many channels go to the speakers (all but the headphone pair). */
inline int speakerChannels (BusFormat f) noexcept     { return channels (f) - (isMonitorFormat (f) ? 2 : 0); }
inline juce::String name (BusFormat f)                { return layout (f).name; }
inline juce::String key (BusFormat f)                 { return layout (f).key; }
inline juce::String channelLabel (BusFormat f, int c) { const auto& l = layout (f); return juce::isPositiveAndBelow (c, l.count) ? juce::String (info (l.spk[c]).shortName) : juce::String(); }

/** A format from its key in a file; stereo for a missing or unknown key (an older fermata wrote none: every bus was stereo). */
inline BusFormat parse (const juce::String& k) noexcept
{
    int n = 0;
    const auto* t = allLayouts (n);
    for (int i = 0; i < n; ++i) if (k.trim().equalsIgnoreCase (t[i].key)) return t[i].format;
    return BusFormat::Stereo;
}

/** A format from an insert's layout code (see BusFormat): 1 and 2 (mono, stereo) and anything unknown give Stereo. */
inline BusFormat fromLayoutCode (int code) noexcept
{
    int n = 0;
    const auto* t = allLayouts (n);
    for (int i = 0; i < n; ++i) if ((int) t[i].format == code) return t[i].format;
    return BusFormat::Stereo;
}

/** The formats offered when a bus is made or changed, in menu order: Int Buses get stereo and the seven surround formats, Ext Buses the monitor
    formats too. */
inline std::vector<BusFormat> offered (bool external)
{
    std::vector<BusFormat> v { BusFormat::Stereo, BusFormat::S51, BusFormat::S71, BusFormat::S712, BusFormat::S514, BusFormat::S714, BusFormat::S914, BusFormat::S916 };
    if (external) { v.push_back (BusFormat::S714Hp); v.push_back (BusFormat::S916Hp); }
    return v;
}

/** Which side a channel's meter belongs to for the stereo-only meters (Stream Deck, control surfaces): -1 left, +1 right, 0 both (C, LFE). */
inline int side (BusFormat f, int c) noexcept
{
    const auto& l = layout (f);
    if (! juce::isPositiveAndBelow (c, l.count)) return 0;
    const float x = info (l.spk[c]).x;
    return x < -0.01f ? -1 : x > 0.01f ? 1 : 0;
}

// ------------------------------------------------------------------ the panner
namespace detail
{
    constexpr float kHalfPi = 1.57079632679489662f;

    /** Constant-power gains across one row of speakers (sorted by x) for a point at x; 'skip' = an index left out (-1: none). */
    inline void rowGains (const float* xs, int n, int skip, float x, float* g) noexcept
    {
        int idx[kMaxBusChannels]; int m = 0;
        for (int i = 0; i < n; ++i) { g[i] = 0.0f; if (i != skip) idx[m++] = i; }
        if (m == 0) return;
        if (m == 1 || x <= xs[idx[0]]) { g[idx[0]] = 1.0f; return; }
        if (x >= xs[idx[m - 1]]) { g[idx[m - 1]] = 1.0f; return; }
        for (int k = 0; k + 1 < m; ++k)
        {
            const float a = xs[idx[k]], b = xs[idx[k + 1]];
            if (x >= a && x <= b)
            {
                const float t = b > a ? (x - a) / (b - a) : 0.0f;
                g[idx[k]] = std::cos (t * kHalfPi); g[idx[k + 1]] = std::sin (t * kHalfPi);
                return;
            }
        }
    }
}

/** The gain of each channel of format f (fermata's order) for one point source, keeping its power constant (the squares add up to 1).
      x      -1 (left) .. +1 (right)
      y      +1 (front) .. -1 (back)
      z      0 (ear level) .. 1 (overhead); a format without top speakers ignores it
      spread 0 .. 1: how much of it is spread evenly over all the speakers (1 = the same in every one)
      centre 0 .. 1: how much the centre speaker is used for a sound between L and R (0 = never: L and R make a phantom centre)
    Rows of speakers (front, wides, sides, rear: same height and same y) are crossfaded front to back, and the speakers in a row left to right,
    both at constant power; the ear-level and the top layer are crossfaded the same way by z. LFE and the headphone pair get nothing.
    Allocates nothing (the audio thread may call it). */
inline void pointGains (BusFormat f, float x, float y, float z, float spread, float centre, float* gains) noexcept
{
    const auto& l = layout (f);
    const int n = l.count;
    for (int i = 0; i < n; ++i) gains[i] = 0.0f;
    if (! std::isfinite (x)) x = 0.0f;
    if (! std::isfinite (y)) y = 1.0f;
    if (! std::isfinite (z)) z = 0.0f;
    if (! std::isfinite (spread)) spread = 0.0f;
    if (! std::isfinite (centre)) centre = 1.0f;
    x = juce::jlimit (-1.0f, 1.0f, x); y = juce::jlimit (-1.0f, 1.0f, y); z = juce::jlimit (0.0f, 1.0f, z);
    spread = juce::jlimit (0.0f, 1.0f, spread); centre = juce::jlimit (0.0f, 1.0f, centre);

    const bool top = l.hasTop();
    const float layerW[2] = { top ? std::cos (z * detail::kHalfPi) : 1.0f, top ? std::sin (z * detail::kHalfPi) : 0.0f };
    int speakers = 0;
    for (int i = 0; i < n; ++i) if (! info (l.spk[i]).lfe && ! info (l.spk[i]).phones) ++speakers;
    if (speakers == 0) return;

    for (int layer = 0; layer < 2; ++layer)
    {
        if (layerW[layer] <= 0.0f) continue;
        // the rows of this layer: their y, front first
        float rows[kMaxBusChannels]; int nr = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto& s = info (l.spk[i]);
            if (s.lfe || s.phones || (s.z > 0.5f) != (layer == 1)) continue;
            bool known = false;
            for (int r = 0; r < nr; ++r) if (std::abs (rows[r] - s.y) < 1.0e-4f) known = true;
            if (! known) rows[nr++] = s.y;
        }
        if (nr == 0) continue;
        std::sort (rows, rows + nr, [] (float a, float b) { return a > b; });
        int ra = 0, rb = -1; float wa = 1.0f, wb = 0.0f;
        if (nr > 1 && y < rows[0])
        {
            if (y <= rows[nr - 1]) ra = nr - 1;
            else
                for (int r = 0; r + 1 < nr; ++r)
                    if (y <= rows[r] && y > rows[r + 1])
                    {
                        const float t = (rows[r] - y) / (rows[r] - rows[r + 1]);
                        ra = r; rb = r + 1; wa = std::cos (t * detail::kHalfPi); wb = std::sin (t * detail::kHalfPi);
                        break;
                    }
        }
        const int pick[2] = { ra, rb };
        const float pickW[2] = { wa, wb };
        for (int p = 0; p < 2; ++p)
        {
            if (pick[p] < 0 || pickW[p] <= 0.0f) continue;
            const float ry = rows[pick[p]];
            // the speakers of this row, by x
            int idx[kMaxBusChannels]; float xs[kMaxBusChannels]; int m = 0;
            for (int i = 0; i < n; ++i)
            {
                const auto& s = info (l.spk[i]);
                if (s.lfe || s.phones || (s.z > 0.5f) != (layer == 1) || std::abs (s.y - ry) > 1.0e-4f) continue;
                int k = m++;
                while (k > 0 && xs[k - 1] > s.x) { xs[k] = xs[k - 1]; idx[k] = idx[k - 1]; --k; }
                xs[k] = s.x; idx[k] = i;
            }
            float ga[kMaxBusChannels], gb[kMaxBusChannels];
            int mid = -1;
            for (int k = 0; k < m; ++k) if (std::abs (xs[k]) < 1.0e-4f && m > 2) mid = k;     // a centre speaker between others
            detail::rowGains (xs, m, -1, x, ga);
            if (mid >= 0 && centre < 1.0f)
            {
                detail::rowGains (xs, m, mid, x, gb);
                for (int k = 0; k < m; ++k) ga[k] = std::sqrt (centre * ga[k] * ga[k] + (1.0f - centre) * gb[k] * gb[k]);
            }
            for (int k = 0; k < m; ++k) gains[idx[k]] = layerW[layer] * pickW[p] * ga[k];
        }
    }
    if (spread > 0.0f)
        for (int i = 0; i < n; ++i)
            if (! info (l.spk[i]).lfe && ! info (l.spk[i]).phones)
                gains[i] = std::sqrt ((1.0f - spread) * gains[i] * gains[i] + spread / (float) speakers);
}

/** Where a mono or stereo source is placed in a surround bus (a send's panner, saved with the send). */
struct PanPos
{
    float x = 0.0f, y = 1.0f, z = 0.0f;     // see pointGains (front centre, ear level)
    float width = 1.0f;                      // stereo source: its two sides at x - width and x + width (1 = full left and full right)
    float spread = 0.0f, centre = 1.0f;      // see pointGains
    bool followPan = true;                   // left-right taken from the channel's own pan (a mono strip's pan; a stereo strip's two pans)
};

/** The x of each side of a source (one or two channels). panMono / panL / panR: the channel's own pan (-1 .. +1), used when followPan is on. */
inline void sourceXs (const PanPos& p, int srcChannels, float panMono, float panL, float panR, float* xs) noexcept
{
    if (srcChannels <= 1) { xs[0] = p.followPan ? panMono : p.x; return; }
    if (p.followPan) { xs[0] = panL; xs[1] = panR; return; }
    xs[0] = juce::jlimit (-1.0f, 1.0f, p.x - p.width);
    xs[1] = juce::jlimit (-1.0f, 1.0f, p.x + p.width);
}

/** The gains from a mono or stereo source into format f: m[c * channels (f) + j] for source channel c (0, or 0 and 1) and bus channel j. */
inline void sourceMatrix (BusFormat f, int srcChannels, const float* xs, const PanPos& p, float* m) noexcept
{
    const int n = channels (f);
    for (int c = 0; c < juce::jlimit (1, 2, srcChannels); ++c) pointGains (f, xs[c], p.y, p.z, p.spread, p.centre, m + c * n);
}

// ------------------------------------------------------------------ one format into another
namespace detail
{
    constexpr float k3dB = 0.70710678f;

    /** Adds g times speaker s into 'row' (one gain per channel of 'to'), following the fold-downs for a speaker that 'to' has not got. */
    inline void foldInto (Spk s, float g, const Layout& to, float* row, int depth) noexcept
    {
        if (depth > 8 || g == 0.0f) return;
        if (const int j = to.indexOf (s); j >= 0) { row[j] += g; return; }
        const bool top = to.hasTop();
        auto into = [&] (Spk t, float k) { foldInto (t, g * k, to, row, depth + 1); };
        switch (s)
        {
            case Spk::LFE: case Spk::HpL: case Spk::HpR: return;                     // no LFE (as Dolby's Lo/Ro), no headphones: left out
            case Spk::C:   into (Spk::L, k3dB); into (Spk::R, k3dB); return;           // Lo/Ro: centre at -3 dB into both sides
            case Spk::Lss: case Spk::Lrs: into (Spk::Ls, 1.0f); return;                // 7.1 -> 5.1 "standard": Ls = Lss + Lrs
            case Spk::Rss: case Spk::Rrs: into (Spk::Rs, 1.0f); return;
            case Spk::Ls:  if (to.has (Spk::Lss) && to.has (Spk::Lrs)) { into (Spk::Lss, k3dB); into (Spk::Lrs, k3dB); } else into (Spk::L, k3dB); return;   // 5.1 -> 7.1 (as the Mastering Console's up-mix); Lo/Ro
            case Spk::Rs:  if (to.has (Spk::Rss) && to.has (Spk::Rrs)) { into (Spk::Rss, k3dB); into (Spk::Rrs, k3dB); } else into (Spk::R, k3dB); return;
            case Spk::Lw:  if (to.has (Spk::Lss)) { into (Spk::L, k3dB); into (Spk::Lss, k3dB); } else if (to.has (Spk::Ls)) { into (Spk::L, k3dB); into (Spk::Ls, k3dB); } else into (Spk::L, 1.0f); return;
            case Spk::Rw:  if (to.has (Spk::Rss)) { into (Spk::R, k3dB); into (Spk::Rss, k3dB); } else if (to.has (Spk::Rs)) { into (Spk::R, k3dB); into (Spk::Rs, k3dB); } else into (Spk::R, 1.0f); return;
            case Spk::Ltf: if (top) into (Spk::Ltm, 1.0f); else into (Spk::L, k3dB); return;
            case Spk::Rtf: if (top) into (Spk::Rtm, 1.0f); else into (Spk::R, k3dB); return;
            case Spk::Ltr: if (top) into (Spk::Ltm, 1.0f); else into (Spk::Lrs, k3dB); return;
            case Spk::Rtr: if (top) into (Spk::Rtm, 1.0f); else into (Spk::Rrs, k3dB); return;
            case Spk::Ltm: if (top) { into (Spk::Ltf, k3dB); into (Spk::Ltr, k3dB); } else into (Spk::Lss, k3dB); return;
            case Spk::Rtm: if (top) { into (Spk::Rtf, k3dB); into (Spk::Rtr, k3dB); } else into (Spk::Rss, k3dB); return;
            case Spk::L: case Spk::R: case Spk::count: return;                        // (every format has L and R)
        }
    }
}

/** How format 'from' reaches format 'to': m[i * channels (to) + j] = the gain of 'from' channel i in 'to' channel j. A speaker both have goes
    straight across; one 'to' lacks is folded as Dolby's downmixes do (Lo/Ro to stereo: centre and surrounds at -3 dB, LFE left out; 7.1 to 5.1:
    side + rear; tops into the ear-level speakers below them at -3 dB, or into the top middles of 7.1.2). Allocates nothing. */
inline void foldMatrix (BusFormat from, BusFormat to, float* m) noexcept
{
    const auto& a = layout (from);
    const auto& b = layout (to);
    for (int i = 0; i < a.count * b.count; ++i) m[i] = 0.0f;
    for (int i = 0; i < a.count; ++i) detail::foldInto (a.spk[i], 1.0f, b, m + i * b.count, 0);
}

// ------------------------------------------------------------------ plug-ins
/** JUCE's channel type for a speaker (a plug-in's buses are described with these). */
inline juce::AudioChannelSet::ChannelType juceType (Spk s) noexcept
{
    using T = juce::AudioChannelSet;
    switch (s)
    {
        case Spk::L:   return T::left;              case Spk::R:   return T::right;
        case Spk::C:   return T::centre;            case Spk::LFE: return T::LFE;
        case Spk::Ls:  return T::leftSurround;      case Spk::Rs:  return T::rightSurround;
        case Spk::Lss: return T::leftSurroundSide;  case Spk::Rss: return T::rightSurroundSide;
        case Spk::Lrs: return T::leftSurroundRear;  case Spk::Rrs: return T::rightSurroundRear;
        case Spk::Lw:  return T::wideLeft;          case Spk::Rw:  return T::wideRight;
        case Spk::Ltf: return T::topFrontLeft;      case Spk::Rtf: return T::topFrontRight;
        case Spk::Ltm: return T::topSideLeft;       case Spk::Rtm: return T::topSideRight;
        case Spk::Ltr: return T::topRearLeft;       case Spk::Rtr: return T::topRearRight;
        case Spk::HpL: case Spk::HpR: case Spk::count: break;
    }
    return T::unknown;
}

/** The plug-in channel set of a format (JUCE's 5.1, 7.1, 7.1.2, 5.1.4, 7.1.4, 9.1.4 and 9.1.6 Atmos sets, which JUCE gives a VST3 plug-in as the
    matching VST3 speaker arrangement). Empty for the two monitor formats: a headphone pair is no speaker a VST3 layout can name - see
    pluginChannelSet for what a plug-in on such a bus is given. */
inline juce::AudioChannelSet channelSet (BusFormat f)
{
    if (isMonitorFormat (f)) return {};
    const auto& l = layout (f);
    juce::Array<juce::AudioChannelSet::ChannelType> types;
    for (int i = 0; i < l.count; ++i) types.add (juceType (l.spk[i]));
    return juce::AudioChannelSet::channelSetWithChannels (types);
}

/** The speakers-only format of a monitor format (9.1.6 of "9.1.6 + headphones"); f itself for any other. */
inline BusFormat speakersOf (BusFormat f) noexcept
{
    return f == BusFormat::S916Hp ? BusFormat::S916 : f == BusFormat::S714Hp ? BusFormat::S714 : f;
}

/** The channel set a plug-in on a bus of format f is asked to run in (3.39.0, Part 2). The seven surround formats: channelSet (f), which JUCE hands a
    VST3 plug-in as the standard speaker arrangement, so a Dolby Atmos Beam or a 9.1.6 reverb recognises the bus. A monitor format (the Dolby Atmos
    Composer's bus: its speakers, then a headphone pair): no VST3 arrangement has a headphone pair, so the speakers' set plus two more speakers (Lc and
    Rc, the front left / right of centre) stands in for it - 18 channels the Composer will not know as a layout, which its manual says it then plays
    "in the channel order shown below its meters": its own order, which is fermata's. So on such a bus the channels are handed over BY POSITION (see
    pluginIndex), not by speaker. */
inline juce::AudioChannelSet pluginChannelSet (BusFormat f)
{
    if (! isMonitorFormat (f)) return channelSet (f);
    auto set = channelSet (speakersOf (f));
    set.addChannel (juce::AudioChannelSet::leftCentre);
    set.addChannel (juce::AudioChannelSet::rightCentre);
    return set;
}

/** Where a channel type sits in a VST3 speaker arrangement: the bit of its speaker (Steinberg's vsttypes.h: kSpeakerL = bit 0, R 1, C 2, Lfe 3,
    Ls 4, Rs 5, Lc 6, Rc 7, Cs 8, Sl 9, Sr 10, Tc 11, Tfl 12, Tfc 13, Tfr 14, Trl 15, Trc 16, Trr 17, Lfe2 18, M 19, ACN0-15 20-35, Tsl 36, Tsr 37,
    Lcs 38, Rcs 39, ... Lw 50, Rw 51). A plug-in handed an arrangement sees its channels in rising bit order; JUCE's host maps its own order onto
    that. -1 for a type no VST3 speaker stands for. */
inline int vst3Rank (juce::AudioChannelSet::ChannelType t) noexcept
{
    using T = juce::AudioChannelSet;
    switch (t)
    {
        case T::left: return 0;               case T::right: return 1;              case T::centre: return 2;           case T::LFE: return 3;
        case T::leftSurround: return 4;       case T::rightSurround: return 5;      case T::leftCentre: return 6;       case T::rightCentre: return 7;
        case T::centreSurround: return 8;     case T::leftSurroundSide: return 9;   case T::rightSurroundSide: return 10; case T::topMiddle: return 11;
        case T::topFrontLeft: return 12;      case T::topFrontCentre: return 13;    case T::topFrontRight: return 14;   case T::topRearLeft: return 15;
        case T::topRearCentre: return 16;     case T::topRearRight: return 17;      case T::LFE2: return 18;
        case T::topSideLeft: return 36;       case T::topSideRight: return 37;      case T::leftSurroundRear: return 38; case T::rightSurroundRear: return 39;
        case T::wideLeft: return 50;          case T::wideRight: return 51;
        default: return -1;
    }
}

/** Where fermata's channel c of format f sits in the buffer a JUCE-hosted plug-in is handed (JUCE orders a channel set by its channel types, not
    as fermata does): -1 if it has no place there (the headphone pair of a monitor format: see pluginIndex for those). */
inline int juceIndex (BusFormat f, int c)
{
    const auto& l = layout (f);
    if (! juce::isPositiveAndBelow (c, l.count)) return -1;
    const auto t = juceType (l.spk[c]);
    if (t == juce::AudioChannelSet::unknown) return -1;
    return channelSet (f).getChannelIndexForType (t);
}

/** Where fermata's channel c of a bus of format f goes in the buffer a plug-in running in pluginChannelSet (f) is handed (and where its output for
    that channel comes back). A surround format: by speaker (juceIndex). A monitor format: by position - fermata's channel c is the c-th channel of
    the plug-in's arrangement in VST3 order (see pluginChannelSet for why). Message thread (it sorts); the result is kept per slot. */
inline int pluginIndex (BusFormat f, int c)
{
    if (! isMonitorFormat (f)) return juceIndex (f, c);
    const auto set = pluginChannelSet (f);
    auto types = set.getChannelTypes();
    std::sort (types.begin(), types.end(), [] (auto a, auto b) { return vst3Rank (a) < vst3Rank (b); });
    if (! juce::isPositiveAndBelow (c, types.size())) return -1;
    return set.getChannelIndexForType (types[c]);
}
} // namespace surround
} // namespace td
