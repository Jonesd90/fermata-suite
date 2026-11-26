#pragma once
#include "Uikit.h"
#include "KeyMap.h"
#include "HotKeysLines.h"
#include <vector>

namespace td
{
/** HotKeys (fermata: Help or Project menu; fermata:Live: Help menu): every key of the program and what it does, window by window, with the keys
    drawn as keys. 3.69.0 / 0.10.16: the keys of the windows' own jobs can be changed here - click one - and the keys in use saved as a file of
    their own (Save keys as...), loaded again on this computer or another (Load keys...), or put back to the standard ones. See KeyMap.h. Shared
    by both programs (COMPATIBILITY.md): each shows its own list. */
namespace hotkeys
{
inline bool isMouseWord (const juce::String& s)
{
    const auto w = s.toLowerCase();
    return w == "click" || w == "drag" || w == "double-click" || w == "right-click" || w == "wheel";
}
/** The lines a program's window shows: fermata's; or fermata:Live's, with the ReTouch window's (the Fixer opens it). */
inline std::vector<const Line*> linesFor (bool live)
{
    std::vector<const Line*> v;
    if (live) { for (auto& l : liveLines()) v.push_back (&l); for (auto& l : fermataLines()) if (sharedWithLive (l.section)) v.push_back (&l); }
    else for (auto& l : fermataLines()) v.push_back (&l);
    return v;
}
/** The binding a key of a line stands for (-1: a key that cannot be changed, the mouse, a word). */
inline int bindingOf (const Line& l, const juce::String& token)
{
    const juce::String sec (l.section);
    const auto shown = keymap::displaySection (sec);
    if (! keymap::editableLine (l) && shown == sec) return -1;
    const auto k = keymap::parse (token);
    if (! keymap::editableKey (k)) return -1;
    return keymap::KeyMap::get().indexOf (shown, k);
}
/** A line's keys as they are now ("F5 / 2"). */
inline juce::String keysNow (const Line& l)
{
    juce::String out;
    for (auto& t : keymap::tokensOf (l.keys))
    {
        if (t.separator) { out << (t.text == "," ? juce::String (", ") : " " + t.text + " "); continue; }
        const int b = bindingOf (l, t.text);
        out << (b >= 0 ? keymap::KeyMap::get().label (keymap::KeyMap::get().bindings()[(size_t) b].section, keymap::KeyMap::get().bindings()[(size_t) b].def) : t.text);
    }
    return out;
}
/** The whole list as plain text (the Copy button): a heading per window, the keys in use in a column, then what they do. */
inline juce::String asText (const std::vector<const Line*>& lines, const juce::String& title)
{
    juce::String out (title + "\n" + keymap::KeyMap::get().setName() + "\n");
    juce::String section;
    for (auto* l : lines)
    {
        if (section != l->section) { section = l->section; out << "\n" << section.toUpperCase() << "\n"; }
        const juce::String keys (l->keys);
        if (keys.isEmpty()) out << "  (" << l->what << ")\n";
        else out << "  " << keysNow (*l).paddedRight (' ', 26) << "  " << l->what << "\n";
    }
    return out;
}
} // namespace hotkeys

/** The list itself: headings, and a line per key with the keys drawn as keycaps on the left and what they do on the right. The keys that can be
    changed answer a click (onPick); a key that is not the standard one has a coloured edge. Sits in a viewport. */
class HotKeysList : public juce::Component, public juce::TooltipClient
{
public:
    explicit HotKeysList (std::vector<const hotkeys::Line*> l) : lines (std::move (l)) { setWantsKeyboardFocus (true); }      // (with the keyboard here, the arrows scroll the list; every other key goes on)
    std::function<void (int binding)> onPick;
    void setFilter (const juce::String& f) { filter = f.trim(); relayout(); }
    void setSelected (int b) { selected = b; repaint(); }
    void layoutFor (int width, int height) { if (width > 0) { listW = width; minH = height; relayout(); } }      // (at least the viewport's height: the list's background fills it)
    void relayout()
    {
        rows.clear();
        int y = 4;
        juce::String section;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const auto& l = *lines[i];
            if (! matches (l)) continue;
            if (section != l.section) { section = l.section; rows.push_back ({ Row::Head, i, y, kHeadH, {} }); y += kHeadH + 4; }
            const bool note = juce::String (l.keys).isEmpty();
            const int textW = note ? listW - 2 * kPad - 8 : listW - 2 * kPad - kKeysW;
            Row r { Row::Line, i, y, 0, note ? std::vector<Piece>() : keyPieces (l) };
            int keysH = 0;
            for (auto& p : r.pieces) keysH = juce::jmax (keysH, p.box.getBottom());
            r.h = juce::jmax (keysH, (int) std::ceil (layoutOf (l.what, textW, note).getHeight()) + 2) + 2 * kRowPad;
            y += r.h;
            rows.push_back (std::move (r));
        }
        setSize (listW, juce::jmax (minH, 80, y + 12));
        repaint();
    }
    int count() const { int n = 0; for (auto& r : rows) if (r.kind == Row::Line && juce::String (lines[r.index]->keys).isNotEmpty()) ++n; return n; }     // (the notes are not counted)
    /** Where a binding's first key is in the list (to scroll it into view), or -1. */
    int yOf (int binding) const
    {
        for (auto& r : rows) for (auto& p : r.pieces) if (p.binding == binding) return r.y;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::row);
        const auto clip = g.getClipBounds();
        if (rows.empty())
        {
            g.setColour (theme::dimText); g.setFont (juce::FontOptions (14.0f, juce::Font::italic));
            g.drawText ("No key matches \"" + filter + "\".", getLocalBounds().reduced (16, 0).removeFromTop (60), juce::Justification::centredLeft, true);
            return;
        }
        int alt = 0;
        const auto& B = keymap::KeyMap::get().bindings();
        for (auto& r : rows)
        {
            if (r.kind == Row::Head) alt = 0;
            if (r.y > clip.getBottom() || r.y + r.h < clip.getY()) { if (r.kind == Row::Line) ++alt; continue; }
            const auto& ln = *lines[r.index];
            const auto area = juce::Rectangle<int> (0, r.y, getWidth(), r.h);
            if (r.kind == Row::Head)
            {
                g.setColour (theme::text); g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
                g.drawText (ln.section, area.reduced (kPad, 0).withTrimmedTop (8), juce::Justification::centredLeft, true);
                g.setColour (theme::accent); g.fillRect (kPad, r.y + r.h - 3, getWidth() - 2 * kPad, 2);
                continue;
            }
            if (alt++ % 2 == 1) { g.setColour (theme::rowAlt); g.fillRect (area); }
            if (juce::String (ln.keys).isEmpty())
            {
                drawText (g, ln.what, juce::Rectangle<int> (kPad + 4, r.y + kRowPad, getWidth() - 2 * kPad - 8, r.h - 2 * kRowPad), true);
                continue;
            }
            for (auto& p : r.pieces)
            {
                auto b = p.box.translated (kPad, r.y + kRowPad);
                const bool changed = p.binding >= 0 && B[(size_t) p.binding].changed();
                const bool sel = p.binding >= 0 && p.binding == selected;
                if (p.kind == Piece::Cap)
                {
                    const auto fill = sel ? theme::accent : theme::button;
                    g.setColour (fill); g.fillRoundedRectangle (b.toFloat(), 4.0f);
                    g.setColour (fill.darker (0.45f)); g.fillRect (b.getX() + 3, b.getBottom() - 2, b.getWidth() - 6, 2);           // (a key's lower edge)
                    g.setColour (changed ? theme::warn : theme::border); g.drawRoundedRectangle (b.toFloat().reduced (changed ? 1.0f : 0.5f), 4.0f, changed ? 2.0f : 1.0f);
                    g.setColour (theme::inkOn (fill)); g.setFont (capFont());
                    g.drawText (p.text, b.withTrimmedBottom (1), juce::Justification::centred, false);
                }
                else
                {
                    g.setColour (p.kind == Piece::None ? theme::warn : theme::dimText);
                    g.setFont (p.kind == Piece::Mouse || p.kind == Piece::None ? juce::FontOptions (13.0f, juce::Font::italic) : juce::FontOptions (12.5f));
                    g.drawText (p.text, b, juce::Justification::centred, false);
                    if (sel) { g.setColour (theme::accent); g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.5f); }
                }
            }
            drawText (g, ln.what, juce::Rectangle<int> (kPad + kKeysW, r.y + kRowPad, getWidth() - kPad * 2 - kKeysW, r.h - 2 * kRowPad), false);
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (pickAt (e.getPosition()) >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int b = pickAt (e.getPosition());
        if (b >= 0 && onPick) onPick (b);
    }
    juce::String getTooltip() override
    {
        const int b = pickAt (getMouseXYRelative());
        if (b < 0) return {};
        const auto& k = keymap::KeyMap::get().bindings()[(size_t) b];
        return "Click to change this key" + (k.changed() ? " (the standard key: " + keymap::describe (k.def) + ")" : juce::String());
    }

private:
    static constexpr int kPad = 14, kKeysW = 270, kCapH = 22, kRowPad = 6, kHeadH = 40;
    struct Piece { enum Kind { Cap, Plus, Word, Mouse, None } kind; juce::String text; juce::Rectangle<int> box; int binding = -1; };
    struct Row { enum Kind { Head, Line } kind; size_t index; int y, h; std::vector<Piece> pieces; };
    std::vector<const hotkeys::Line*> lines;
    std::vector<Row> rows;
    juce::String filter;
    int listW = 600, minH = 0, selected = -1;

    static juce::Font capFont() { return juce::Font (juce::FontOptions (12.5f, juce::Font::bold)); }
    static juce::Font textFont() { return juce::Font (juce::FontOptions (14.0f)); }
    static juce::Font noteFont() { return juce::Font (juce::FontOptions (13.5f, juce::Font::italic)); }

    int pickAt (juce::Point<int> pos) const
    {
        for (auto& r : rows)
        {
            if (pos.y < r.y || pos.y >= r.y + r.h) continue;
            for (auto& p : r.pieces)
                if (p.binding >= 0 && p.box.translated (kPad, r.y + kRowPad).expanded (2).contains (pos)) return p.binding;
        }
        return -1;
    }
    /** A key's parts: the modifiers, then the key ("Ctrl", "Shift", "Z"; "Alt", "click"). */
    static juce::StringArray partsOf (const juce::String& token)
    {
        juce::StringArray parts;
        auto s = token;
        for (bool more = true; more;)
        {
            more = false;
            for (const char* m : { "Ctrl+", "Cmd+", "Alt+", "Shift+" })
                if (const juce::String mm (m); s.startsWithIgnoreCase (mm) && s.length() > mm.length()) { parts.add (mm.dropLastCharacters (1)); s = s.substring (mm.length()); more = true; }
        }
        parts.add (s);
        return parts;
    }
    /** The keys of a line laid out left to right in the keys column, relative to its top left corner. A combination (Ctrl + Shift + Z) is never split:
        when it does not fit after what is already on the line, it starts the next one. A key that can be changed is drawn as it is now. */
    std::vector<Piece> keyPieces (const hotkeys::Line& l) const
    {
        std::vector<Piece> out;
        int x = 0, y = 0;
        const auto smallW = [] (const juce::String& s) { return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (12.5f)), s); };
        const auto mouseW = [] (const juce::String& s) { return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (13.0f, juce::Font::italic)), s) + 6; };
        const auto capW   = [] (const juce::String& s) { return juce::jmax (kCapH + 2, juce::GlyphArrangement::getStringWidthInt (capFont(), s) + 16); };
        const auto& km = keymap::KeyMap::get();
        for (auto& t : keymap::tokensOf (l.keys))
        {
            if (t.separator)
            {
                const int w = smallW (t.text) + 8;
                out.push_back ({ Piece::Word, t.text, juce::Rectangle<int> (x + 3, y, w, kCapH) });
                x += w + 6;
                continue;
            }
            const int b = hotkeys::bindingOf (l, t.text);
            juce::String shown = t.text;
            if (b >= 0)
            {
                const auto& bd = km.bindings()[(size_t) b];
                if (! bd.now.valid())
                {
                    const int w = mouseW ("no key") + 8;
                    if (x > 0 && x + w > kKeysW - 8) { x = 0; y += kCapH + 4; }
                    out.push_back ({ Piece::None, "no key", juce::Rectangle<int> (x, y, w, kCapH), b });
                    x += w;
                    continue;
                }
                shown = keymap::describe (bd.now);
            }
            auto parts = partsOf (shown);
            int comboW = 0;
            for (int i = 0; i < parts.size(); ++i) comboW += (i > 0 ? 13 : 0) + (hotkeys::isMouseWord (parts[i]) ? mouseW (parts[i]) : capW (parts[i]));
            if (x > 0 && x + comboW > kKeysW - 8) { x = 0; y += kCapH + 4; }                     // (the whole combination onto the next line)
            for (int i = 0; i < parts.size(); ++i)
            {
                if (i > 0) { out.push_back ({ Piece::Plus, "+", juce::Rectangle<int> (x, y, 13, kCapH), b }); x += 13; }
                const auto& p = parts[i];
                const bool mouse = hotkeys::isMouseWord (p);
                const int w = mouse ? mouseW (p) : capW (p);
                out.push_back ({ mouse ? Piece::Mouse : Piece::Cap, p, juce::Rectangle<int> (x, y, w, kCapH), b });
                x += w;
            }
        }
        return out;
    }
    static juce::TextLayout layoutOf (const juce::String& text, int width, bool note)
    {
        juce::AttributedString as; as.setJustification (juce::Justification::topLeft);
        as.append (text, note ? noteFont() : textFont(), note ? theme::dimText : theme::text);
        juce::TextLayout tl; tl.createLayout (as, (float) juce::jmax (40, width));
        return tl;
    }
    static void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, bool note)
    {
        auto tl = layoutOf (text, r.getWidth(), note);
        const float top = note ? (float) r.getY() : (float) r.getY() + 2.0f;             // (level with the keys' writing)
        tl.draw (g, juce::Rectangle<float> ((float) r.getX(), top, (float) r.getWidth(), tl.getHeight() + 2.0f));
    }
    bool matches (const hotkeys::Line& l) const
    {
        if (filter.isEmpty()) return true;
        const auto keys = juce::String (l.keys) + " " + hotkeys::keysNow (l);
        for (auto& word : juce::StringArray::fromTokens (filter, " ", ""))                // every word typed must be found somewhere in the line
            if (! juce::String (l.section).containsIgnoreCase (word) && ! keys.containsIgnoreCase (word) && ! juce::String (l.what).containsIgnoreCase (word))
                return false;
        return true;
    }
};

/** The box a new key is pressed into: it takes every key as it is pressed (KeyMap::setRaw), Esc giving up. */
class KeyCatcher : public juce::Component
{
public:
    KeyCatcher() { setWantsKeyboardFocus (true); keymap::KeyMap::setRaw (*this); }
    std::function<void()> onCaught, onCancel;
    keymap::Key caught;
    juce::String refusal;
    void clear() { caught = {}; refusal.clear(); repaint(); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::field); g.fillRoundedRectangle (r, 5.0f);
        g.setColour (hasKeyboardFocus (false) ? theme::accent : theme::border); g.drawRoundedRectangle (r, 5.0f, hasKeyboardFocus (false) ? 2.0f : 1.0f);
        if (caught.valid())
        {
            g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
            g.drawText (keymap::describe (caught), getLocalBounds(), juce::Justification::centred, false);
        }
        else
        {
            g.setColour (refusal.isNotEmpty() ? theme::warn : juce::Colours::white.withAlpha (0.6f)); g.setFont (juce::FontOptions (13.0f, juce::Font::italic));
            g.drawText (refusal.isNotEmpty() ? refusal : (hasKeyboardFocus (false) ? juce::String ("Press the new key (with Ctrl, Alt, Shift if you like)") : juce::String ("Click here, then press the new key")),
                        getLocalBounds().reduced (8, 0), juce::Justification::centred, true);
        }
    }
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::escapeKey && ! k.getModifiers().isAnyModifierKeyDown()) { if (onCancel) onCancel(); return true; }
        const auto key = keymap::keyOf (k);
        if (key.code == juce::KeyPress::tabKey || key.code == juce::KeyPress::escapeKey) { caught = {}; refusal = "Tab and Esc keep their jobs everywhere: choose another key"; repaint(); return true; }
        if (! key.valid()) return true;
        caught = key; refusal.clear(); repaint();
        if (onCaught) onCaught();
        return true;
    }
};

/** The window's contents: a box to find a key or a word, the Copy button, the keys in use with Load / Save as / the standard keys, the list, and
    - once a key has been clicked - the bar that changes it. */
class HotKeysComponent : public juce::Component, private juce::ChangeListener
{
public:
    /** live: fermata:Live's list (with the ReTouch window's); else fermata's. */
    explicit HotKeysComponent (bool forLive = false) : live (forLive), list (hotkeys::linesFor (forLive))
    {
        find.setTextToShowWhenEmpty ("Find a key or a word: zoom, mark, Enter, F2 ...", theme::dimText.withAlpha (0.7f));
        find.onTextChange = [this] { list.setFilter (find.getText()); viewport.setViewPosition (0, 0); showCount(); };
        find.onEscapeKey = [this] { find.clear(); list.setFilter ({}); showCount(); list.grabKeyboardFocus(); };
        find.onReturnKey = [this] { list.grabKeyboardFocus(); };
        addAndMakeVisible (find);
        copyButton.setTooltip ("Copies the whole list, with the keys in use, as text to paste into a document or an e-mail");
        copyButton.onClick = [this]
        {
            const auto version = juce::JUCEApplication::getInstance() != nullptr ? juce::JUCEApplication::getInstance()->getApplicationVersion() : juce::String();
            juce::SystemClipboard::copyTextToClipboard (hotkeys::asText (hotkeys::linesFor (live), (live ? "FERMATA:LIVE " : "FERMATA ") + version + " - HOTKEYS"));
            copyButton.setButtonText ("Copied");
            juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<juce::TextButton> (&copyButton)] { if (safe != nullptr) safe->setButtonText ("Copy the list"); });
        };
        addAndMakeVisible (copyButton);
        for (auto* l : { &countLabel, &setLabel, &status })
        {
            l->setColour (juce::Label::textColourId, theme::dimText);
            l->setFont (juce::FontOptions (12.5f));
            addAndMakeVisible (*l);
        }
        countLabel.setJustificationType (juce::Justification::centredRight);
        setLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        setLabel.setColour (juce::Label::textColourId, theme::text);
        loadButton.setTooltip ("Use keys saved with Save keys as... - your own, or someone else's (from either program, on any computer). fermata and fermata:Live then both use them, and keep using them until other keys are loaded.");
        saveButton.setTooltip ("Keep the keys in use as a file of their own (in Documents > Fermata > Hot Keys to begin with), to load again later or on another computer");
        standardButton.setTooltip ("Every key back to the one the program comes with (a file saved with Save keys as... is left as it is)");
        loadButton.onClick = [this] { loadKeys(); };
        saveButton.onClick = [this] { saveKeys(); };
        standardButton.onClick = [this] { backToStandard(); };
        for (auto* b : { &loadButton, &saveButton, &standardButton }) addAndMakeVisible (*b);
        list.onPick = [this] (int b) { pick (b); };
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (12);
        addAndMakeVisible (viewport);

        // ---- the bar that changes a key
        barTitle.setColour (juce::Label::textColourId, theme::text);
        barTitle.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        barTitle.setMinimumHorizontalScale (1.0f);
        barNow.setColour (juce::Label::textColourId, theme::dimText);
        barNow.setFont (juce::FontOptions (13.0f));
        catcher.onCaught = [this] { useButton.setEnabled (true); };
        catcher.onCancel = [this] { closeBar(); };
        useButton.onClick = [this] { if (picked >= 0 && catcher.caught.valid()) applyKey (picked, catcher.caught); };
        noKeyButton.onClick = [this] { if (picked >= 0) applyKey (picked, {}); };
        ownKeyButton.onClick = [this] { if (picked >= 0) applyKey (picked, keymap::KeyMap::get().bindings()[(size_t) picked].def); };
        cancelButton.onClick = [this] { closeBar(); };
        useButton.setTooltip ("This key does the job from now on (here, and in fermata:Live or fermata too)");
        noKeyButton.setTooltip ("No key does this job (its standard key does nothing here then)");
        for (auto* c : std::initializer_list<juce::Component*> { &barTitle, &barNow, &catcher, &useButton, &noKeyButton, &ownKeyButton, &cancelButton }) bar.addAndMakeVisible (*c);
        addChildComponent (bar);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.8f);

        keymap::KeyMap::get().addChangeListener (this);
        setSize (900, 780);
        showSet();
        showCount();
    }
    ~HotKeysComponent() override { keymap::KeyMap::get().removeChangeListener (this); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::window);
        if (bar.isVisible())
        {
            g.setColour (theme::panel); g.fillRoundedRectangle (bar.getBounds().toFloat(), 6.0f);
            g.setColour (theme::accent); g.drawRoundedRectangle (bar.getBounds().toFloat().reduced (0.5f), 6.0f, 1.5f);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto top = r.removeFromTop (30);
        top.removeFromRight (52);                                     // (the window's pin and sun / moon buttons sit in the top right corner)
        copyButton.setBounds (top.removeFromRight (128));
        top.removeFromRight (10);
        countLabel.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        find.setBounds (top);
        r.removeFromTop (8);
        auto second = r.removeFromTop (28);
        standardButton.setBounds (second.removeFromRight (190)); second.removeFromRight (6);
        saveButton.setBounds (second.removeFromRight (130)); second.removeFromRight (6);
        loadButton.setBounds (second.removeFromRight (120)); second.removeFromRight (10);
        setLabel.setBounds (second);
        r.removeFromTop (4);
        status.setBounds (r.removeFromBottom (status.getText().isEmpty() ? 0 : 22));
        if (bar.isVisible())
        {
            auto b = r.removeFromBottom (112);
            r.removeFromBottom (8);
            bar.setBounds (b);
            auto in = bar.getLocalBounds().reduced (10, 8);
            barTitle.setBounds (in.removeFromTop (22));
            barNow.setBounds (in.removeFromTop (22));
            in.removeFromTop (6);
            auto row = in.removeFromTop (34);
            cancelButton.setBounds (row.removeFromRight (80)); row.removeFromRight (6);
            ownKeyButton.setBounds (row.removeFromRight (juce::jmax (150, juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f)), ownKeyButton.getButtonText()) + 24))); row.removeFromRight (6);
            noKeyButton.setBounds (row.removeFromRight (80)); row.removeFromRight (6);
            useButton.setBounds (row.removeFromRight (120)); row.removeFromRight (10);
            catcher.setBounds (row);
        }
        viewport.setBounds (r);
        list.layoutFor (viewport.getMaximumVisibleWidth(), viewport.getMaximumVisibleHeight());
        showCount();
    }
    /** The keyboard goes to the list (not the Find box): the keys then work as everywhere else, and the arrows scroll. */
    void focusGained (FocusChangeType) override { if (! bar.isVisible()) list.grabKeyboardFocus(); }

private:
    const bool live;
    juce::TextEditor find;
    juce::TextButton copyButton { "Copy the list" };
    juce::Label countLabel, setLabel, status;
    juce::TextButton loadButton { "Load keys..." }, saveButton { "Save keys as..." }, standardButton { "Back to the standard keys" };
    juce::Viewport viewport;
    HotKeysList list;
    juce::Component bar;
    juce::Label barTitle, barNow;
    KeyCatcher catcher;
    juce::TextButton useButton { "Use this key" }, noKeyButton { "No key" }, ownKeyButton { "Its standard key" }, cancelButton { "Cancel" };
    int picked = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String programName() const { return live ? "fermata:Live" : "fermata"; }
    juce::String otherProgram() const { return live ? "fermata" : "fermata:Live"; }
    static const keymap::Binding& B (int i) { return keymap::KeyMap::get().bindings()[(size_t) i]; }
    static juce::String jobOf (const keymap::Binding& b)
    {
        auto w = b.what[0];
        if (w.length() > 110) w = w.substring (0, 107).trimEnd() + "...";
        return w + (b.what.size() > 1 ? "  (and " + juce::String (b.what.size() - 1) + " more)" : juce::String());
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { list.relayout(); showSet(); showCount(); if (picked >= 0) showBar(); }
    void showCount()
    {
        const int n = list.count();
        countLabel.setText (find.getText().trim().isEmpty() ? juce::String (n) + " shortcuts" : juce::String (n) + (n == 1 ? " match" : " matches"), juce::dontSendNotification);
    }
    void showSet()
    {
        auto& km = keymap::KeyMap::get();
        const int n = km.changedCount();
        setLabel.setText ("Keys in use: " + km.setName() + (n > 0 ? "  (" + juce::String (n) + (n == 1 ? " key" : " keys") + " not the standard ones, edged in orange)" : juce::String()),
                          juce::dontSendNotification);
        standardButton.setEnabled (n > 0 || keymap::KeyMap::activeFile().existsAsFile());
    }
    void say (const juce::String& s) { status.setText (s, juce::dontSendNotification); resized(); }

    // ---- the bar
    void pick (int b)
    {
        picked = b;
        list.setSelected (b);
        catcher.clear();
        useButton.setEnabled (false);
        showBar();
        bar.setVisible (true);
        resized(); repaint();
        // the line stays in view above the bar
        if (const int y = list.yOf (b); y >= 0 && (y < viewport.getViewPositionY() || y > viewport.getViewPositionY() + viewport.getViewHeight() - 40))
            viewport.setViewPosition (0, juce::jmax (0, y - 60));
        catcher.grabKeyboardFocus();
    }
    void showBar()
    {
        if (picked < 0) return;
        const auto& b = B (picked);
        barTitle.setText (b.section + ":  " + jobOf (b), juce::dontSendNotification);
        barNow.setText ("Now: " + (b.now.valid() ? keymap::describe (b.now) : juce::String ("no key")) + "      Its standard key: " + keymap::describe (b.def),
                        juce::dontSendNotification);
        ownKeyButton.setButtonText ("Its standard key (" + keymap::describe (b.def) + ")");
        ownKeyButton.setEnabled (b.changed());
        noKeyButton.setEnabled (b.now.valid());
    }
    void closeBar()
    {
        picked = -1;
        list.setSelected (-1);
        bar.setVisible (false);
        resized(); repaint();
        list.grabKeyboardFocus();
    }
    /** Gives a key to a job. Another job of the same window with that key: swapped, or left with none (it asks). The same standard key with the
        same key in other windows (Space plays in most of them): changed there too, or only here (it asks). */
    void applyKey (int bi, keymap::Key nk)
    {
        auto& km = keymap::KeyMap::get();
        const auto b = B (bi);
        if (nk == b.now) { say (b.now.valid() ? keymap::describe (nk) + " already does this." : "This already has no key."); closeBar(); return; }
        int other = -1;
        if (nk.valid())
            for (int i = 0; i < (int) km.bindings().size(); ++i)
                if (i != bi && B (i).section == b.section && B (i).now == nk) { other = i; break; }
        if (other < 0) { thenElsewhere (bi, nk, {}); return; }
        auto* aw = new juce::AlertWindow ("That key is taken",
                                          keymap::describe (nk) + " already does this in the " + b.section + ":\n\n" + jobOf (B (other)) + "\n\nGive it to \"" + jobOf (b) + "\" instead?",
                                          juce::MessageBoxIconType::QuestionIcon, this);
        aw->addButton (b.now.valid() ? "Yes - swap the two keys" : "Yes", 1, juce::KeyPress (juce::KeyPress::returnKey));
        if (b.now.valid()) aw->addButton ("Yes - that one gets no key", 2);
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<HotKeysComponent> safe (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, bi, nk, other, oldKey = b.now] (int res)
        {
            if (safe == nullptr || res == 0) return;
            safe->thenElsewhere (bi, nk, { { other, res == 1 ? oldKey : keymap::Key() } });
        }), true);
    }
    void thenElsewhere (int bi, keymap::Key nk, std::vector<std::pair<int, keymap::Key>> changes)
    {
        auto& km = keymap::KeyMap::get();
        const auto b = B (bi);
        changes.push_back ({ bi, nk });
        std::vector<int> same;
        if (nk.valid() && b.now.valid())
            for (int i = 0; i < (int) km.bindings().size(); ++i)
            {
                const auto& o = B (i);
                if (i == bi || o.section == b.section || o.def != b.def || o.now != b.now) continue;
                if (o.section.startsWith ("fermata:Live: ") != b.section.startsWith ("fermata:Live: ")) continue;      // (each program's windows by themselves)
                same.push_back (i);
            }
        if (same.empty()) { finish (bi, nk, changes, {}); return; }
        // each of them with what the key does there, ticked where it is the same job (the first words of what it does are the same: Space plays
        // in most windows, but L is Loop in some and the room tone in others)
        struct Choices : juce::Component
        {
            juce::OwnedArray<juce::ToggleButton> boxes;
            std::vector<int> index;
            void resized() override { int y = 0; for (auto* t : boxes) { t->setBounds (0, y, getWidth(), 24); y += 26; } }
        };
        auto choices = std::make_shared<Choices>();
        const auto mine = firstWords (b.what[0]);
        for (int i : same)
        {
            const auto& o = B (i);
            auto* t = choices->boxes.add (new juce::ToggleButton (o.section + ":  " + jobOf (o)));
            t->setToggleState (firstWords (o.what[0]) == mine, juce::dontSendNotification);
            t->setColour (juce::ToggleButton::textColourId, theme::text);
            t->setTooltip (o.what.joinIntoString ("\n"));
            choices->addAndMakeVisible (t);
            choices->index.push_back (i);
        }
        choices->setSize (620, (int) same.size() * 26);
        auto* aw = new juce::AlertWindow ("The same key in other windows",
                                          keymap::describe (b.now) + " has a job in " + juce::String ((int) same.size()) + (same.size() == 1 ? " other window" : " other windows")
                                          + " too. Tick those where " + keymap::describe (nk) + " should take its place (ticked: the same job as here):",
                                          juce::MessageBoxIconType::QuestionIcon, this);
        aw->addCustomComponent (choices.get());
        aw->addButton ("Change the ticked ones too", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Only in the " + b.section, 2);
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<HotKeysComponent> safe (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, bi, nk, changes, choices] (int res) mutable
        {
            if (safe == nullptr || res == 0) return;
            juce::StringArray skipped;
            if (res == 1)
                for (size_t n = 0; n < choices->index.size(); ++n)
                {
                    if (! choices->boxes[(int) n]->getToggleState()) continue;
                    const int i = choices->index[n];
                    bool taken = false;
                    for (int j = 0; j < (int) keymap::KeyMap::get().bindings().size(); ++j)
                    {
                        if (B (j).section != B (i).section || j == i) continue;
                        keymap::Key now = B (j).now;
                        for (auto& c : changes) if (c.first == j) now = c.second;
                        taken = taken || now == nk;
                    }
                    if (taken) skipped.add (B (i).section); else changes.push_back ({ i, nk });
                }
            safe->finish (bi, nk, changes, skipped);
        }), true);
    }
    /** The first three words of what a key does, to tell the same job in another window ("loop on off", "play or stop"). */
    static juce::String firstWords (const juce::String& what)
    {
        juce::StringArray w;
        for (auto& t : juce::StringArray::fromTokens (what.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789 "), " ", ""))
            if (t.isNotEmpty() && w.size() < 3) w.add (t);
        return w.joinIntoString (" ");
    }
    void finish (int bi, keymap::Key nk, const std::vector<std::pair<int, keymap::Key>>& changes, const juce::StringArray& skipped)
    {
        auto& km = keymap::KeyMap::get();
        const auto b = B (bi);
        km.assignAll (changes);
        juce::String s = nk.valid() ? keymap::describe (nk) + " now does it" : juce::String ("No key does it now");
        s << " (" << b.section << (changes.size() > 1 ? " and " + juce::String ((int) changes.size() - 1) + " more" : juce::String()) << "). Saved for " << programName() << " and " << otherProgram() << ".";
        if (skipped.size() > 0) s << " Left as it was in " << skipped.joinIntoString (", ") << ": " << keymap::describe (nk) << " does something else there.";
        // a key that does a job in every window (fermata's Everywhere list): in this window it now does this instead
        if (nk.valid() && km.fallbackSection.isNotEmpty() && b.section != km.fallbackSection)
            if (const auto r = km.resolve (km.fallbackSection, nk); r.kind == 1)
                if (const int i = km.indexOf (km.fallbackSection, r.def); i >= 0)
                    s << " In other windows " << keymap::describe (nk) << " still does: " << jobOf (B (i));
        juce::Logger::writeToLog ("Hot keys: " + b.section + " - " + keymap::describe (b.def) + " -> " + (nk.valid() ? keymap::describe (nk) : juce::String ("no key")) + " (" + juce::String ((int) changes.size()) + " changed)");
        closeBar();
        say (s);
    }

    // ---- the files
    void loadKeys()
    {
        keymap::KeyMap::setsFolder().createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Load keys (a file saved with Save keys as...)", keymap::KeyMap::setsFolder(), "*.fermatakeys");
        juce::Component::SafePointer<HotKeysComponent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            juce::String why;
            if (keymap::KeyMap::get().load (fc.getResult(), why))
                safe->say ("Loaded " + fc.getResult().getFileName() + ": " + juce::String (keymap::KeyMap::get().changedCount()) + " keys not the standard ones. " + safe->programName() + " and " + safe->otherProgram() + " use them now.");
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Load keys", "That file could not be used: " + why + ".\n\nThe keys in use are as they were.", "OK", safe.getComponent());
        });
    }
    void saveKeys()
    {
        auto& km = keymap::KeyMap::get();
        keymap::KeyMap::setsFolder().createDirectory();
        auto name = km.setName().upToLastOccurrenceOf (" (changed)", false, false);
        if (name.startsWith ("The standard keys")) name = "My keys";
        chooser = std::make_unique<juce::FileChooser> ("Save keys as (a name for these keys: \"Dave's keys\")", keymap::KeyMap::setsFolder().getChildFile (juce::File::createLegalFileName (name) + ".fermatakeys"), "*.fermatakeys");
        juce::Component::SafePointer<HotKeysComponent> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            auto f = fc.getResult();
            if (! f.hasFileExtension ("fermatakeys")) f = f.withFileExtension ("fermatakeys");
            juce::String why;
            if (keymap::KeyMap::get().saveAs (f, why)) safe->say ("Saved as " + f.getFullPathName() + ". Load keys... in either program, on any computer, uses them.");
            else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save keys as", "The keys could not be saved: " + why + ".", "OK", safe.getComponent());
        });
    }
    void backToStandard()
    {
        auto* aw = new juce::AlertWindow ("Back to the standard keys",
                                          "Every key goes back to the one " + programName() + " and " + otherProgram() + " come with. A file you saved with Save keys as... is left as it is: Load keys... brings it back.",
                                          juce::MessageBoxIconType::QuestionIcon, this);
        aw->addButton ("Yes - the standard keys", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<HotKeysComponent> safe (this);
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int res)
        {
            if (safe == nullptr || res != 1) return;
            keymap::KeyMap::get().resetAll();
            safe->say ("Back to the standard keys.");
        }), true);
    }
};
} // namespace td
