#pragma once
#include "Common.h"

namespace td
{
/** What the plug-ins are told about the transport (VST3 ProcessContext): playing or not, recording or not, and where on the timeline the block
    starts. Plug-ins that "listen" while the host plays (Auto-Align 2, analysers ...) need it; most others ignore it.
    The engine fills one per block before running the mixers, and points the running thread at it while a mixer is processed
    (currentHostTransport), so the plug-ins' play head can read it on whichever thread that mixer runs. Nothing here allocates or locks. */
struct HostTransport
{
    bool playing = false, recording = false;
    juce::int64 samplePos = 0;           // timeline position of the block's first sample
    double sampleRate = 48000.0;
};

inline const HostTransport*& currentHostTransport() noexcept
{
    thread_local const HostTransport* t = nullptr;
    return t;
}

/** 3.39.0: how late (samples) the sound going into the insert slot being run right now is, counted from the timeline, because of plug-ins before it
    (delay compensation; InsertSlot::posLat). The slot sets it just before it runs its plug-in; the plug-in's play head takes it off the transport's
    position, so a plug-in behind a reverb with latency is told the position of the sound it is actually processing (the Dolby Atmos Composer lines
    its Beams up by that). The spot-mic alignment is not in it: that delay is meant to be heard. */
inline int& currentSlotLatency() noexcept
{
    thread_local int lat = 0;
    return lat;
}
} // namespace td
