#pragma once
/** The small-screen check's eye and hands (fermata 3.71.0; tools/fit-check.sh): opens a program's windows one after another ("scenes"), and for each
    writes what does not fit (td::fit::audit, src/ui/FitLayout.h) and a picture of it, then closes it. Built into a program only for the check
    (FERMATA_FIT_CHECK=1), never into the program the user runs. fermata's scenes: FitCheckDriver.h; fermata:Live's: LiveFitCheck.h.
    - each window as it opens, then its tabs; then (a window that can be resized) filling the screen, and its tabs again;
    - report.txt: one block per window - its size, whether it scrolls (down / across, and how big its contents are laid out), how long the fitting
      took, and the problems: "squashed" (a control shorter than it needs), "text" (text wider than its control), "overlap" (two controls over one
      another), "off" (a control off the window where nothing scrolls to it), "why it scrolls" (the controls that would be squashed without it);
    - <n>-<scene>-<how>.png: the window. Then the program quits. */
#include "../../src/ui/FitLayout.h"
#include <set>

namespace td::fitcheck
{
class Checker
{
public:
    Checker (juce::Component& mainWin, juce::File outDir) : main (mainWin), out (std::move (outDir))
    {
        out.createDirectory();
        report = out.getChildFile ("report.txt");
        report.replaceWithText ("");
        const auto& d = *juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        say ("screen " + d.totalArea.toString() + "  usable " + d.userArea.toString() + "  scale " + juce::String (d.scale));
    }
    virtual ~Checker() = default;
    /** Starts the scenes (after 'delayMs', for the program to settle). */
    void begin (int delayMs = 5000) { juce::Timer::callAfterDelay (delayMs, [this] { runScene (0); }); }

protected:
    struct Scene { juce::String name; std::function<void()> open; juce::StringArray tabs; bool mainOnly = false; };
    juce::Component& main;
    juce::File out, report;
    std::vector<Scene> scenes;
    std::set<juce::Component*> before;
    int shot = 0, totalProblems = 0;

    /** A scene. FIT_ONLY (3.72.0; optional: names, with commas between): only those scenes - "main" is always there. */
    void add (const juce::String& n, std::function<void()> f, juce::StringArray tabs = {}, bool mainOnly = false)
    {
        const auto only = juce::SystemStats::getEnvironmentVariable ("FIT_ONLY", {});
        if (only.isNotEmpty() && ! mainOnly && ! juce::StringArray::fromTokens (only, ",", "").contains (n)) return;
        scenes.push_back ({ n, std::move (f), std::move (tabs), mainOnly });
    }
    void say (const juce::String& s) { report.appendText (s + "\n"); }

    static std::vector<juce::Component*> windowsNow()
    {
        std::vector<juce::Component*> v;
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
            if (auto* c = d.getComponent (i); c != nullptr && c->isVisible() && c->getWidth() > 40 && c->getHeight() > 40 && dynamic_cast<juce::TopLevelWindow*> (c) != nullptr)
                v.push_back (c);
        return v;
    }
    std::vector<juce::Component*> fresh() const
    {
        std::vector<juce::Component*> v;
        for (auto* c : windowsNow()) if (before.count (c) == 0) v.push_back (c);
        return v;
    }

    void runScene (size_t i)
    {
        if (i >= scenes.size()) { finish(); return; }
        before.clear();
        for (auto* c : windowsNow()) before.insert (c);
        if (scenes[i].mainOnly)
        {
            look (&main, scenes[i].name, "as it is");
            juce::Timer::callAfterDelay (300, [this, i] { runScene (i + 1); });
            return;
        }
        scenes[i].open();
        juce::Timer::callAfterDelay (1500, [this, i] { opened (i); });
    }
    void opened (size_t i)
    {
        auto list = fresh();
        if (list.empty()) say ("== " + scenes[i].name + ": nothing opened");
        for (auto* w : list) look (w, scenes[i].name, "as it opened");
        tab (i, 0, "", [this, i] { grow (i); });                 // (its tabs at the size it opened at, then as big as the screen lets it be)
    }
    void grow (size_t i)
    {
        bool grew = false;
        for (auto* w : fresh())
            if (auto* rw = dynamic_cast<juce::ResizableWindow*> (w); rw != nullptr && rw->isResizable())
            {
                const auto room = fit::roomFor (*rw);
                if (! room.isEmpty() && rw->getBounds() != room) { rw->setBounds (room); grew = true; }
            }
        if (! grew) { closeAll (i); return; }
        juce::Timer::callAfterDelay (900, [this, i]
        {
            for (auto* w : fresh()) look (w, scenes[i].name, "filling the screen");
            tab (i, 0, ", filling the screen", [this, i] { closeAll (i); });
        });
    }
    void tab (size_t i, int t, juce::String how, std::function<void()> then)
    {
        if (t >= scenes[i].tabs.size()) { then(); return; }
        const auto name = scenes[i].tabs[t];
        for (auto* w : fresh())
            if (auto* b = findButton (*w, name)) b->triggerClick();
        juce::Timer::callAfterDelay (900, [this, i, t, name, how, then]
        {
            for (auto* w : fresh()) look (w, scenes[i].name, "tab " + name + how);
            tab (i, t + 1, how, then);
        });
    }
    void closeAll (size_t i)
    {
        for (auto* w : fresh())
        {
            if (auto* aw = dynamic_cast<juce::AlertWindow*> (w)) { aw->exitModalState (0); continue; }
            if (auto* dw = dynamic_cast<juce::DocumentWindow*> (w)) { dw->closeButtonPressed(); continue; }
            if (w->isCurrentlyModal()) w->exitModalState (0);
        }
        juce::Timer::callAfterDelay (900, [this, i]
        {
            for (auto* w : fresh()) { if (w->isCurrentlyModal()) w->exitModalState (0); else w->setVisible (false); }     // (anything that did not go)
            runScene (i + 1);
        });
    }
    static juce::Button* findButton (juce::Component& c, const juce::String& text)
    {
        for (auto* ch : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (ch); b != nullptr && b->isShowing() && b->getButtonText() == text) return b;
            if (auto* b = findButton (*ch, text)) return b;
        }
        return nullptr;
    }

    /** A window whose contents look after themselves: what would be squashed in it, compared with a roomy layout (laid out once more, then put back). */
    static void selfFittingSquash (juce::ResizableWindow& rw, std::vector<fit::Problem>& out)
    {
        auto* c = rw.getContentComponent();
        if (c == nullptr || ! fit::isSelfFitting (*c)) return;
        const auto area = rw.getContentComponentBorder().subtractedFrom (rw.getLocalBounds());
        fit::Roomy roomy;
        c->setBounds (area.withHeight (juce::jmax (1600, area.getHeight() * 2))); roomy.take (*c);
        c->setBounds (area);
        juce::Array<juce::Component*> found;
        fit::squashed (*c, roomy, &found);
        for (auto* x : found) out.push_back ({ "squashed (self-fitting)", fit::describe (*x), rw.getLocalArea (x, x->getLocalBounds()) });
    }

    /** What made a window scroll: the controls its fitter found squashed at the window's own size. */
    static void whyItScrolls (juce::ResizableWindow& rw, fit::WindowFit& f, std::vector<fit::Problem>& out)
    {
        for (auto& p : f.whyItScrollsAcross()) if (auto* x = p.getComponent()) out.push_back ({ "why it scrolls across", fit::describe (*x), rw.getLocalArea (x, x->getLocalBounds()) });
        for (auto& p : f.whyItScrollsDown()) if (auto* x = p.getComponent()) out.push_back ({ "why it scrolls down", fit::describe (*x), rw.getLocalArea (x, x->getLocalBounds()) });
    }
    void look (juce::Component* w, const juce::String& scene, const juce::String& how)
    {
        auto problems = fit::audit (*w);
        juce::String head = "== " + scene + " / " + how + ": \"" + w->getName() + "\" " + juce::String (w->getWidth()) + "x" + juce::String (w->getHeight());
        if (auto* rw = dynamic_cast<juce::ResizableWindow*> (w))
        {
            selfFittingSquash (*rw, problems);
            if (auto* c = rw->getContentComponent(); c != nullptr && fit::isSelfFitting (*c)) head << "  (looks after itself)";
        }
        if (auto* f = fit::fitterOf (w))
        {
            if (! f->scrolling()) head << "  fits";
            else head << "  SCROLLS" << (f->scrollsDown() ? " down (contents " + juce::String (f->neededHeight()) + " tall)" : juce::String())
                      << (f->scrollsAcross() ? " across (contents " + juce::String (f->neededWidth()) + " wide)" : juce::String());
            head << "  fitted in " << juce::String (f->lastFitMs(), 1) << " ms";
        }
        else head << "  (no fitter)";
        if (auto* f = fit::fitterOf (w); f != nullptr && f->scrolling())
            if (auto* rw = dynamic_cast<juce::ResizableWindow*> (w))
                whyItScrolls (*rw, *f, problems);
        const auto file = juce::String (++shot).paddedLeft ('0', 3) + "-" + scene + "-" + how.replaceCharacters (" ", "_").retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") + ".png";
        head << "  [" << file << "]";
        say (head);
        for (auto& p : problems)
            say ("   " + p.kind + ": " + p.what + "  at " + p.where.toString());
        totalProblems += (int) problems.size();
        auto img = w->createComponentSnapshot (w->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (out.getChildFile (file));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
    }
    void finish()
    {
        say ("DONE: " + juce::String (shot) + " windows looked at, " + juce::String (totalProblems) + " problems");
        juce::Timer::callAfterDelay (500, [] { juce::JUCEApplicationBase::quit(); });
    }
};
} // namespace td::fitcheck
