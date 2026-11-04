#include "MonitorMix.h"

namespace td::live
{
// (out of line so that the realtime check can watch it: tools/tool-checks.sh live)
void MonitorMix::process (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int n) noexcept
{
    if (n <= 0) return;
    auto* p = pending.load (std::memory_order_acquire);              // (the newest list is never freed while it is the newest)
    bool ramp = false;
    if (p != nullptr && p->gen != currentGen) { current = p; currentGen = p->gen; ramp = true; ack.store (p->gen, std::memory_order_release); }
    const float talkTarget = talkOpen.load (std::memory_order_acquire) ? 1.0f : 0.0f;
    const float talkFrom = talkNow;
    talkNow = talkTarget;
    if (current == nullptr) return;
    for (auto& r : current->routes)
    {
        if (! juce::isPositiveAndBelow (r.in, numInputs) || ! juce::isPositiveAndBelow (r.out, numOutputs)) continue;
        const float* src = inputs[r.in];
        float* dst = outputs[r.out];
        if (src == nullptr || dst == nullptr) continue;
        float g0 = ramp ? r.from : r.to, g1 = r.to;
        if (r.group == 1) { g0 *= talkFrom; g1 *= talkTarget; }
        if (g0 == 0.0f && g1 == 0.0f) continue;
        if (g0 == g1) juce::FloatVectorOperations::addWithMultiply (dst, src, g1, n);
        else
        {
            const float step = (g1 - g0) / (float) n;
            float g = g0;
            for (int i = 0; i < n; ++i) { dst[i] += src[i] * g; g += step; }
        }
    }
}
} // namespace td::live
