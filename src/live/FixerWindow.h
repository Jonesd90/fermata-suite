#pragma once
/** fermata:Live 0.3.0: the Fixer's window - the take window delayed (the timeline fills as the audio reaches the disk), Follow Primary, the
    Primary's flags at once with a reply box, the Fixer's own mixer, and ReTouch: a part of the takes chosen by dragging (time and tracks), a fix
    (De-click, Repair, Denoise, Gain - fermata's own processing), tried in place, then kept as a fix version. 0.4.0: or fermata's own ReTouch window on
    that part (src/ui/SpectralWindow.h, shared), whose fixes are kept as one fix version. */
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
    // 0.10.18: fitted to the screen and its contents (FitLayout.h)
    fit::WindowFit fitter { *this };
    void resized() override { juce::DocumentWindow::resized(); fitter.windowResized(); }
    void childBoundsChanged (juce::Component* c) override { if (! fitter.adjusting()) juce::DocumentWindow::childBoundsChanged (c); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { if (! fitter.wheel (w)) juce::DocumentWindow::mouseWheelMove (e, w); }
private:
    FixerComponent* content = nullptr;
};
} // namespace td::live
