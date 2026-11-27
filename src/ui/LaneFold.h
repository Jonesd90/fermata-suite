#pragma once
#include "AppContext.h"
#include "Uikit.h"
#include <set>

namespace td::lanefold
{
/** 3.72.0 (asked for on 27 November 2026, with a picture of the edit window's lanes - "Edit 1 comments", "Edit 2 pieces", "Changes since Edit 2",
    Markers, Feedback: "I'd like a way of collapsing and opening all of these lanes individually or globally please so that I can make the view of
    the tracks below larger if I need to"): THE LANES ABOVE THE TRACKS FOLD AWAY, in the take and edit windows.

    - One lane: its triangle, at the top right of its name (open: pointing down). A click folds it to one thin line - its name and a triangle
      pointing right - and a click on that line opens it again. The thin strips along the very top (the markers, the listeners' comments) fold
      away altogether: the LANES key's menu brings them back.
    - All of them: the LANES key at the top of the names column (beside the ruler) - its menu folds them all, opens them all, or folds and opens
      each one (ticked: open). The key is lit while any lane is folded.
    - Folded lanes are remembered for each window, in the program's settings (a view, like the tracks' heights: the project does not change).
      A lane that is not there (no guide, the tuning line switched off, no video) is not in the menu; its fold is kept for when it comes back.

    Also here (3.72.0, same request): placeZoomKeys - the take and edit windows' - and + under the pin and sun / moon buttons, on the buttons'
    second line (they were often alone on a third). */
constexpr int kFoldedH = 16;                    // a folded lane: one thin line

/** A lane of a window: its id (what the settings keep) and its name (in the menu and on its folded line). */
struct Lane { juce::String id, name; };

/** Which of one window's lanes are folded (see the top of this file). */
class Folds
{
public:
    Folds (AppContext& a, const juce::Uuid& windowId) : app (a), key ("laneFolds_" + windowId.toString())
    {
        if (auto* st = app.props.getUserSettings())
            for (auto& s : juce::StringArray::fromTokens (st->getValue (key), ",", "")) if (s.trim().isNotEmpty()) folded.insert (s.trim());
    }
    bool isFolded (const juce::String& lane) const { return folded.count (lane) > 0; }
    void set (const juce::String& lane, bool fold)
    {
        if (fold == isFolded (lane)) return;
        if (fold) folded.insert (lane); else folded.erase (lane);
        changed();
    }
    void toggle (const juce::String& lane) { set (lane, ! isFolded (lane)); }
    void setAll (const std::vector<Lane>& lanes, bool fold)
    {
        bool any = false;
        for (auto& l : lanes)
            if (fold != isFolded (l.id)) { any = true; if (fold) folded.insert (l.id); else folded.erase (l.id); }
        if (any) changed();
    }
    int countFolded (const std::vector<Lane>& lanes) const { int n = 0; for (auto& l : lanes) if (isFolded (l.id)) ++n; return n; }
    std::function<void()> onChange;             // (the window lays itself out again)

private:
    void changed()
    {
        if (auto* st = app.props.getUserSettings())
        {
            juce::StringArray s; for (auto& f : folded) s.add (f);
            if (s.isEmpty()) st->removeValue (key); else st->setValue (key, s.joinIntoString (","));
            app.props.saveIfNeeded();
        }
        if (onChange) onChange();
    }
    AppContext& app;
    juce::String key;
    std::set<juce::String> folded;
};

//==============================================================================================================================
// drawing
//==============================================================================================================================
/** Where a lane's triangle is: the top right of its name box (nameX .. nameX + nameW), on its first line. */
inline juce::Rectangle<int> triangleBox (int nameX, int nameW, int laneTop) { return { nameX + nameW - 22, laneTop + 1, 18, 14 }; }

inline void paintTriangle (juce::Graphics& g, juce::Rectangle<int> box, bool open, juce::Colour c)
{
    const auto b = box.toFloat().withSizeKeepingCentre (8.0f, 8.0f);
    juce::Path p;
    if (open) p.addTriangle (b.getX(), b.getY() + 1.5f, b.getRight(), b.getY() + 1.5f, b.getCentreX(), b.getBottom() - 0.5f);
    else      p.addTriangle (b.getX() + 1.5f, b.getY(), b.getX() + 1.5f, b.getBottom(), b.getRight() - 0.5f, b.getCentreY());
    g.setColour (c.withAlpha (0.9f)); g.fillPath (p);
}

/** A folded lane's band across the timeline (from x to x + w): darker, with a line under it. */
inline void paintFoldedBand (juce::Graphics& g, juce::Rectangle<int> band)
{
    g.setColour (juce::Colours::black.withAlpha (0.20f)); g.fillRect (band);
    g.setColour (juce::Colours::black.withAlpha (0.40f)); g.fillRect (band.getX(), band.getBottom() - 1, band.getWidth(), 1);
}

/** A folded lane's name box: its name, and the triangle (pointing right) that opens it. */
inline void paintFoldedName (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& name, juce::Colour colour)
{
    g.setColour (theme::panel); g.fillRect (box);
    g.setColour (juce::Colours::black.withAlpha (0.40f)); g.fillRect (box.getX(), box.getBottom() - 1, box.getWidth(), 1);
    g.setColour (colour.withAlpha (0.85f)); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (name, box.withTrimmedLeft (6).withTrimmedRight (24), juce::Justification::centredLeft, true);
    paintTriangle (g, triangleBox (box.getX(), box.getWidth(), box.getY()), false, colour);
}

/** The LANES key, at the top of the names column. */
inline juce::Rectangle<int> keyBox (int nameX, int y) { return { nameX + 6, y, 70, 18 }; }

inline void paintKey (juce::Graphics& g, juce::Rectangle<int> b, int foldedCount)
{
    const bool lit = foldedCount > 0;
    g.setColour (lit ? theme::accent.withAlpha (0.85f) : juce::Colour (0xff2a2f36)); g.fillRoundedRectangle (b.toFloat(), 3.0f);
    g.setColour (lit ? theme::accent.brighter (0.3f) : theme::border); g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 3.0f, 1.0f);
    const auto ink = lit ? juce::Colours::black : theme::text;
    g.setColour (ink); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText (lit ? "LANES " + juce::String (foldedCount) : juce::String ("LANES"), b.withTrimmedRight (14), juce::Justification::centred, false);
    paintTriangle (g, b.removeFromRight (16), true, ink);
}

inline juce::String keyTip()
{
    return "The lanes above the tracks: fold them all, open them all, or fold / open each one (each lane's own triangle, at the top right of its name, "
           "does the same). A folded lane is one thin line, so the tracks have more room. Lit, with how many: some lanes are folded.";
}

/** The LANES key's menu. 'owner' owns 'folds' (the menu does nothing if it has gone by the time something is chosen). */
inline void showMenu (juce::Component& owner, Folds& folds, const std::vector<Lane>& lanes, juce::Rectangle<int> screenArea)
{
    juce::PopupMenu m;
    m.addSectionHeader ("The lanes above the tracks");
    const int nFolded = folds.countFolded (lanes);
    m.addItem (1, "Fold all lanes", nFolded < (int) lanes.size());
    m.addItem (2, "Open all lanes", nFolded > 0);
    m.addSeparator();
    for (size_t i = 0; i < lanes.size(); ++i)
        m.addItem (10 + (int) i, lanes[i].name, true, ! folds.isFolded (lanes[i].id));
    if (lanes.empty()) m.addItem (-1, "(no lanes in this window just now)", false);
    juce::Component::SafePointer<juce::Component> safe (&owner);
    auto* f = &folds;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea), [safe, f, lanes] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r == 1) f->setAll (lanes, true);
        else if (r == 2) f->setAll (lanes, false);
        else if (juce::isPositiveAndBelow (r - 10, (int) lanes.size())) f->toggle (lanes[(size_t) (r - 10)].id);
    });
}

//==============================================================================================================================
/** The take and edit windows' - and + (the timeline's zoom): at the right of the buttons' second line, under the pin and sun / moon buttons in
    the window's corner (r: the window's area inside its 6-pixel edge; iconH: the height of the picture buttons, which sets the first line's). */
inline void placeZoomKeys (juce::Rectangle<int> r, juce::Component& minus, juce::Component& plus, int iconH)
{
    const int line1 = juce::jmax (grid::rowH, iconH + 8);
    const int y = r.getY() + line1 + (line1 - grid::btnH) / 2;
    const int w = (grid::cornerReserve - 6) / 2;
    plus.setBounds (r.getRight() - w, y, w, grid::btnH);
    minus.setBounds (plus.getX() - 2 - w, y, w, grid::btnH);
}
} // namespace td::lanefold
