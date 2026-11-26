#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "HotKeysLines.h"
#include <functional>
#include <map>
#include <vector>

namespace td::keymap
{
/** 3.69.0 (fermata) / 0.10.16 (fermata:Live): the keys can be changed. Asked for on 26 November 2026: the HotKeys list made editable, with a
    save / export, "so multiple users can set and save their preferences"; fermata uses the key file loaded last, keeping a copy in the Fermata
    documents folder (where the log and the crash reports are), and the standard keys when there is none; fermata:Live reads the same file, the
    same way. Shared by both programs (COMPATIBILITY.md).

    HOW IT WORKS - without touching the windows' own key code. Every window still asks for its STANDARD keys (the keys in HotKeysLines.h). A key
    that has been changed is swapped for the standard one on its way in, before any window sees it (Translator, below): press F5 where F5 now
    plays, and the window is handed Space. The standard key itself, moved away, does nothing there. So:
      - a "binding" is one standard key in one window's list (a section of HotKeysLines.h: "Take window", "Edit window" ...); its "now" is the
        key that does that job now (the standard key, another key, or none);
      - which window's list counts is the window the keyboard is in (a component marked with setSection, or one inside it), and after it the
        program's list for every window ("Everywhere" in fermata). A window that hands its keys on (the mixer: they go to the take or edit
        window used last) counts as the window they go to;
      - a box being typed in, a message or a menu (anything modal), and a component marked raw (the HotKeys window's key catcher, a plug-in's
        window) get the keys as they are pressed;
      - keys made inside the program - the Stream Deck's, a control surface's, keys handed from one window to another - are standard keys and
        never pass through here.
    A key the program watches on the keyboard itself (fermata's Space that stops a recording from any window, R that starts one) is watched
    through current(): see AppContext::pollTransportKeys and setPolled. */

// ============================================================================================================== one key
/** A key with its modifiers. Ctrl is the command key (Ctrl on Windows, Cmd on a Mac). code 0: no key. Letters are kept as capitals, and a
    character typed with Shift as the key it is on (Shift + , not <). */
struct Key
{
    int code = 0;
    bool ctrl = false, alt = false, shift = false;
    bool valid() const noexcept { return code != 0; }
    bool operator== (const Key& o) const noexcept { return code == o.code && ctrl == o.ctrl && alt == o.alt && shift == o.shift; }
    bool operator!= (const Key& o) const noexcept { return ! operator== (o); }
    bool operator< (const Key& o) const noexcept
    {
        if (code != o.code) return code < o.code;
        return (ctrl ? 4 : 0) + (alt ? 2 : 0) + (shift ? 1 : 0) < (o.ctrl ? 4 : 0) + (o.alt ? 2 : 0) + (o.shift ? 1 : 0);
    }
};

namespace detail
{
struct Named { const char* name; int code; };
/** The keys with names (the rest are written as the character on them). JUCE's codes differ from one system to another, so the file always
    holds the names. */
inline const std::vector<Named>& named()
{
    static const std::vector<Named> v = []
    {
        using K = juce::KeyPress;
        std::vector<Named> n {
            { "Space", K::spaceKey }, { "Enter", K::returnKey }, { "Esc", K::escapeKey }, { "Tab", K::tabKey }, { "Backspace", K::backspaceKey },
            { "Delete", K::deleteKey }, { "Insert", K::insertKey }, { "Home", K::homeKey }, { "End", K::endKey }, { "Page Up", K::pageUpKey },
            { "Page Down", K::pageDownKey }, { "Left", K::leftKey }, { "Right", K::rightKey }, { "Up", K::upKey }, { "Down", K::downKey },
            { "Num 0", K::numberPad0 }, { "Num 1", K::numberPad1 }, { "Num 2", K::numberPad2 }, { "Num 3", K::numberPad3 }, { "Num 4", K::numberPad4 },
            { "Num 5", K::numberPad5 }, { "Num 6", K::numberPad6 }, { "Num 7", K::numberPad7 }, { "Num 8", K::numberPad8 }, { "Num 9", K::numberPad9 },
            { "Num +", K::numberPadAdd }, { "Num -", K::numberPadSubtract }, { "Num *", K::numberPadMultiply }, { "Num /", K::numberPadDivide },
            { "Num .", K::numberPadDecimalPoint },
            { "F1", K::F1Key }, { "F2", K::F2Key }, { "F3", K::F3Key }, { "F4", K::F4Key }, { "F5", K::F5Key }, { "F6", K::F6Key }, { "F7", K::F7Key },
            { "F8", K::F8Key }, { "F9", K::F9Key }, { "F10", K::F10Key }, { "F11", K::F11Key }, { "F12", K::F12Key }, { "F13", K::F13Key },
            { "F14", K::F14Key }, { "F15", K::F15Key }, { "F16", K::F16Key }, { "F17", K::F17Key }, { "F18", K::F18Key }, { "F19", K::F19Key },
            { "F20", K::F20Key }, { "F21", K::F21Key }, { "F22", K::F22Key }, { "F23", K::F23Key }, { "F24", K::F24Key },
            { "Play", K::playKey }, { "Stop", K::stopKey }, { "Fast forward", K::fastForwardKey }, { "Rewind", K::rewindKey } };
        return n;
    }();
    return v;
}
/** A British keyboard: the character Shift gives on each key (and back). */
inline const std::vector<std::pair<juce::juce_wchar, juce::juce_wchar>>& shifted()
{
    static const std::vector<std::pair<juce::juce_wchar, juce::juce_wchar>> v {
        { '1', '!' }, { '2', '"' }, { '3', (juce::juce_wchar) 0xa3 }, { '4', '$' }, { '5', '%' }, { '6', '^' }, { '7', '&' }, { '8', '*' }, { '9', '(' },
        { '0', ')' }, { '-', '_' }, { '=', '+' }, { '[', '{' }, { ']', '}' }, { ';', ':' }, { '\'', '@' }, { '#', '~' }, { ',', '<' }, { '.', '>' },
        { '/', '?' }, { '\\', '|' }, { '`', (juce::juce_wchar) 0xac } };
    return v;
}
inline juce::juce_wchar baseOf (juce::juce_wchar c) { for (auto& p : shifted()) if (p.second == c) return p.first; return 0; }
inline juce::juce_wchar shiftOf (juce::juce_wchar c) { for (auto& p : shifted()) if (p.first == c) return p.second; return 0; }
inline bool isLetter (int c) { return c >= 'A' && c <= 'Z'; }
inline bool isPrintable (int c) { return c > 32 && c != 127 && c < 0x10000 && ! (c >= 0x7f && c < 0xa0); }
inline const char* nameOf (int code) { for (auto& n : named()) if (n.code == code) return n.name; return nullptr; }
} // namespace detail

/** The key of a key press, as the key map counts it. */
inline Key keyOf (const juce::KeyPress& k)
{
    Key r;
    const auto m = k.getModifiers();
    r.ctrl = m.isCommandDown(); r.alt = m.isAltDown(); r.shift = m.isShiftDown();
    int c = k.getKeyCode();
    if (c == 0) c = (int) k.getTextCharacter();
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (r.shift && detail::nameOf (c) == nullptr)
        if (const auto b = detail::baseOf ((juce::juce_wchar) c)) c = (int) b;              // (Linux hands Shift + , over as <; Windows as ,)
    r.code = c;
    return r;
}

/** A key press for a key, as the keyboard would make it: Windows' key code, the modifiers, and the character it types. */
inline juce::KeyPress pressOf (const Key& k)
{
    int mods = 0;
    if (k.ctrl) mods |= juce::ModifierKeys::commandModifier;
    if (k.alt) mods |= juce::ModifierKeys::altModifier;
    if (k.shift) mods |= juce::ModifierKeys::shiftModifier;
    juce::juce_wchar ch = 0;
    if (! k.ctrl && ! k.alt)
    {
        if (detail::isLetter (k.code)) ch = k.shift ? (juce::juce_wchar) k.code : (juce::juce_wchar) (k.code + ('a' - 'A'));
        else if (k.code == juce::KeyPress::spaceKey) ch = ' ';
        else if (k.code == juce::KeyPress::returnKey) ch = '\r';
        else if (k.code >= juce::KeyPress::numberPad0 && k.code <= juce::KeyPress::numberPad9) ch = (juce::juce_wchar) ('0' + (k.code - juce::KeyPress::numberPad0));
        else if (detail::nameOf (k.code) == nullptr && detail::isPrintable (k.code))
            ch = k.shift ? (detail::shiftOf ((juce::juce_wchar) k.code) != 0 ? detail::shiftOf ((juce::juce_wchar) k.code) : (juce::juce_wchar) k.code) : (juce::juce_wchar) k.code;
    }
    return juce::KeyPress (k.code, juce::ModifierKeys (mods), ch);
}

/** The name of the key alone ("Z", "Space", "Num +", ","). */
inline juce::String keyName (int code)
{
    if (auto* n = detail::nameOf (code)) return n;
    if (detail::isPrintable (code)) return juce::String::charToString ((juce::juce_wchar) code);
    return "key " + juce::String (code);
}
/** "Ctrl+Shift+Z", as the HotKeys list writes keys (and the file holds them). Empty for no key. */
inline juce::String describe (const Key& k)
{
    if (! k.valid()) return {};
    juce::String s;
    if (k.ctrl) s << "Ctrl+";
    if (k.alt) s << "Alt+";
    if (k.shift) s << "Shift+";
    return s + keyName (k.code);
}
/** The other way: "Ctrl+Shift+Z" -> the key. No key (code 0) for anything that is not one: the mouse, "Shift+Alt", a button's name. */
inline Key parse (const juce::String& text)
{
    Key k;
    auto s = text.trim();
    for (bool more = true; more && s.length() > 1;)
    {
        more = false;
        for (const char* m : { "Ctrl+", "Cmd+", "Alt+", "Shift+" })
            if (const juce::String mm (m); s.startsWithIgnoreCase (mm) && s.length() > mm.length())
            {
                if (mm == "Ctrl+" || mm == "Cmd+") k.ctrl = true; else if (mm == "Alt+") k.alt = true; else k.shift = true;
                s = s.substring (mm.length()); more = true;
            }
    }
    for (auto& n : detail::named()) if (s.equalsIgnoreCase (n.name)) { k.code = n.code; return k; }
    if (s.length() == 1)
    {
        int c = (int) s[0];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (detail::isPrintable (c)) { k.code = c; return k; }
    }
    return {};
}

// ============================================================================================================== the lines' keys
/** A line's keys, cut into its keys and the words between them ("or", "/", ","): see HotKeysLines.h. */
struct Token { juce::String text; bool separator = false; };
inline std::vector<Token> tokensOf (const juce::String& keys)
{
    std::vector<Token> out;
    juce::String cur;
    auto flush = [&] { if (cur.trim().isNotEmpty()) out.push_back ({ cur.trim(), false }); cur.clear(); };
    const int n = keys.length();
    for (int i = 0; i < n;)
    {
        if (keys.substring (i).startsWith (" or ")) { flush(); out.push_back ({ "or", true }); i += 4; continue; }
        if (keys.substring (i).startsWith (" / ")) { flush(); out.push_back ({ "/", true }); i += 3; continue; }
        // ", " between two groups ("Up / Down, . / ,") - not the comma key itself (after "+", or standing alone between spaces)
        if (keys[i] == ',' && i + 1 < n && keys[i + 1] == ' ' && i > 0 && keys[i - 1] != '+' && keys[i - 1] != ' ')
        { flush(); out.push_back ({ ",", true }); i += 2; continue; }
        cur += keys[i]; ++i;
    }
    flush();
    return out;
}

/** The sections whose keys can be changed: the windows' own jobs. (Not the questions, the boxes for typing, the mixer's - all mouse and boxes -
    or the take chart and plug-in windows, which show the keys of the take window and of every window: see displaySection.) */
inline bool editableSection (const juce::String& s)
{
    static const juce::StringArray fermata { "Everywhere", "Take window", "Edit window", "Trim window", "ReTouch, DeClick & Denoise (Spectral Repair)",
                                             "Noise mode", "Pitch", "ReHarmoniSer", "ReTimEr", "Mastering", "Display Organiser", "Take Display" };
    return fermata.contains (s) || s.startsWith ("fermata:Live: ");
}
/** A line's keys can be changed: a window's own job - not a box's keys ("In the ... box: ..."). */
inline bool editableLine (const hotkeys::Line& l)
{
    return editableSection (l.section) && juce::String (l.keys).isNotEmpty() && ! juce::String (l.what).startsWith ("In ");
}
/** A key in such a line that can be changed: never Esc or Tab (they always close and move on), never the mouse. */
inline bool editableKey (const Key& k) { return k.valid() && k.code != juce::KeyPress::escapeKey && k.code != juce::KeyPress::tabKey; }
/** Whose keys a section's lines show: the take chart's are the take window's, the plug-in windows' those of every window. */
inline juce::String displaySection (const juce::String& s)
{
    if (s == "Take chart") return "Take window";
    if (s == "Plug-in windows") return "Everywhere";
    return s;
}

// ============================================================================================================== the key map
struct Binding
{
    juce::String section;             // whose list ("Take window")
    Key def;                          // the standard key
    Key now;                          // the key that does it now (invalid: none)
    juce::StringArray what;           // what it does (the lines it is in)
    bool changed() const { return now != def; }
};

/** The keys of both programs: the standard ones (HotKeysLines.h) and the changes, kept in the file. Message thread only. */
class KeyMap : public juce::ChangeBroadcaster
{
public:
    static KeyMap& get() { static KeyMap m; return m; }

    /** The program's settings: the list of every window (fermata: "Everywhere"; fermata:Live: none), and where the keys of a window that hands
        them on go (fermata: the take or edit window used last), as the content of the window they go to. */
    juce::String fallbackSection;
    std::function<juce::Component* (juce::Component* topLevel)> forwardTarget;

    static constexpr const char* kSectionProperty = "fermataKeysSection";
    static constexpr const char* kRawProperty = "fermataKeysRaw";
    /** Marks a window's contents as using a section's keys (its constructor). */
    static void setSection (juce::Component& c, const juce::String& section) { c.getProperties().set (kSectionProperty, section); }
    /** Marks a component that wants the keys as they are pressed (the key catcher; a plug-in's own window). */
    static void setRaw (juce::Component& c) { c.getProperties().set (kRawProperty, true); }

    const std::vector<Binding>& bindings() const { return list; }
    int indexOf (const juce::String& section, const Key& def) const
    {
        if (auto it = byKey.find ({ section, def }); it != byKey.end()) return it->second;
        return -1;
    }
    /** The key that does a standard key's job in a section now (the standard key when nothing is known of it). */
    Key current (const juce::String& section, const Key& def) const
    {
        const int i = indexOf (section, def);
        return i >= 0 ? list[(size_t) i].now : def;
    }
    /** As the HotKeys list and the windows' labels write it ("F5"; "no key"). */
    juce::String label (const juce::String& section, const Key& def) const { const auto k = current (section, def); return k.valid() ? describe (k) : juce::String ("no key"); }

    // ---- what a key does where the keyboard is
    struct Resolved { int kind = 0; Key def; juce::String section; };      // kind 0: not this list's; 1: it does def's job; 2: its job has moved (it does nothing)
    Resolved resolve (const juce::String& section, const Key& pressed) const
    {
        Resolved r;
        auto it = bySection.find (section);
        if (it == bySection.end()) return r;
        for (int i : it->second) if (list[(size_t) i].now == pressed) { r.kind = 1; r.def = list[(size_t) i].def; r.section = section; return r; }
        for (int i : it->second) if (list[(size_t) i].def == pressed) { r.kind = 2; r.def = pressed; r.section = section; return r; }
        return r;
    }
    /** The lists that count for a component that has the keyboard, the first first. {"@raw"}: it gets the keys as they are. */
    juce::StringArray contextOf (juce::Component* c) const
    {
        juce::StringArray out;
        if (c == nullptr) return out;
        for (auto* p = c; p != nullptr; p = p->getParentComponent())
        {
            if (p->getProperties().contains (kRawProperty)) return { "@raw" };
            if (p->getProperties().contains (kSectionProperty)) { out.add (p->getProperties()[kSectionProperty].toString()); break; }
        }
        if (out.isEmpty())
            if (auto* sec = markedWithin (c, 0)) out.add (sec->getProperties()[kSectionProperty].toString());      // (nothing in the window has the keyboard)
        if (out.isEmpty() && forwardTarget)
            if (auto* top = c->getTopLevelComponent())
                if (auto* to = forwardTarget (top))
                {
                    if (to->getProperties().contains (kSectionProperty)) out.add (to->getProperties()[kSectionProperty].toString());
                    else if (auto* sec = markedWithin (to, 0)) out.add (sec->getProperties()[kSectionProperty].toString());
                }
        if (fallbackSection.isNotEmpty()) out.addIfNotAlreadyThere (fallbackSection);
        return out;
    }
    /** The first list that knows the key, for a component that has the keyboard (kind 0: none does). */
    Resolved resolveFor (juce::Component* c, const Key& pressed) const
    {
        const auto ctx = contextOf (c);
        if (ctx.contains ("@raw")) return {};
        for (auto& s : ctx) if (const auto r = resolve (s, pressed); r.kind != 0) return r;
        return {};
    }
    /** A job the program does by watching the keyboard itself (fermata: R, which starts a recording from any window): when another key does it,
        that key is not handed on as the standard key - the watcher sees it - but kept from the windows. */
    void setPolled (const juce::String& section, const Key& def) { polled.push_back ({ section, def }); }

    enum class Act { pass, use, swallow };
    /** What to do with a key press for a component that has the keyboard: hand it on as it is, hand 'out' on instead, or drop it. */
    Act translate (juce::Component* c, const juce::KeyPress& in, juce::KeyPress& out) const
    {
        if (list.empty() || c == nullptr) return Act::pass;
        const auto pressed = keyOf (in);
        if (! pressed.valid()) return Act::pass;
        const auto r = resolveFor (c, pressed);
        if (r.kind == 0) return Act::pass;
        if (r.kind == 2) return Act::swallow;
        if (r.def == pressed) return Act::pass;
        for (auto& p : polled) if (p.first == r.section && p.second == r.def) return Act::swallow;
        out = pressOf (r.def);
        return Act::use;
    }
    /** The key is down now, with exactly its modifiers (for a job watched on the keyboard). */
    static bool isDown (const Key& k)
    {
        if (! k.valid() || ! juce::KeyPress::isKeyCurrentlyDown (k.code)) return false;
        const auto m = juce::ModifierKeys::getCurrentModifiersRealtime();
        return m.isCommandDown() == k.ctrl && m.isAltDown() == k.alt && m.isShiftDown() == k.shift;
    }

    // ---- changing
    /** Gives a binding a key (invalid: none). Saved at once. */
    void assign (int index, const Key& k)
    {
        if (! juce::isPositiveAndBelow (index, (int) list.size())) return;
        list[(size_t) index].now = k;
        noteChanged();
    }
    /** Several at once (a swap; the same key in every window), saved once. */
    void assignAll (const std::vector<std::pair<int, Key>>& changes)
    {
        for (auto& c : changes) if (juce::isPositiveAndBelow (c.first, (int) list.size())) list[(size_t) c.first].now = c.second;
        noteChanged();
    }
    int changedCount() const { int n = 0; for (auto& b : list) if (b.changed()) ++n; return n; }
    /** The name of the keys in use: the file loaded last ("Dave's keys"), "(changed)" after a change; else the standard keys. */
    juce::String setName() const { return name.isNotEmpty() ? name : (changedCount() > 0 ? juce::String ("The standard keys, changed") : juce::String ("The standard keys")); }

    // ---- the files
    static juce::File folder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fermata"); }
    /** The copy of the keys in use (both programs read it at the start and whenever it changes). */
    static juce::File activeFile() { return folder().getChildFile ("Hot keys in use.fermatakeys"); }
    /** Where Save keys as... and Load keys... start. */
    static juce::File setsFolder() { return folder().getChildFile ("Hot Keys"); }

    /** Load keys...: a file saved by Save keys as... (from either program, on any computer) becomes the keys in use; its copy is kept. */
    bool load (const juce::File& f, juce::String& why)
    {
        Parsed p;
        if (! read (f, p, why)) return false;
        apply (p);
        name = f.getFileNameWithoutExtension();
        if (! writeActive (why)) return false;
        sendChangeMessage();
        juce::Logger::writeToLog ("Hot keys: loaded " + f.getFullPathName() + " (" + juce::String (changedCount()) + " keys not the standard ones)");
        return true;
    }
    /** Save keys as...: the keys in use, to a file of their own (named after it from now on). */
    bool saveAs (const juce::File& f, juce::String& why)
    {
        if (! writeTo (f, f.getFileNameWithoutExtension(), why)) return false;
        name = f.getFileNameWithoutExtension();
        writeActive (why);
        sendChangeMessage();
        juce::Logger::writeToLog ("Hot keys: saved as " + f.getFullPathName());
        return true;
    }
    /** Back to the standard keys: the copy in use goes (a file saved with Save keys as... is left alone). */
    void resetAll()
    {
        for (auto& b : list) b.now = b.def;
        foreign.clear(); name.clear();
        activeFile().deleteFile();
        noteFile();
        sendChangeMessage();
        juce::Logger::writeToLog ("Hot keys: back to the standard keys");
    }
    /** Both programs' keys (each program calls it once, at the start, with both lists), then the keys in use read from the copy. */
    void setup()
    {
        list.clear(); byKey.clear(); bySection.clear();
        addLines (hotkeys::fermataLines());
        addLines (hotkeys::liveLines());
        juce::String why;
        Parsed p;
        if (activeFile().existsAsFile())
        {
            if (read (activeFile(), p, why)) { apply (p); name = p.name; }
            else juce::Logger::writeToLog ("Hot keys: " + activeFile().getFullPathName() + " could not be read (" + why + "): the standard keys");
        }
        noteFile();
        juce::Logger::writeToLog ("Hot keys: " + setName() + (changedCount() > 0 ? " (" + juce::String (changedCount()) + " not the standard ones)" : juce::String()));
    }
    /** The copy changed under us (the other program, or another fermata, saved it): read it again. Called every few seconds. */
    void reloadIfChanged()
    {
        const auto f = activeFile();
        const auto stamp = f.existsAsFile() ? f.getLastModificationTime().toMilliseconds() ^ f.getSize() : (juce::int64) 0;
        if (stamp == fileStamp) return;
        fileStamp = stamp;
        for (auto& b : list) b.now = b.def;
        foreign.clear(); name.clear();
        juce::String why; Parsed p;
        if (f.existsAsFile() && read (f, p, why)) { apply (p); name = p.name; }
        sendChangeMessage();
        juce::Logger::writeToLog ("Hot keys: the keys in use changed (" + setName() + ")");
    }

private:
    KeyMap() = default;
    std::vector<Binding> list;
    std::map<std::pair<juce::String, Key>, int> byKey;
    std::map<juce::String, std::vector<int>> bySection;
    std::vector<std::pair<juce::String, Key>> polled;
    juce::Array<juce::var> foreign;          // entries of the file this program does not know (a newer program's): kept when it saves
    juce::String name;
    juce::int64 fileStamp = 0;

    static juce::Component* markedWithin (juce::Component* c, int depth)
    {
        if (c->getProperties().contains (kSectionProperty)) return c;
        if (depth >= 2) return nullptr;
        for (auto* ch : c->getChildren()) if (auto* m = markedWithin (ch, depth + 1)) return m;
        return nullptr;
    }
    void addLines (const std::vector<hotkeys::Line>& lines)
    {
        for (auto& l : lines)
        {
            if (! editableLine (l)) continue;
            for (auto& t : tokensOf (l.keys))
            {
                if (t.separator) continue;
                const auto k = parse (t.text);
                if (! editableKey (k)) continue;
                const juce::String sec (l.section);
                int i = indexOf (sec, k);
                if (i < 0)
                {
                    i = (int) list.size();
                    list.push_back ({ sec, k, k, {} });
                    byKey[{ sec, k }] = i;
                    bySection[sec].push_back (i);
                }
                list[(size_t) i].what.addIfNotAlreadyThere (l.what);
            }
        }
    }
    struct Parsed { juce::String name; std::vector<std::pair<int, Key>> keys; juce::Array<juce::var> foreign; };
    bool read (const juce::File& f, Parsed& p, juce::String& why) const
    {
        if (! f.existsAsFile()) { why = "the file is not there"; return false; }
        const auto v = juce::JSON::parse (f.loadFileAsString());
        if (! v.isObject() || ! v.hasProperty ("fermataHotKeys")) { why = "it is not a hot keys file of fermata's"; return false; }
        p.name = v["name"].toString();
        if (auto* arr = v["keys"].getArray())
            for (auto& e : *arr)
            {
                const auto sec = e["section"].toString();
                const auto def = parse (e["default"].toString());
                const auto now = e["key"].toString().trim();
                const int i = def.valid() ? indexOf (sec, def) : -1;
                if (i < 0) { p.foreign.add (e); continue; }
                p.keys.push_back ({ i, now.isEmpty() ? Key() : parse (now) });
            }
        return true;
    }
    void apply (const Parsed& p)
    {
        for (auto& b : list) b.now = b.def;
        for (auto& k : p.keys) list[(size_t) k.first].now = k.second;
        foreign = p.foreign;
    }
    bool writeTo (const juce::File& f, const juce::String& setName, juce::String& why) const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("fermataHotKeys", 1);
        root->setProperty ("name", setName);
        root->setProperty ("saved", juce::Time::getCurrentTime().toISO8601 (true));
        root->setProperty ("note", "The keys that are not the standard ones, for fermata and fermata:Live. Load it in either program's HotKeys window (Help menu).");
        juce::Array<juce::var> keys;
        for (auto& b : list)
        {
            if (! b.changed()) continue;
            auto* e = new juce::DynamicObject();
            e->setProperty ("section", b.section);
            e->setProperty ("default", describe (b.def));
            e->setProperty ("key", describe (b.now));                    // ("": no key)
            keys.add (juce::var (e));
        }
        keys.addArray (foreign);
        root->setProperty ("keys", keys);
        f.getParentDirectory().createDirectory();
        const auto tmp = f.getSiblingFile (f.getFileName() + ".saving");
        if (! tmp.replaceWithText (juce::JSON::toString (juce::var (root), false)) || ! tmp.moveFileTo (f))
        {
            tmp.deleteFile();
            why = "it could not be written: " + f.getFullPathName();
            return false;
        }
        return true;
    }
    bool writeActive (juce::String& why)
    {
        const bool ok = writeTo (activeFile(), name, why);
        noteFile();
        return ok;
    }
    void noteFile()
    {
        const auto f = activeFile();
        fileStamp = f.existsAsFile() ? f.getLastModificationTime().toMilliseconds() ^ f.getSize() : (juce::int64) 0;
    }
    void noteChanged()
    {
        if (name.isNotEmpty() && ! name.endsWith (" (changed)")) name << " (changed)";
        juce::String why;
        if (changedCount() == 0 && foreign.isEmpty() && name.isEmpty()) { activeFile().deleteFile(); noteFile(); }
        else if (! writeActive (why)) juce::Logger::writeToLog ("Hot keys: " + why);
        sendChangeMessage();
    }
};

// ============================================================================================================== the translator
/** Swaps a changed key for the standard one on its way into the windows (see the top of this file). One for the program, made at the start
    (install) and taken down at the end. It listens to the component that has the keyboard - JUCE asks a component's key listeners before the
    component itself - and to every window (for when nothing in it has the keyboard). */
class Translator : private juce::FocusChangeListener, private juce::KeyListener, private juce::Timer
{
public:
    static Translator& get() { static Translator t; return t; }
    void install()
    {
        if (installed) return;
        installed = true;
        juce::Desktop::getInstance().addFocusChangeListener (this);
        globalFocusChanged (juce::Component::getCurrentlyFocusedComponent());
        startTimer (1000);
    }
    void uninstall()
    {
        if (! installed) return;
        installed = false;
        stopTimer();
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        if (auto* a = attached.getComponent()) a->removeKeyListener (this);
        for (auto& w : windows) if (auto* c = w.getComponent()) c->removeKeyListener (this);
        windows.clear();
    }
    /** For keys handed to a window by the program itself from a real keyboard (fermata:Live's second seat): what the window should get.
        False: nothing (the key's job has moved). */
    bool translateFor (juce::Component* target, const juce::KeyPress& in, juce::KeyPress& out)
    {
        out = in;
        if (target == nullptr || typing (target)) return true;
        juce::KeyPress swapped;
        switch (KeyMap::get().translate (target, in, swapped))
        {
            case KeyMap::Act::pass: return true;
            case KeyMap::Act::use: out = swapped; return true;
            case KeyMap::Act::swallow: return false;
        }
        return true;
    }

private:
    Translator() = default;
    bool installed = false, injecting = false;
    juce::Component::SafePointer<juce::Component> attached;
    std::vector<juce::Component::SafePointer<juce::Component>> windows;
    int ticks = 0;

    void globalFocusChanged (juce::Component* c) override
    {
        if (attached.getComponent() == c) return;
        if (auto* a = attached.getComponent())
        {
            bool isWindow = false;
            for (auto& w : windows) isWindow = isWindow || w.getComponent() == a;
            if (! isWindow) a->removeKeyListener (this);
        }
        attached = c;
        if (c != nullptr) c->addKeyListener (this);
    }
    void timerCallback() override
    {
        // every window on the screen gets the listener too (a new one within a second; it is asked when nothing inside it has the keyboard)
        windows.erase (std::remove_if (windows.begin(), windows.end(), [] (auto& w) { return w.getComponent() == nullptr; }), windows.end());
        auto& d = juce::Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
            if (auto* c = d.getComponent (i))
            {
                bool known = false;
                for (auto& w : windows) known = known || w.getComponent() == c;
                if (! known) { c->addKeyListener (this); windows.emplace_back (c); }
            }
        if (++ticks >= 2) { ticks = 0; KeyMap::get().reloadIfChanged(); }         // (the other program changed the keys)
    }
    static bool typing (juce::Component* c)
    {
        if (auto* t = dynamic_cast<juce::TextInputTarget*> (c)) if (t->isTextInputActive()) return true;
        return false;
    }
    bool keyPressed (const juce::KeyPress& key, juce::Component* origin) override
    {
        if (injecting || origin == nullptr) return false;
        auto* focus = juce::Component::getCurrentlyFocusedComponent();
        auto* c = focus != nullptr && focus->getTopLevelComponent() == origin->getTopLevelComponent() ? focus : origin;
        if (typing (c)) return false;
        for (auto* p = c; p != nullptr; p = p->getParentComponent()) if (p->isCurrentlyModal (false)) return false;     // a message, a menu: as pressed
        juce::KeyPress out;
        switch (KeyMap::get().translate (c, key, out))
        {
            case KeyMap::Act::pass: return false;
            case KeyMap::Act::swallow: return true;
            case KeyMap::Act::use: break;
        }
        auto* peer = origin->getPeer();
        if (peer == nullptr) return false;
        const juce::ScopedValueSetter<bool> s (injecting, true);
        peer->handleKeyPress (out);               // (from the top again, as if the standard key had been pressed)
        return true;
    }
};
} // namespace td::keymap
