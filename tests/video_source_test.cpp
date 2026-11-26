// 3.68.0: tests src/ui/VideoSource.cpp - the part of fermata's video that is the same on every system (the decoder's thread, the frames kept
// ready, the picture that never waits, the small pictures, the sound written out) - over a made-up decoder (VideoReader.h) whose every frame
// says which frame it is. Built and run by tools/video-check.sh (plain, and under the thread checker). Nothing of Windows is needed.
#include "../src/ui/VideoSource.h"
#include "../src/ui/VideoReader.h"
#include "../src/core/AudioFormats.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

static int failures = 0;
// (under the thread or address checker everything is many times slower: the test waits longer, and "at once" is a longer once)
#if defined (__SANITIZE_THREAD__) || defined (__SANITIZE_ADDRESS__)
static const double kPatience = 15000.0, kNeverWaitsMs = 600.0;
#else
static const double kPatience = 3000.0, kNeverWaitsMs = 40.0;
#endif
#define CHECK(c) do { if (! (c)) { ++failures; std::printf ("  FAIL line %d: %s\n", __LINE__, #c); } } while (false)
#define SECTION(n) std::printf ("-- %s\n", n)

// ------------------------------------------------------------------------------------------------ the made-up decoder
namespace fake
{
constexpr double kFps = 25.0, kFirst = 0.08;              // (its first frame is not at time 0: the Source takes that time as the start)
constexpr int kFrames = 1500, kKey = 12;                  // a minute; every 12th frame a key frame
constexpr int kW = 1920, kH = 1080;
std::atomic<int> decodeMs { 1 };                          // how long one frame takes to decode
std::atomic<int> opens { 0 }, seeks { 0 }, decodes { 0 }, copies { 0 }, alive { 0 };
std::atomic<int> hardwareFailsAfter { 0 };                // > 0: a reader on the "graphics card" fails after this many frames
std::atomic<bool> noSuchFile { false };
std::atomic<int> soundCase { 0 };                         // 0: starts 10 ms after the first frame, with a 100 ms hole at 1 s; 1: starts 5 ms before it; 2: none

struct Picture : td::video::raw::PictureReader
{
    Picture (const td::video::raw::Options& o) : hardware (o.hardware)
    {
        double sc = 1.0;
        if (o.maxWidth > 0) sc = std::min (sc, (double) o.maxWidth / kW);
        if (o.maxHeight > 0) sc = std::min (sc, (double) o.maxHeight / kH);
        w = std::max (16, ((int) std::lround (kW * sc)) & ~1); h = std::max (16, ((int) std::lround (kH * sc)) & ~1);
        ++opens; ++alive;
    }
    ~Picture() override { --alive; }
    td::video::raw::Format format() const override
    {
        td::video::raw::Format f; f.seconds = kFrames / kFps; f.fps = kFps; f.width = kW; f.height = kH; f.codec = "made up"; f.soundChannels = soundCase.load() == 2 ? 0 : 2; f.soundRate = 48000.0;
        f.hardware = hardware; f.how = hardware ? "made up, on the graphics card" : "made up";
        return f;
    }
    bool next (td::video::raw::Frame& f, bool& ended, std::string& why) override
    {
        if (hardware && hardwareFailsAfter.load() > 0 && ++given > hardwareFailsAfter.load()) { why = "the made-up graphics card gave up"; return false; }
        if (index >= kFrames) { ended = true; return false; }
        if (const int ms = decodeMs.load(); ms > 0) std::this_thread::sleep_for (std::chrono::milliseconds (ms));
        held = index++; ++decodes;
        f.time = kFirst + held / kFps; f.length = 1.0 / kFps; f.width = w; f.height = h;
        return true;
    }
    bool copyFrame (std::uint8_t* rows, int stride, int cw, int ch) override
    {
        ++copies;
        for (int y = 0; y < std::min (ch, h); ++y)
        {
            auto* p = rows + (size_t) y * (size_t) stride;
            for (int x = 0; x < std::min (cw, w); ++x) { p[4 * x] = (std::uint8_t) (held & 255); p[4 * x + 1] = (std::uint8_t) ((held >> 8) & 255); p[4 * x + 2] = 77; p[4 * x + 3] = 255; }
        }
        return true;
    }
    bool seek (double t) override
    {
        ++seeks;
        const int want = (int) std::floor ((t - kFirst) * kFps + 1.0e-6);
        index = std::max (0, std::min (kFrames, (want / kKey) * kKey));
        return true;
    }
    bool hardware; int w = 0, h = 0, index = 0, held = 0, given = 0;
};

/** The sound: sample n (of the sound's own count) has the value n / 100000 on the left and its negative on the right. */
struct Sound : td::video::raw::SoundReader
{
    td::video::raw::SoundFormat format() const override { return { 2, 48000.0, 2.0 }; }
    bool next (double& time, std::vector<float>& out, bool& ended, std::string&) override
    {
        if (n >= 96000) { ended = true; return false; }
        const int k = 4000;
        const double start = soundCase.load() == 0 ? kFirst + 0.010 : kFirst - 0.005;
        time = start + n / 48000.0 + ((soundCase.load() == 0 && n >= 48000) ? 0.100 : 0.0);      // (case 0: 100 ms of nothing after its first second)
        out.resize ((size_t) k * 2);
        for (int i = 0; i < k; ++i) { out[(size_t) i * 2] = (float) (n + i) / 100000.0f; out[(size_t) i * 2 + 1] = -(float) (n + i) / 100000.0f; }
        n += k;
        return true;
    }
    int n = 0;
};
} // namespace fake

namespace td::video::raw
{
std::unique_ptr<PictureReader> openPicture (const std::string&, const Options& o, std::string& why)
{
    if (fake::noSuchFile.load()) { why = "the made-up decoder cannot read it"; return nullptr; }
    return std::make_unique<fake::Picture> (o);
}
std::unique_ptr<SoundReader> openSound (const std::string&, std::string& why)
{
    if (fake::soundCase.load() == 2) { why = "it has no sound"; return nullptr; }
    return std::make_unique<fake::Sound>();
}
const char* systemName() { return "made up (the test's)"; }
static std::atomic<int> uses { 0 };
ThreadUse::ThreadUse() { mine = true; ++uses; }
ThreadUse::~ThreadUse() { if (mine) --uses; }
}

// ------------------------------------------------------------------------------------------------ helpers
static int frameOf (const juce::Image& im)
{
    if (! im.isValid()) return -1;
    const auto c = im.getPixelAt (im.getWidth() / 2, im.getHeight() / 2);
    return c.getRed() == 77 ? (int) c.getBlue() + ((int) c.getGreen() << 8) : -2;
}
static void sleepMs (int ms) { std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
/** Asks for the frame at t (standing) every few ms until it is the frame given, or 'ms' have gone. The frame's number, or what there was. */
static int standAt (td::video::Source& s, double t, int ms, juce::Image* out = nullptr)
{
    const int want = std::max (0, std::min (fake::kFrames - 1, (int) std::floor (t * fake::kFps + 1.0e-6)));
    int got = -1; double ft = 0.0;
    const double until = nowMs() + ms;
    for (;;)
    {
        const auto im = s.frameAt (t, false, ft);
        got = frameOf (im);
        if (out != nullptr) *out = im;
        if (got == want || nowMs() > until) return got;
        sleepMs (2);
    }
}

int main()
{
    using namespace td::video;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fermata_video_source_test");
    dir.deleteRecursively(); dir.createDirectory();
    const auto file = dir.getChildFile ("made up.mp4");
    file.replaceWithText ("not a real video: the test's decoder makes the frames");

    SECTION ("opened for the picture window: at once, the first frame soon after; a file that is not there says so");
    {
        const double t0 = nowMs();
        auto s = openForPicture (file);
        CHECK (nowMs() - t0 < 50.0);
        juce::String why;
        CHECK (s->state (why) >= 0);
        CHECK (standAt (*s, 0.0, 2000) == 0);
        CHECK (s->state (why) == 1 && why.isEmpty());
        const auto inf = s->info();
        CHECK (inf.width == 1920 && inf.height == 1080 && inf.fps == 25.0 && std::abs (inf.seconds - 60.0) < 1.0e-9 && inf.codec == "made up" && inf.soundChannels == 2 && inf.hardware);
        auto gone = openForPicture (dir.getChildFile ("gone.mp4"));
        double ft = 0.0;
        for (int i = 0; i < 100 && gone->state (why) == 0; ++i) { gone->frameAt (0.0, false, ft); sleepMs (5); }
        CHECK (gone->state (why) == -1 && why.contains ("not there"));
        CHECK (! gone->frameAt (0.0, false, ft).isValid());
        juce::String w2; CHECK (open (dir.getChildFile ("gone.mp4"), w2) == nullptr && w2.contains ("not there"));
    }

    SECTION ("standing: the frame at the playhead - the first frame's time is the start - and frames on the way to it are not copied out");
    {
        auto s = openForPicture (file);
        s->wantSize (960, 540);
        CHECK (standAt (*s, 0.0, 2000) == 0);
        sleepMs (150);
        fake::seeks = 0; fake::copies = 0; fake::decodes = 0;
        juce::Image im; double ft = 0.0;
        CHECK (standAt (*s, 20.0 + 9.5 / 25.0, 2000, &im) == 509);                 // frame 509: the key frame before it is 504
        s->frameAt (20.0 + 9.5 / 25.0, false, ft);
        CHECK (std::abs (ft - 509 / 25.0) < 1.0e-6);
        CHECK (im.getWidth() == 960 && im.getHeight() == 540);
        sleepMs (100);                                                              // (the two frames after it are made ready too)
        CHECK (fake::seeks.load() == 1);
        CHECK (fake::decodes.load() >= 6 && fake::decodes.load() <= 9);            // 504 ... 509, and 510, 511
        CHECK (fake::copies.load() == fake::decodes.load());                        // (all within the few kept before a standing playhead)
        // a step back by a frame, and by three: at once, without the decoder
        fake::seeks = 0; fake::decodes = 0;
        CHECK (frameOf (s->frameAt (20.0 + 8.5 / 25.0, false, ft)) == 508 && std::abs (ft - 508 / 25.0) < 1.0e-6);
        CHECK (frameOf (s->frameAt (20.0 + 5.5 / 25.0, false, ft)) == 505);
        CHECK (frameOf (s->frameAt (20.0 + 10.5 / 25.0, false, ft)) == 510);      // and a step on
        sleepMs (60);
        CHECK (fake::seeks.load() == 0);
        // far from a key frame: the frames between are decoded but not copied, except the few before the playhead
        fake::seeks = 0; fake::copies = 0; fake::decodes = 0;
        fake::decodeMs = 0;
        {
            // (a made-up file with a key frame only every 12 frames cannot show it; so: a long way on from where the decoder stands, less than a second)
            CHECK (standAt (*s, 20.0 + 33.5 / 25.0, 2000) == 533);                 // 22 frames on from 511: reached by decoding on, no seek
            sleepMs (60);
            CHECK (fake::seeks.load() == 0);
            CHECK (fake::copies.load() <= 14);                                     // the 10 before it, itself, 2 after (not all 24)
        }
        fake::decodeMs = 1;
        // a time before the start, and one after the end: the first frame, the last frame
        CHECK (standAt (*s, -3.0, 2000) == 0);
        CHECK (standAt (*s, 59.99, 2000) == 1499);
        CHECK (standAt (*s, 75.0, 2000) == 1499);
        CHECK (standAt (*s, 30.0, 2000) == 750);
    }

    SECTION ("playing: frames ready ahead of the playhead, each in its turn, never one from the future; a jump while playing");
    {
        auto s = openForPicture (file);
        s->wantSize (960, 540);
        CHECK (standAt (*s, 10.0, 2000) == 250);
        sleepMs (100);
        fake::seeks = 0;
        const double begin = nowMs();
        int last = -1, distinct = 0, late = 0, future = 0, ticks = 0;
        for (;;)
        {
            const double t = 10.0 + (nowMs() - begin) / 1000.0;
            if (t > 12.0) break;
            double ft = 0.0;
            const int f = frameOf (s->frameAt (t, true, ft));
            const int want = (int) std::floor (t * 25.0 + 1.0e-6);
            if (f != last) { ++distinct; last = f; }
            if (ft > t + 1.0e-3) ++future;
            if (f < want - 1) ++late;
            ++ticks;
            sleepMs (8);
        }
        CHECK (future == 0);
        CHECK (late <= ticks / 50);                                                 // (a hiccup of the test machine aside: the right frame every time)
        CHECK (distinct >= 45 && distinct <= 52);                                   // 2 s at 25 frames a second
        CHECK (fake::seeks.load() == 0);
        if (late > ticks / 50 || distinct < 45) std::printf ("   playing: %d ticks, %d frames shown, %d late\n", ticks, distinct, late);
        // a jump to another place while playing: the old picture stays until the new one is there (never a frame from in between), then in step again
        const double begin2 = nowMs();
        bool between = false; int settled = -1; double settledAfter = 0.0;
        for (;;)
        {
            const double t = 40.0 + (nowMs() - begin2) / 1000.0;
            if (t > 41.0) break;
            double ft = 0.0;
            const int f = frameOf (s->frameAt (t, true, ft));
            const int want = (int) std::floor (t * 25.0 + 1.0e-6);
            if (f > 320 && f < 990) between = true;
            if (settled < 0 && (f == want || f == want - 1)) { settled = f; settledAfter = nowMs() - begin2; }
            sleepMs (8);
        }
        CHECK (! between && settled >= 1000 && settledAfter < 400.0);
    }

    SECTION ("a computer too slow for the file: the picture still moves, in step, a few frames a second - never ahead, never far behind");
    {
        auto s = openForPicture (file);
        s->wantSize (960, 540);
        CHECK (standAt (*s, 10.0, 2000) == 250);
        fake::decodeMs = 70;                                                        // 70 ms a frame: under 15 frames a second for a 25 fps file
        const double begin = nowMs();
        int last = -1, distinct = 0, future = 0; double worstLag = 0.0;
        for (;;)
        {
            const double t = 10.0 + (nowMs() - begin) / 1000.0;
            if (t > 14.0) break;
            double ft = 0.0;
            const int f = frameOf (s->frameAt (t, true, ft));
            if (f != last) { ++distinct; last = f; }
            if (ft > t + 1.0e-3) ++future;
            if (t > 11.5) worstLag = std::max (worstLag, t - ft);
            sleepMs (8);
        }
        fake::decodeMs = 1;
        CHECK (future == 0 && distinct >= 25);                                      // (it can decode about 14 a second: most of those are shown)
        CHECK (worstLag < 1.7);                                                     // (a second behind at most, then on to the key frame before the playhead)
        std::printf ("   too slow (70 ms a frame): %d frames shown in 4 s, at most %.2f s behind\n", distinct, worstLag);
    }

    SECTION ("the picture drawn bigger: bigger frames, after a moment; switching the graphics card off; a card that gives up");
    {
        auto s = openForPicture (file);
        s->wantSize (400, 225);
        juce::Image im;
        const double until0 = nowMs() + kPatience;
        while (nowMs() < until0) { if (standAt (*s, 5.0, 20, &im) == 125 && im.getHeight() == 270) break; sleepMs (10); }
        CHECK (frameOf (im) == 125 && im.getHeight() == 270);                       // (the step above 225 lines)
        s->wantSize (1920, 1080);
        const double until = nowMs() + kPatience;
        while (nowMs() < until) { if (standAt (*s, 5.0, 20, &im) == 125 && im.getHeight() == 1080) break; sleepMs (10); }
        CHECK (im.getHeight() == 1080 && im.getWidth() == 1920 && frameOf (im) == 125);
        s->wantSize (3840, 2160);                                                   // (never above 1080 lines)
        sleepMs (500); standAt (*s, 5.0, 200, &im);
        CHECK (im.getHeight() == 1080);
        CHECK (s->info().hardware);
        setHardwareDecoding (false);
        const double until2 = nowMs() + kPatience;
        while (nowMs() < until2 && s->info().hardware) { standAt (*s, 5.0, 20); sleepMs (10); }
        CHECK (! s->info().hardware && standAt (*s, 5.0, 1000) == 125);
        setHardwareDecoding (true);
        const double until3 = nowMs() + kPatience;
        while (nowMs() < until3 && ! s->info().hardware) { standAt (*s, 5.0, 20); sleepMs (10); }
        CHECK (s->info().hardware);
        // the card's decoder gives up in the middle: carried on without it, the picture is still right
        fake::hardwareFailsAfter = 30;
        auto s2 = openForPicture (file);
        CHECK (standAt (*s2, 30.0, 3000) == 750);
        CHECK (standAt (*s2, 31.0, 3000) == 775);
        CHECK (standAt (*s2, 33.0, 3000) == 825);
        juce::String why;
        CHECK (s2->state (why) == 1 && ! s2->info().hardware);
        fake::hardwareFailsAfter = 0;
        // a file no decoder can read
        fake::noSuchFile = true;
        auto bad = openForPicture (file);
        double ft = 0.0;
        for (int i = 0; i < 200 && bad->state (why) == 0; ++i) { bad->frameAt (0.0, false, ft); sleepMs (5); }
        CHECK (bad->state (why) == -1 && why.contains ("made-up decoder"));
        juce::String w2; CHECK (open (file, w2) == nullptr && w2.contains ("made-up decoder"));
        fake::noSuchFile = false;
    }

    SECTION ("the small pictures: the key frame at or before each time, no wider than asked, in the picture's shape");
    {
        juce::String why;
        auto s = open (file, why);
        CHECK (s != nullptr && why.isEmpty());
        if (s != nullptr)
        {
            CHECK (s->info().width == 1920 && s->info().soundChannels == 2);
            auto im = s->still (5.0, 160);
            CHECK (im.isValid() && im.getWidth() == 160 && im.getHeight() == 90 && frameOf (im) == 120);        // 5.0 s is frame 125: its key frame is 120
            CHECK (frameOf (s->still (0.0, 160)) == 0 && frameOf (s->still (59.99, 160)) == 1488);
            CHECK (! s->still (61.0, 160).isValid());
            CHECK (frameOf (s->still (12.0, 64)) == 300 && s->still (12.0, 64).getWidth() == 64);
        }
    }

    SECTION ("the sound written out: its sample 0 is the first frame - silence where it starts later, a hole kept as silence, what came before left out");
    {
        juce::AudioFormatManager fm; td::registerAudioFormats (fm);
        juce::String why;
        auto s = open (file, why);
        CHECK (s != nullptr);
        if (s != nullptr)
        {
            // starts 10 ms after the first frame (480 samples of silence first), and has a 100 ms hole after its first second
            fake::soundCase = 0;
            const auto wav = dir.getChildFile ("sound.wav");
            float lastP = -1.0f; bool rising = true;
            CHECK (s->readSound (wav, [&] (float p) { rising = rising && p >= lastP; lastP = p; return true; }, why));
            CHECK (rising && lastP > 0.9f);
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (wav));
            CHECK (r != nullptr && r->numChannels == 2 && r->sampleRate == 48000.0 && r->bitsPerSample == 24 && r->lengthInSamples == 480 + 96000 + 4800);
            if (r != nullptr && r->lengthInSamples == 480 + 96000 + 4800)
            {
                juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                auto at = [&] (int i) { return b.getSample (0, i); };
                CHECK (at (0) == 0.0f && at (479) == 0.0f && std::abs (at (480) - 0.0f) < 1.0e-6f && std::abs (at (481) - 1.0f / 100000.0f) < 1.0e-6f);
                CHECK (std::abs (at (480 + 47999) - 47999.0f / 100000.0f) < 1.0e-6f);
                CHECK (at (480 + 48000) == 0.0f && at (480 + 48000 + 4799) == 0.0f);                              // the hole
                CHECK (std::abs (at (480 + 48000 + 4800) - 48000.0f / 100000.0f) < 1.0e-6f);
                CHECK (std::abs (b.getSample (1, 480 + 1000) + 1000.0f / 100000.0f) < 1.0e-6f);
            }
            r.reset();
            // starts 5 ms before the first frame: those 240 samples are left out
            fake::soundCase = 1;
            CHECK (s->readSound (wav, {}, why));
            r.reset (fm.createReaderFor (wav));
            CHECK (r != nullptr && r->lengthInSamples == 96000 - 240);
            if (r != nullptr) { juce::AudioBuffer<float> b (2, 16); r->read (&b, 0, 16, 0, true, true); CHECK (std::abs (b.getSample (0, 0) - 240.0f / 100000.0f) < 1.0e-6f); }
            r.reset();
            // stopped by the user: no file is left
            CHECK (! s->readSound (wav, [] (float) { return false; }, why) && ! wav.existsAsFile());
            // no sound
            fake::soundCase = 2;
            CHECK (! s->readSound (wav, {}, why) && why.contains ("no sound") && ! wav.existsAsFile());
            fake::soundCase = 0;
        }
    }

    SECTION ("made and thrown away in the middle of everything, many at once: nothing waits, nothing is left behind");
    {
        juce::Random rnd (2026);
        const double begin = nowMs();
        double worstCall = 0.0;
        for (int round = 0; round < 60; ++round)
        {
            std::vector<std::shared_ptr<Source>> some;
            for (int k = 0; k < 3; ++k) some.push_back (openForPicture (file));
            const int steps = rnd.nextInt (40);
            double base = rnd.nextDouble() * 50.0;
            for (int i = 0; i < steps; ++i)
            {
                for (auto& s : some)
                {
                    if (rnd.nextInt (10) == 0) base = rnd.nextDouble() * 58.0;
                    if (rnd.nextInt (15) == 0) s->wantSize (200 + rnd.nextInt (1800), 100 + rnd.nextInt (1000));
                    double ft = 0.0;
                    const double c0 = nowMs();
                    const auto im = s->frameAt (base + i * 0.016, rnd.nextBool(), ft);
                    worstCall = std::max (worstCall, nowMs() - c0);
                    if (im.isValid() && frameOf (im) < 0) { ++failures; std::printf ("  FAIL: a frame that is not one of the file's\n"); }
                }
                if (rnd.nextInt (4) == 0) sleepMs (rnd.nextInt (6));
            }
            const double d0 = nowMs();
            some.clear();                                                           // (while their threads are decoding)
            worstCall = std::max (worstCall, nowMs() - d0);
        }
        CHECK (worstCall < kNeverWaitsMs);                                          // frameAt, and letting go of a Source, never wait for the decoder
        std::printf ("   60 rounds in %.1f s; the longest call took %.2f ms\n", (nowMs() - begin) / 1000.0, worstCall);
        finishDecoders();
        CHECK (fake::alive.load() == 0);
        CHECK (td::video::raw::uses.load() == 0);
    }

    dir.deleteRecursively();
    std::printf (failures == 0 ? "\nALL VIDEO SOURCE TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
