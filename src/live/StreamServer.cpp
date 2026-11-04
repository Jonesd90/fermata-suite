#include "StreamServer.h"
#include "../core/CrashReport.h"
#include <csignal>

namespace td::live
{
/** One station's connection: its own thread reads the station's packets into its slot and sends it credits. Only this thread touches the socket. */
class StreamServer::Connection final : public juce::Thread
{
public:
    Connection (StreamServer& s, Slot& sl, std::unique_ptr<juce::StreamingSocket> so)
        : juce::Thread ("fermata:Live stream"), server (s), slot (sl), sock (std::move (so)) { startThread (juce::Thread::Priority::high); }
    ~Connection() override { signalThreadShouldExit(); stopThread (4000); sock->close(); }
    bool isFinished() const noexcept { return finished.load(); }

private:
    void run() override
    {
        crashreport::nameThisThread ("fermata:Live stream");
        char probe = 0;
        sock->read (&probe, 0, false);                           // (non-blocking from here on: no read or write can hold this thread)
        std::string buf;
        std::vector<char> tmp ((size_t) 65536);
        juce::uint32 gen = 0;
        juce::int64 lastCreditMs = 0;
        int lastFree = -1;
        juce::String why ("the station closed it");
        while (! threadShouldExit())
        {
            const int ready = sock->waitUntilReady (true, 4);
            if (ready < 0) { why = "network error"; break; }
            if (ready > 0)
            {
                const int n = sock->read (tmp.data(), (int) tmp.size(), false);
                if (n <= 0) break;
                buf.append (tmp.data(), (size_t) n);
                const bool ok = stream::unpack (buf, [&] (juce::uint32 type, const char* p, size_t bytes)
                {
                    if (type == stream::kHello && bytes >= sizeof (stream::Hello))
                    {
                        stream::Hello h; std::memcpy (&h, p, sizeof h);
                        h.name[sizeof (h.name) - 1] = 0;
                        { const juce::ScopedLock sl (server.slotLock); slot.name = juce::String::fromUTF8 (h.name); }
                        setState (h.outL, h.outR, h.generation, h.playing != 0, gen);
                        if (server.log) server.log ("Playback stream: " + juce::String::fromUTF8 (h.name) + " connected (outputs " + juce::String (h.outL + 1) + "-" + juce::String (h.outR + 1) + ")");
                    }
                    else if (type == stream::kState && bytes >= sizeof (stream::State))
                    {
                        stream::State st; std::memcpy (&st, p, sizeof st);
                        setState (st.outL, st.outR, st.generation, st.playing != 0, gen);
                    }
                    else if (type == stream::kAudio && bytes >= sizeof (stream::AudioHead))
                    {
                        stream::AudioHead ah; std::memcpy (&ah, p, sizeof ah);
                        if (ah.generation != gen) return;                // (queued before a Stop or a locate: dropped)
                        const size_t frames = juce::jmin ((size_t) ah.frames, (bytes - sizeof ah) / (2 * sizeof (float)));
                        write (reinterpret_cast<const float*> (p + sizeof ah), frames);
                    }
                });
                if (! ok) { why = "it sent something that is not a playback stream"; break; }
            }
            // credits: every 5 ms, and at once when the room has grown by a good piece
            const auto now = juce::Time::currentTimeMillis();
            const int window = stream::windowFor (server.rate.load(), server.blockFrames.load());
            const juce::uint64 fill = slot.w.load (std::memory_order_acquire) - slot.r.load (std::memory_order_acquire);
            const int freeFrames = (int) juce::jmax<juce::int64> (0, (juce::int64) window - (juce::int64) fill);
            if (now - lastCreditMs >= 5 || freeFrames >= lastFree + 512)
            {
                const int w = sock->waitUntilReady (false, 0);
                if (w < 0) { why = "network error"; break; }
                if (w > 0)
                {
                    stream::Credit c;
                    c.freeFrames = (juce::uint32) freeFrames; c.window = (juce::uint32) window;
                    c.played = slot.played.load (std::memory_order_acquire); c.underruns = slot.underruns.load (std::memory_order_relaxed);
                    c.generation = slot.audioGen.load (std::memory_order_acquire); c.sampleRate = server.rate.load();
                    juce::MemoryBlock mb; stream::pack (mb, stream::kCredit, &c, sizeof c);
                    if (! stream::writeAll (*sock, mb, 200)) { why = "network error, or the station stopped reading"; break; }
                    lastCreditMs = now; lastFree = freeFrames;
                }
            }
        }
        slot.playing = false;
        slot.active.store (false, std::memory_order_release);
        // the audio thread may be in the slot at this moment: let two blocks pass (or a moment, when the device is not running) before it can be used again
        const auto b = server.blocks.load (std::memory_order_acquire);
        for (int i = 0; i < 40 && server.blocks.load (std::memory_order_acquire) < b + 2; ++i) juce::Thread::sleep (5);
        if (server.log && ! threadShouldExit()) server.log ("Playback stream: " + slot.name + " disconnected (" + why + ")");
        { const juce::ScopedLock sl (server.slotLock); slot.inUse = false; }
        finished = true;
        sock->close();
    }

    void setState (int outL, int outR, juce::uint32 generation, bool playing, juce::uint32& gen)
    {
        slot.outL.store (outL, std::memory_order_release); slot.outR.store (outR, std::memory_order_release);
        if (generation != gen)
        {
            gen = generation;
            slot.pendingGen.store (generation, std::memory_order_release);
            slot.flushTo.store (slot.w.load (std::memory_order_relaxed), std::memory_order_release);
            slot.flushSeq.fetch_add (1, std::memory_order_acq_rel);     // (the audio thread drops everything before flushTo, keeps what comes after)
        }
        slot.playing.store (playing, std::memory_order_release);
        slot.active.store (true, std::memory_order_release);
    }

    void write (const float* interleaved, size_t frames)
    {
        const juce::uint64 w = slot.w.load (std::memory_order_relaxed), r = slot.r.load (std::memory_order_acquire);
        const size_t room = (size_t) juce::jmax<juce::int64> (0, (juce::int64) stream::kRingFrames - (juce::int64) (w - r));
        const size_t n = juce::jmin (frames, room);
        constexpr juce::uint64 mask = (juce::uint64) stream::kRingFrames - 1;
        for (size_t i = 0; i < n; ++i)
        {
            const size_t at = (size_t) ((w + i) & mask) * 2;
            slot.data[at] = interleaved[2 * i]; slot.data[at + 1] = interleaved[2 * i + 1];
        }
        slot.w.store (w + n, std::memory_order_release);
    }

    StreamServer& server;
    Slot& slot;
    std::unique_ptr<juce::StreamingSocket> sock;
    std::atomic<bool> finished { false };
};

StreamServer::StreamServer() : juce::Thread ("fermata:Live stream port")
{
    for (auto& s : slots) s.data.assign ((size_t) stream::kRingFrames * 2, 0.0f);
}

StreamServer::~StreamServer() { stop(); }

bool StreamServer::start (int port)
{
    stop();
   #if ! JUCE_WINDOWS
    std::signal (SIGPIPE, SIG_IGN);
   #endif
    if (port <= 0 || port > 65535 || ! listener.createListener (port, "127.0.0.1")) return false;
    listening = true;
    startThread();
    return true;
}

void StreamServer::stop()
{
    stopThread (4000);
    if (listening) { listener.close(); listening = false; }
    std::vector<std::unique_ptr<Connection>> gone;
    { const juce::ScopedLock sl (slotLock); gone.swap (connections); }
    gone.clear();                                                // (outside the lock: each waits for its own thread, which takes the lock at its end)
    for (auto& s : slots) { s.active = false; s.playing = false; }
}

StreamServer::Slot* StreamServer::claimSlot()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& s : slots)
        if (! s.inUse && ! s.active.load())
        {
            s.inUse = true;
            s.w.store (0); s.r.store (0); s.flushTo.store (0); s.played.store (0); s.underruns.store (0);
            s.outL.store (-1); s.outR.store (-1); s.playing.store (false); s.name = "station";
            return &s;
        }
    return nullptr;
}

void StreamServer::run()
{
    crashreport::nameThisThread ("fermata:Live stream port");
    while (! threadShouldExit())
    {
        {   // the connections that have ended go (outside the lock: their threads have finished)
            std::vector<std::unique_ptr<Connection>> gone;
            {
                const juce::ScopedLock sl (slotLock);
                for (auto it = connections.begin(); it != connections.end();)
                    if ((*it)->isFinished()) { gone.push_back (std::move (*it)); it = connections.erase (it); } else ++it;
            }
        }
        const int r = listener.waitUntilReady (true, 200);
        if (r < 0) { wait (200); continue; }
        if (r == 0) continue;
        std::unique_ptr<juce::StreamingSocket> s (listener.waitForNextConnection());
        if (s == nullptr) continue;
        auto* slot = claimSlot();
        if (slot == nullptr) { if (log) log ("Playback stream refused: " + juce::String (stream::kSlots) + " stations are already playing"); s->close(); continue; }
        auto c = std::make_unique<Connection> (*this, *slot, std::move (s));
        const juce::ScopedLock sl (slotLock);
        connections.push_back (std::move (c));
    }
}

void StreamServer::mix (float* const* outputs, int numOutputs, int n) noexcept
{
    constexpr juce::uint64 mask = (juce::uint64) stream::kRingFrames - 1;
    for (auto& s : slots)
    {
        if (! s.active.load (std::memory_order_acquire)) continue;
        const auto seq = s.flushSeq.load (std::memory_order_acquire);
        juce::uint64 r = s.r.load (std::memory_order_relaxed);
        if (seq != s.flushSeen)
        {
            s.flushSeen = seq;
            r = juce::jmax (r, s.flushTo.load (std::memory_order_acquire));
            s.played.store (0, std::memory_order_release);
            s.audioGen.store (s.pendingGen.load (std::memory_order_acquire), std::memory_order_release);
        }
        const juce::uint64 w = s.w.load (std::memory_order_acquire);
        const int take = (int) juce::jmin<juce::uint64> ((juce::uint64) n, w > r ? w - r : 0);
        const int L = s.outL.load (std::memory_order_relaxed), R = s.outR.load (std::memory_order_relaxed);
        float* dl = juce::isPositiveAndBelow (L, numOutputs) ? outputs[L] : nullptr;
        float* dr = juce::isPositiveAndBelow (R, numOutputs) ? outputs[R] : nullptr;
        if (dl == dr) dr = nullptr;                                   // (one output: the left and right summed into it at half each)
        const float* data = s.data.data();
        for (int i = 0; i < take; ++i)
        {
            const size_t at = (size_t) ((r + (juce::uint64) i) & mask) * 2;
            const float l = data[at], rr = data[at + 1];
            if (dr != nullptr) { if (dl != nullptr) dl[i] += l; dr[i] += rr; }
            else if (dl != nullptr) dl[i] += 0.5f * (l + rr);
        }
        s.r.store (r + (juce::uint64) take, std::memory_order_release);
        s.played.fetch_add ((juce::uint64) take, std::memory_order_acq_rel);
        if (take < n && s.playing.load (std::memory_order_relaxed)) s.underruns.fetch_add (1, std::memory_order_relaxed);
    }
}

std::vector<StreamServer::SlotInfo> StreamServer::info() const
{
    std::vector<SlotInfo> out;
    const juce::ScopedLock sl (slotLock);
    for (auto& s : slots)
    {
        SlotInfo i;
        i.connected = s.inUse; i.name = s.name; i.outL = s.outL.load(); i.outR = s.outR.load(); i.playing = s.playing.load();
        i.underruns = s.underruns.load(); i.fill = (int) (s.w.load() - s.r.load());
        out.push_back (i);
    }
    return out;
}
} // namespace td::live
