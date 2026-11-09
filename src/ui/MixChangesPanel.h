#pragma once
#include "AppContext.h"
#include "Uikit.h"
#include "../core/EditVersions.h"

namespace td
{
/** 3.42.0: the Changes list of an edit's mixer (the "Changes..." key beside the phase scope): every setting that differs from the mixer as it was
    when a frozen version of the edit was made - faders to 0.1 dB, pans, mutes, solos, sends, low cuts, plug-ins and their settings - each with a
    Put back key that sets that one thing back; and one key that puts the whole mix back. The version compared with is chosen at the top (the
    edit's changes lane uses the same choice). */
class MixChangesPanel : public juce::Component, private juce::Timer
{
public:
    MixChangesPanel (AppContext& a, const juce::Uuid& mixerId) : app (a), mixer (mixerId)
    {
        intro.setColour (juce::Label::textColourId, theme::text); intro.setFont (juce::FontOptions (13.0f));
        intro.setText ("Every setting of this mixer that differs from how it was when the version was frozen. Put back sets one of them back; the whole mix can go back too.", juce::dontSendNotification);
        intro.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (intro);
        sinceCap.setText ("Since:", juce::dontSendNotification); sinceCap.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (sinceCap); addAndMakeVisible (sinceBox);
        sinceBox.onChange = [this] { if (auto* e = edit()) { e->compareWith = sinceBox.getSelectedId(); app.project.markDirty(); } rebuild (true); };
        viewport.setViewedComponent (&rows, false); viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        allButton.setButtonText ("Put back the whole mix"); allButton.onClick = [this] { putBackAll(); };
        allButton.setTooltip ("Sets every fader, pan, send, low cut and plug-in of this mixer as they were when that version was frozen (the plug-ins' settings too). Undo copy / the quit box can take it back.");
        closeButton.setButtonText ("Close"); closeButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->closeButtonPressed(); };
        addAndMakeVisible (allButton); addAndMakeVisible (closeButton);
        fillVersions();
        rebuild (true);
        setSize (640, 460);
        startTimerHz (2);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        intro.setBounds (r.removeFromTop (40));
        auto row = r.removeFromTop (24);
        sinceCap.setBounds (row.removeFromLeft (44)); sinceBox.setBounds (row.removeFromLeft (320));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (28);
        closeButton.setBounds (bottom.removeFromRight (90)); bottom.removeFromRight (8);
        allButton.setBounds (bottom.removeFromLeft (190));
        r.removeFromBottom (8);
        viewport.setBounds (r);
        layoutRows();
    }
    void paint (juce::Graphics& g) override { g.fillAll (theme::window); }

private:
    struct Row { std::unique_ptr<juce::Label> text; std::unique_ptr<juce::TextButton> button; };
    EditDef* edit() const { auto* m = app.project.mixerById (mixer); return m != nullptr && ! m->editId.isNull() ? app.project.findEdit (m->editId) : nullptr; }
    MixerState* mixerState() const { return app.project.mixerById (mixer); }
    void fillVersions()
    {
        sinceBox.clear (juce::dontSendNotification);
        auto* e = edit(); if (e == nullptr) return;
        int pick = 0;
        for (auto& v : e->versions)
        {
            if (v.mixer.isVoid()) continue;
            sinceBox.addItem (e->versionTitle (v.number) + "  (frozen " + v.frozenAt.toString (true, true, false) + ")", v.number);
            if (v.number == e->compareWith || pick == 0) pick = v.number;
        }
        if (pick > 0) sinceBox.setSelectedId (pick, juce::dontSendNotification);
        else sinceBox.setTextWhenNothingSelected (e->versions.empty() ? "no frozen version yet - freeze one with the Freeze key above the edit's timeline" : "no frozen version has a mixer");
    }
    void timerCallback() override { rebuild (false); }
    void rebuild (bool force)
    {
        auto* e = edit(); auto* m = mixerState();
        if (e == nullptr || m == nullptr) { changes.clear(); rowsUi.clear(); allButton.setEnabled (false); return; }
        if ((int) e->versions.size() != lastVersions) { lastVersions = (int) e->versions.size(); fillVersions(); force = true; }
        const auto* v = e->version (sinceBox.getSelectedId());
        if (v == nullptr || v->mixer.isVoid()) { changes.clear(); rowsUi.clear(); allButton.setEnabled (false); layoutRows(); return; }
        const auto now = app.project.mixSnapshot (*m, true);
        const auto key = juce::JSON::toString (now, true).hashCode64() ^ ((juce::int64) v->number << 48);
        if (! force && key == lastKey) return;
        lastKey = key;
        auto nameOf = [this] (const juce::String& kind, const juce::String& id) -> juce::String
        {
            if (kind == "track") { for (auto& t : app.project.tracks) if (t.id.toString() == id) return t.name; return "a track that is gone"; }
            if (kind == "bus") { for (auto& b : app.project.buses) if (b.id.toString() == id) return b.name; return "a bus that is gone"; }
            for (auto& x : app.project.vcas) if (juce::String (x.number) == id) return "VCA " + x.label();
            return "VCA " + id;
        };
        changes = editversions::compareMix (v->mixer, now, nameOf);
        rowsUi.clear();
        for (size_t i = 0; i < changes.size(); ++i)
        {
            Row r;
            r.text = std::make_unique<juce::Label>(); r.text->setText (changes[i].text, juce::dontSendNotification);
            r.text->setColour (juce::Label::textColourId, theme::text); r.text->setFont (juce::FontOptions (13.0f)); r.text->setMinimumHorizontalScale (0.7f);
            r.button = std::make_unique<juce::TextButton> ("Put back");
            const int k = (int) i;
            r.button->onClick = [this, k] { putBack (k); };
            rows.addAndMakeVisible (*r.text); rows.addAndMakeVisible (*r.button);
            rowsUi.push_back (std::move (r));
        }
        if (changes.empty())
        {
            Row r; r.text = std::make_unique<juce::Label>(); r.text->setText ("Nothing differs: the mixer is as it was at " + e->versionTitle (v->number) + ".", juce::dontSendNotification);
            r.text->setColour (juce::Label::textColourId, theme::dimText); r.text->setFont (juce::FontOptions (13.0f));
            rows.addAndMakeVisible (*r.text); rowsUi.push_back (std::move (r));
        }
        allButton.setEnabled (! changes.empty());
        layoutRows();
    }
    void layoutRows()
    {
        const int w = juce::jmax (100, viewport.getWidth() - 14), h = 26;
        rows.setSize (w, juce::jmax (viewport.getHeight(), (int) rowsUi.size() * h + 4));
        int y = 2;
        for (auto& r : rowsUi)
        {
            if (r.button != nullptr) { r.button->setBounds (w - 84, y + 2, 80, h - 4); r.text->setBounds (4, y, w - 94, h); }
            else r.text->setBounds (4, y, w - 8, h);
            y += h;
        }
    }
    void putBack (int k)
    {
        auto* e = edit(); auto* m = mixerState();
        if (e == nullptr || m == nullptr || k < 0 || k >= (int) changes.size()) return;
        const auto* v = e->version (sinceBox.getSelectedId()); if (v == nullptr || v->mixer.isVoid()) return;
        const auto& c = changes[(size_t) k];
        const auto now = app.project.mixSnapshot (*m, true);
        const auto patched = editversions::patchedMix (now, c);
        const double sr = app.engine.getSampleRate() > 0.0 ? app.engine.getSampleRate() : 48000.0;
        MixParts parts; parts.inserts = c.insert; parts.clearEmptySlots = c.insert;
        app.project.applyMix (*m, patched, sr, app.engine.getMaxBlock(), parts);
        if (c.insert) app.engine.rebuildPlan();
        app.project.structure.sendChangeMessage();
        app.setNotice ("Put back: " + c.text);
        rebuild (true);
    }
    void putBackAll()
    {
        auto* e = edit(); auto* m = mixerState();
        if (e == nullptr || m == nullptr) return;
        const auto* v = e->version (sinceBox.getSelectedId()); if (v == nullptr || v->mixer.isVoid()) return;
        const int n = v->number;
        juce::Component::SafePointer<MixChangesPanel> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Put back the whole mix")
            .withMessage ("Set this mixer as it was when " + e->versionTitle (n) + " was frozen - every fader, pan, send, low cut and plug-in?")
            .withButton ("Put back").withButton ("Cancel").withAssociatedComponent (this),
            [safe, n] (int r)
            {
                if (safe == nullptr || r != 1) return;
                auto* e2 = safe->edit(); auto* m2 = safe->mixerState(); if (e2 == nullptr || m2 == nullptr) return;
                const auto* v2 = e2->version (n); if (v2 == nullptr || v2->mixer.isVoid()) return;
                auto& app = safe->app;
                const double sr = app.engine.getSampleRate() > 0.0 ? app.engine.getSampleRate() : 48000.0;
                MixParts all; all.clearEmptySlots = true;
                app.project.applyMix (*m2, v2->mixer, sr, app.engine.getMaxBlock(), all);
                app.engine.rebuildPlan(); app.project.structure.sendChangeMessage();
                app.setNotice ("The mixer is as it was at Edit " + juce::String (n) + ".");
                safe->rebuild (true);
            });
    }

    AppContext& app; juce::Uuid mixer;
    juce::Label intro, sinceCap; juce::ComboBox sinceBox;
    juce::Viewport viewport; juce::Component rows;
    std::vector<Row> rowsUi; std::vector<editversions::MixChange> changes;
    juce::TextButton allButton, closeButton;
    juce::int64 lastKey = 0; int lastVersions = -1;
};

inline void showMixChangesPanel (AppContext& app, const juce::Uuid& mixerId)
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Mixer changes since a frozen version";
    o.content.setOwned (new MixChangesPanel (app, mixerId));
    o.dialogBackgroundColour = theme::window;
    o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = true;
    o.launchAsync();
}
} // namespace td
