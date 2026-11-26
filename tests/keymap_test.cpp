// fermata 3.69.0 / fermata:Live 0.10.16: tests src/ui/KeyMap.h - the keys that can be changed (HotKeys). Built and run by tools/keymap-check.sh.
// What a key press becomes where the keyboard is, the standard keys read from both programs' lists, and the key file (load, save as, the copy
// in use, another program's entries kept). Nothing here needs a screen: the swapping of the key on its way into a window (the Translator) is
// tried on the Linux build's screen.
#include "../src/ui/KeyMap.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (! (c)) { ++failures; std::printf ("  FAIL line %d: %s\n", __LINE__, #c); } } while (false)
#define SECTION(n) std::printf ("-- %s\n", n)

using namespace td;
using keymap::Key;
using keymap::KeyMap;

static Key K (const char* s) { return keymap::parse (s); }

int main()
{
    // the key file lives in Documents/Fermata: a home of the test's own
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fermata_keymap_test_home");
    home.deleteRecursively(); home.getChildFile ("Documents").createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI gui;
    CHECK (KeyMap::folder().isAChildOf (home));
    if (! KeyMap::folder().isAChildOf (home)) { std::printf ("   (Documents is %s)\n", KeyMap::folder().getFullPathName().toRawUTF8()); return 1; }

    SECTION ("a key written down and read back; a key press as the key map counts it, and the key press made for a key");
    {
        for (const char* s : { "Space", "Ctrl+Shift+Z", "Shift+4", "Num +", "F12", "Alt+F3", "Shift+,", ".", "\\", "[", "Delete", "Page Up", "Ctrl+Alt+Shift+K", "Home" })
            CHECK (keymap::describe (K (s)) == s);
        CHECK (K ("ctrl+shift+z") == K ("Ctrl+Shift+Z") && K ("z") == K ("Z") && K ("Cmd+Z") == K ("Ctrl+Z"));
        for (const char* s : { "click", "Alt+click", "Shift+Alt", "BARS", "double-click", "Shift+wheel", "", "Ctrl+" })
            CHECK (! K (s).valid());
        // as Windows hands them over, and as Linux does (Shift + , as <)
        CHECK (keymap::keyOf (juce::KeyPress (',', juce::ModifierKeys::shiftModifier, '<')) == K ("Shift+,"));
        CHECK (keymap::keyOf (juce::KeyPress ('<', juce::ModifierKeys::shiftModifier, '<')) == K ("Shift+,"));
        CHECK (keymap::keyOf (juce::KeyPress ('4', juce::ModifierKeys::shiftModifier, '$')) == K ("Shift+4"));
        CHECK (keymap::keyOf (juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0)) == K ("Ctrl+Z"));
        CHECK (keymap::keyOf (juce::KeyPress ('r', {}, 'r')) == K ("R"));
        CHECK (keymap::keyOf (juce::KeyPress (juce::KeyPress::F5Key)) == K ("F5"));
        // made for a key: what the windows' own code asks for (KeyPress ==, the typed character, Shift + 4's key code or $)
        CHECK (keymap::pressOf (K ("Space")) == juce::KeyPress (juce::KeyPress::spaceKey));
        CHECK (keymap::pressOf (K ("R")).getTextCharacter() == 'r' && keymap::pressOf (K ("Shift+B")).getTextCharacter() == 'B');
        CHECK (keymap::pressOf (K ("R")) == juce::KeyPress ('r') && keymap::pressOf (K ("Enter")) == juce::KeyPress (juce::KeyPress::returnKey));
        { const auto p = keymap::pressOf (K ("Shift+4")); CHECK (p.getKeyCode() == '4' && p.getTextCharacter() == '$' && p.getModifiers().isShiftDown()); }
        { const auto p = keymap::pressOf (K ("Shift+,")); CHECK (p.getKeyCode() == ',' && p.getTextCharacter() == '<'); }
        { const auto p = keymap::pressOf (K ("Ctrl+Shift+Z")); CHECK (p.getKeyCode() == 'Z' && p.getModifiers().isCommandDown() && p.getModifiers().isShiftDown() && p.getTextCharacter() == 0); }
        { const auto p = keymap::pressOf (K ("Shift+Left")); CHECK (p.getKeyCode() == juce::KeyPress::leftKey && p.getModifiers().isShiftDown()); }
        { const auto p = keymap::pressOf (K (".")); CHECK (p.getTextCharacter() == '.' && p == juce::KeyPress ('.')); }
        for (const char* s : { "Space", "Ctrl+Shift+Z", "Shift+4", "Num +", "F12", "Shift+,", "Alt+X", "Delete", "1", "Shift+Right" })      // a round trip
            CHECK (keymap::keyOf (keymap::pressOf (K (s))) == K (s));
    }

    SECTION ("a line's keys cut into keys and words");
    {
        auto texts = [] (const char* keys) { juce::StringArray a; for (auto& t : keymap::tokensOf (keys)) a.add (t.separator ? "|" + t.text + "|" : t.text); return a.joinIntoString (" "); };
        CHECK (texts ("Ctrl+Y or Ctrl+Shift+Z") == "Ctrl+Y |or| Ctrl+Shift+Z");
        CHECK (texts ("Shift+, / Shift+.") == "Shift+, |/| Shift+.");
        CHECK (texts ("Up / Down, . / ,") == "Up |/| Down |,| . |/| ,");
        CHECK (texts ("C / P / , / .") == "C |/| P |/| , |/| .");
        CHECK (texts (". or = / , or -") == ". |or| = |/| , |or| -");
        CHECK (texts ("Z / \\ / X") == "Z |/| \\ |/| X");
        CHECK (texts ("double-click or Alt+click") == "double-click |or| Alt+click");
        CHECK (texts ("Num +") == "Num +");
    }

    auto& km = KeyMap::get();
    km.fallbackSection = "Everywhere";
    km.setPolled ("Everywhere", K ("R"));
    km.setup();

    SECTION ("the standard keys, from both programs' lists: the windows' own jobs only");
    {
        const auto& B = km.bindings();
        CHECK (B.size() > 120);
        int live = 0; for (auto& b : B) if (b.section.startsWith ("fermata:Live: ")) ++live;
        CHECK (live >= 25);
        for (auto [sec, key] : std::vector<std::pair<const char*, const char*>> {
                 { "Everywhere", "Space" }, { "Everywhere", "R" }, { "Everywhere", "M" }, { "Everywhere", "Ctrl+B" }, { "Everywhere", "Ctrl+Y" }, { "Everywhere", "Ctrl+Shift+Z" },
                 { "Take window", "1" }, { "Take window", "2" }, { "Take window", "Shift+4" }, { "Take window", "Shift+J" }, { "Take window", "Enter" }, { "Take window", "Ctrl+A" },
                 { "Edit window", "Delete" }, { "Edit window", "Backspace" }, { "Edit window", "F1" }, { "Edit window", "Up" }, { "Trim window", "[" }, { "Trim window", "F2" },
                 { "ReTouch, DeClick & Denoise (Spectral Repair)", "Shift+," }, { "ReTouch, DeClick & Denoise (Spectral Repair)", "\\" }, { "Noise mode", "N" },
                 { "Pitch", "R" }, { "ReHarmoniSer", "Ctrl+Z" }, { "ReHarmoniSer", "Enter" }, { "ReTimEr", "O" }, { "Mastering", "Home" }, { "Display Organiser", "Enter" },
                 { "Take Display", "Space" }, { "fermata:Live: the Primary's take window", "Num +" }, { "fermata:Live: the Primary's take window", "=" }, { "fermata:Live: the Fixer", "F" } })
        {
            const bool there = km.indexOf (sec, K (key)) >= 0;
            CHECK (there);
            if (! there) std::printf ("   missing: %s - %s\n", sec, key);
        }
        for (auto [sec, key] : std::vector<std::pair<const char*, const char*>> {
                 { "Edit window", "Esc" }, { "Trim window", "Esc" }, { "Messages and questions", "Enter" }, { "Mixer", "Enter" }, { "Take chart", "Shift+B" },
                 { "Mastering", "Enter" }, { "ReTimEr", "Enter" }, { "Take Display", "Enter" }, { "Plug-in windows", "Space" }, { "Take window", "Tab" } })
        {
            const bool there = km.indexOf (sec, K (key)) >= 0;
            CHECK (! there);
            if (there) std::printf ("   should not be changeable: %s - %s\n", sec, key);
        }
        // the same key on two lines of one window is one job to change (Space plays, and stops a recording)
        const int sp = km.indexOf ("Everywhere", K ("Space"));
        CHECK (sp >= 0 && B[(size_t) sp].what.size() == 2);
        // every standard key of every changeable line is a key the key map can write down and read back
        int keys = 0;
        for (auto* lines : { &hotkeys::fermataLines(), &hotkeys::liveLines() })
            for (auto& l : *lines)
                if (keymap::editableLine (l))
                    for (auto& t : keymap::tokensOf (l.keys))
                        if (! t.separator)
                            if (const auto k = K (t.text.toRawUTF8()); k.valid()) { ++keys; CHECK (keymap::describe (k).equalsIgnoreCase (t.text) || t.text == "Cmd+Z"); }
        CHECK (keys > 150);
        CHECK (km.changedCount() == 0 && km.setName() == "The standard keys" && ! KeyMap::activeFile().exists());
    }

    SECTION ("what a key does where the keyboard is: the window's list first, then every window's; moved keys do nothing; a box gets them as pressed");
    {
        juce::Component takeWin, editWin, mixerWin, plainWin, catcher;
        juce::Component takeChild, mixerChild;
        KeyMap::setSection (takeWin, "Take window"); takeWin.addChildComponent (takeChild);
        KeyMap::setSection (editWin, "Edit window");
        mixerWin.addChildComponent (mixerChild);
        KeyMap::setRaw (catcher);
        km.forwardTarget = [&] (juce::Component* top) -> juce::Component* { return top == &mixerWin ? &takeWin : nullptr; };       // (the mixer hands its keys to the take window)
        auto act = [&] (juce::Component& at, const char* pressed, juce::String* becomes = nullptr)
        {
            juce::KeyPress out;
            const auto a = km.translate (&at, keymap::pressOf (K (pressed)), out);
            if (becomes != nullptr) *becomes = a == KeyMap::Act::use ? keymap::describe (keymap::keyOf (out)) : juce::String();
            return a;
        };
        juce::String to;
        // nothing changed: every key as it is
        CHECK (act (takeChild, "1") == KeyMap::Act::pass && act (editWin, "Space") == KeyMap::Act::pass && act (plainWin, "M") == KeyMap::Act::pass);
        // the take window's mark 1 moves to Q
        km.assign (km.indexOf ("Take window", K ("1")), K ("Q"));
        CHECK (act (takeChild, "Q", &to) == KeyMap::Act::use && to == "1");
        CHECK (act (takeChild, "1") == KeyMap::Act::swallow);                   // (1 does nothing in the take window now)
        CHECK (act (editWin, "1") == KeyMap::Act::pass && act (editWin, "Q") == KeyMap::Act::pass);      // (the edit window's 1 is its own)
        CHECK (act (mixerChild, "Q", &to) == KeyMap::Act::use && to == "1");    // (from the mixer: the take window's keys)
        CHECK (act (catcher, "Q") == KeyMap::Act::pass);                        // (the key catcher: as pressed)
        CHECK (km.label ("Take window", K ("1")) == "Q" && km.changedCount() == 1 && km.setName() == "The standard keys, changed");
        CHECK (KeyMap::activeFile().existsAsFile());                            // (saved at once)
        // a job of every window: the mixer to Ctrl+M - it works from every window; M alone does nothing anywhere (no window has an M of its own)
        km.assign (km.indexOf ("Everywhere", K ("M")), K ("Ctrl+M"));
        CHECK (act (takeChild, "Ctrl+M", &to) == KeyMap::Act::use && to == "M");
        CHECK (act (plainWin, "Ctrl+M", &to) == KeyMap::Act::use && to == "M");
        CHECK (act (plainWin, "M") == KeyMap::Act::swallow && act (editWin, "M") == KeyMap::Act::swallow);
        // R's job from every window to F9: it is watched on the keyboard, so F9 is not handed on as R (the edit window's R butts pieces)
        km.assign (km.indexOf ("Everywhere", K ("R")), K ("F9"));
        CHECK (act (editWin, "F9") == KeyMap::Act::swallow);
        CHECK (act (editWin, "R") == KeyMap::Act::pass);                        // (its own R is still its own)
        CHECK (act (plainWin, "R") == KeyMap::Act::swallow);
        {   const auto r = km.resolveFor (&editWin, K ("R")); CHECK (r.kind == 1 && r.section == "Edit window"); }
        {   const auto r = km.resolveFor (&plainWin, K ("F9")); CHECK (r.kind == 1 && r.section == "Everywhere" && r.def == K ("R")); }
        // a window's own job wins over every window's: the edit window's Delete to Ctrl+M
        km.assign (km.indexOf ("Edit window", K ("Delete")), K ("Ctrl+M"));
        CHECK (act (editWin, "Ctrl+M", &to) == KeyMap::Act::use && to == "Delete");
        CHECK (act (takeChild, "Ctrl+M", &to) == KeyMap::Act::use && to == "M");
        // no key: its standard key does nothing there
        km.assign (km.indexOf ("Edit window", K ("L")), {});
        CHECK (act (editWin, "L") == KeyMap::Act::swallow && km.label ("Edit window", K ("L")) == "no key");
        // nothing in the window has the keyboard: the window's own contents decide
        juce::Component window; window.addChildComponent (editWin);
        CHECK (act (window, "Ctrl+M", &to) == KeyMap::Act::use && to == "Delete");
        window.removeChildComponent (&editWin);
        km.forwardTarget = nullptr;
    }

    SECTION ("the file: the copy in use, Save keys as..., Load keys..., another program's keys kept, back to the standard keys");
    {
        const int changed = km.changedCount();
        CHECK (changed == 5);
        const auto dave = KeyMap::setsFolder().getChildFile ("Dave's keys.fermatakeys");
        juce::String why;
        CHECK (km.saveAs (dave, why) && dave.existsAsFile() && km.setName() == "Dave's keys");
        // what is in it: only the keys that are not the standard ones, by name
        const auto v = juce::JSON::parse (dave.loadFileAsString());
        CHECK (v["fermataHotKeys"].isInt() && v["name"] == "Dave's keys" && v["keys"].getArray() != nullptr && v["keys"].getArray()->size() == changed);
        bool sawQ = false, sawNone = false;
        for (auto& e : *v["keys"].getArray())
        {
            if (e["section"] == "Take window" && e["default"] == "1" && e["key"] == "Q") sawQ = true;
            if (e["section"] == "Edit window" && e["default"] == "L" && e["key"].toString().isEmpty()) sawNone = true;
        }
        CHECK (sawQ && sawNone);
        // a change after Save as: the name says so; the saved file stays as it was
        km.assign (km.indexOf ("Trim window", K ("F2")), K ("F6"));
        CHECK (km.setName() == "Dave's keys (changed)" && juce::JSON::parse (dave.loadFileAsString())["keys"].getArray()->size() == changed);
        // back to the standard keys: the copy goes, the saved file stays
        km.resetAll();
        CHECK (km.changedCount() == 0 && km.setName() == "The standard keys" && ! KeyMap::activeFile().exists() && dave.existsAsFile());
        // Load keys...: Dave's again, as the keys in use (the copy kept)
        CHECK (km.load (dave, why) && km.changedCount() == changed && km.setName() == "Dave's keys" && km.label ("Take window", K ("1")) == "Q");
        CHECK (juce::JSON::parse (KeyMap::activeFile().loadFileAsString())["name"] == "Dave's keys");
        // entries this program does not know (a newer program's window) are kept through a change and a save
        {
            auto w = juce::JSON::parse (dave.loadFileAsString());
            auto* e = new juce::DynamicObject(); e->setProperty ("section", "A window of a later fermata"); e->setProperty ("default", "K"); e->setProperty ("key", "F11");
            w["keys"].getArray()->add (juce::var (e));
            const auto later = KeyMap::setsFolder().getChildFile ("Later.fermatakeys");
            later.replaceWithText (juce::JSON::toString (w));
            CHECK (km.load (later, why));
            km.assign (km.indexOf ("Edit window", K ("C")), K ("F7"));
            bool kept = false;
            const auto inUse = juce::JSON::parse (KeyMap::activeFile().loadFileAsString());
            if (auto* arr = inUse["keys"].getArray()) for (auto& x : *arr) kept = kept || x["section"] == "A window of a later fermata";
            CHECK (kept);
        }
        // a file that is not one: refused, the keys in use as they were
        {
            const auto junk = KeyMap::setsFolder().getChildFile ("junk.fermatakeys");
            junk.replaceWithText ("{ \"something\": 1 }");
            const int before = km.changedCount();
            CHECK (! km.load (junk, why) && why.isNotEmpty() && km.changedCount() == before);
            CHECK (! km.load (KeyMap::setsFolder().getChildFile ("not there.fermatakeys"), why));
        }
        // the other program (or another fermata) changed the copy: read again
        {
            auto w = juce::JSON::parse (dave.loadFileAsString());
            w.getDynamicObject()->setProperty ("name", "Sam's keys");
            juce::Thread::sleep (1100);                                          // (a file system that keeps whole seconds)
            KeyMap::activeFile().replaceWithText (juce::JSON::toString (w));
            km.reloadIfChanged();
            CHECK (km.setName() == "Sam's keys" && km.changedCount() == changed);
            km.reloadIfChanged();                                                // (not changed again: nothing happens)
            CHECK (km.setName() == "Sam's keys");
            KeyMap::activeFile().deleteFile();
            km.reloadIfChanged();
            CHECK (km.changedCount() == 0 && km.setName() == "The standard keys");
        }
        // a fresh start reads the copy in use
        CHECK (km.load (dave, why));
        km.setup();
        CHECK (km.changedCount() == changed && km.setName() == "Dave's keys" && km.label ("Everywhere", K ("M")) == "Ctrl+M");
        km.resetAll();
    }

    home.deleteRecursively();
    std::printf (failures == 0 ? "\nALL KEY MAP TESTS PASSED\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
