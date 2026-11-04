#pragma once
/** fermata:Live 0.3.0: the Fixer's window - the take window delayed (the timeline fills as the audio reaches the disk), Follow Primary, the
    Primary's flags at once with a reply box, the Fixer's own mixer, and ReTouch: a part of the takes chosen by dragging (time and tracks), a fix
    (De-click, Repair, Denoise, Gain - fermata's own processing), tried in place, then kept as a fix version. */
#include "FixerApp.h"
#include "../ui/Uikit.h"

namespace td::live
{
class FixerComponent;

class FixerWindow final : public juce::DocumentWindow
{
public:
    explicit FixerWindow (FixerApp&);
    ~FixerWindow() override;
    void closeButtonPressed() override { if (onClose) onClose(); }
    std::function<void()> onClose;
    /** A flag came from the Primary (or changed): shown at once, and offered for a reply. */
    void flagArrived (const FlagRec&);
private:
    FixerComponent* content = nullptr;
};
} // namespace td::live
