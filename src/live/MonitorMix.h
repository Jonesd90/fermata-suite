#pragma once
/** fermata:Live 0.3.0: the live monitoring in the recorder - what the Primary hears straight from the inputs, and the talkback.

    The Primary's window turns its mixer (the project's processing mixer: levels, pans, mutes, solos, the output pair) into a list of routes, one per
    input channel and output it reaches, each with its gain, and sends it to the recorder ("mix"). The talkback is a second set of routes (the
    inputs marked TALK to the talkback outputs) that only sounds while the talkback is open ("talkback").

    The audio thread applies the routes to the block after it went into the recording (the recorder's own callback: the inputs are only read here),
    so nothing in the monitoring can touch what is recorded. It never waits and never allocates: a new list is handed over through an atomic
    pointer, and every gain that changes is ramped across the block it changes in (no clicks). The message thread publishes a new list only once
    the audio thread has taken the last one (setRoutes may be called as often as the faders move: only the newest wanted state is kept), and frees
    old lists only once the audio thread has certainly left them.

    Threads: setRoutes, setTalkOpen and service are the message thread's; process is the audio thread's. */
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace td::live
{
class MonitorMix
{
public:
    /** One route: input channel 'in' (0-based driver input) to driver output 'out', at a gain. group 0 = the monitoring, 1 = the talkback. */
    struct Route { int in = 0, out = 0; float from = 0.0f, to = 0.0f; int group = 0; };
    using Key = std::pair<int, std::pair<int, int>>;                  // (group, (in, out))

    MonitorMix() = default;
    ~MonitorMix() { for (auto* l : owned) delete l; }

    /** Message thread: what should be heard from now on - (group, (in, out)) -> gain (linear). Taken over at the next service(). */
    void setRoutes (std::map<Key, float> wantedGains)
    {
        wanted = std::move (wantedGains);
        wantedChanged = true;
        service();
    }
    /** Message thread: the talkback open (the talkback routes sound) or closed. Takes effect within one block, ramped. */
    void setTalkOpen (bool open) noexcept      { talkOpen.store (open, std::memory_order_release); }
    bool isTalkOpen() const noexcept           { return talkOpen.load (std::memory_order_acquire); }
    /** Message thread: everything off at once (the device closed, or the window says so). */
    void clear()                               { setRoutes ({}); }

    /** Message thread, from a timer: publishes the wanted routes once the audio thread has taken the last list, and frees lists it has left. */
    void service()
    {
        const int acked = ack.load (std::memory_order_acquire);
        // a list older than the one the audio thread has taken is no longer used by it (it only ever moves on to newer ones)
        for (auto it = owned.begin(); it != owned.end();)
        {
            if ((*it)->gen < acked) { delete *it; it = owned.erase (it); }
            else ++it;
        }
        if (! wantedChanged) return;
        if (acked != published && audioRunning.load (std::memory_order_acquire)) return;       // (the last one is not taken yet: try again at the next tick)
        // the union of what is sounding now (the last published targets) and what is wanted: everything ramps from where it is to where it should be
        auto* l = new List();
        l->gen = published + 1;
        std::map<Key, std::pair<float, float>> both;                     // key -> (from, to)
        for (auto& [k, g] : currentTargets) both[k].first = g;
        for (auto& [k, g] : wanted) both[k].second = g;
        for (auto& [k, ft] : both)
            if (ft.first != 0.0f || ft.second != 0.0f)
                l->routes.push_back ({ k.second.first, k.second.second, ft.first, ft.second, k.first });
        currentTargets.clear();
        for (auto& [k, g] : wanted) if (g != 0.0f) currentTargets[k] = g;
        owned.push_back (l);
        published = l->gen;
        wantedChanged = false;
        if (! audioRunning.load (std::memory_order_acquire)) ack.store (published, std::memory_order_release);   // (no audio thread to take it: it is current at once)
        pending.store (l, std::memory_order_release);
    }

    /** The audio device started or stopped (audioDeviceAboutToStart / Stopped): while it is stopped the lists are taken over at once. */
    void setAudioRunning (bool running) noexcept { audioRunning.store (running, std::memory_order_release); }

    /** Audio thread: adds the routes to the outputs (which the caller has cleared or filled before). Never waits. (MonitorMix.cpp) */
    void process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int n) noexcept;

    /** Tests: the number of routes in the list the audio thread uses now. */
    size_t routesInUse() const noexcept        { return current != nullptr ? current->routes.size() : 0; }

private:
    struct List { std::vector<Route> routes; int gen = 0; };
    std::atomic<List*> pending { nullptr };
    List* current = nullptr;                                             // audio thread only
    int currentGen = 0;                                                  // audio thread only (generations, not pointers, tell lists apart: an address can be used again)
    float talkNow = 0.0f;                                                // audio thread only
    std::atomic<int> ack { 0 };
    std::atomic<bool> talkOpen { false }, audioRunning { false };
    // message thread only
    std::vector<List*> owned;
    std::map<Key, float> wanted, currentTargets;
    bool wantedChanged = false;
    int published = 0;
    JUCE_DECLARE_NON_COPYABLE (MonitorMix)
};
} // namespace td::live
