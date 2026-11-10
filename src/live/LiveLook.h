#pragma once
/** fermata:Live 0.9.0 (asked for on 10 November 2026): fermata's choice of looks, for a station of fermata:Live.

    - UI style: the Fermata style UI (the blue-grey desk, Inter) or the Manuscript UI (midnight blue and gilt, a Renaissance serif).
    - Colour theme: the desk's six colours (they belong to the Fermata style UI; the Manuscript UI has its own).
    - The sun / moon key in the corner of every window: the light and the dark look. (The key was there from the start - the windows are
      fermata's - but nothing was behind it.)

    All three are fermata's own code (src/ui/ThemeSwitch.h, shared): this file only puts them into a menu and remembers the choice. Each station -
    the Primary, the Fixer - keeps its own, in its own settings, under the names fermata uses ("uiStyle", "deskColour", "deskDark"): the two are
    separate programs, often on two screens for two people. fermata's own choice is fermata's: the programs do not share settings. */
#include "../ui/ThemeSwitch.h"

namespace td::live::look
{
/** At the start, when the look-and-feel is in place and before the first window is made: the look this station had, and the sun / moon keys work
    from here on. 'settings' must outlive every window (it is the station's own settings file). */
inline void install (juce::PropertiesFile& settings)
{
    installThemeSwitch();
    themeSaveHook() = [&settings] (bool dark) { settings.setValue ("deskDark", dark); settings.saveIfNeeded(); };
    if (const int us = settings.getIntValue ("uiStyle", 0); us != 0) applyStyle (us);
    if (const int dc = settings.getIntValue ("deskColour", 0); dc != 0) applyDeskColour (dc);
    if (settings.getBoolValue ("deskDark", false)) switchTheme (true);
}
/** When the station closes (its settings are about to go). */
inline void uninstall() { themeSaveHook() = nullptr; themeSwitchHook() = nullptr; }

/** The menu ids of the two UI styles (styleId and styleId + 1) and of the row of colours. */
constexpr int styleId = 9201, colourId = 9211;

/** Adds "UI style" and the row of colours to a menu. 'picked' is where the row says which colour was clicked (see chosen). */
inline void addToMenu (juce::PopupMenu& m, const std::shared_ptr<int>& picked)
{
    juce::PopupMenu style;
    for (int i = 0; i < 2; ++i) style.addItem (styleId + i, theme::styleName (i), true, theme::styleIndex() == i);
    m.addSubMenu ("UI style", style);
    m.addCustomItem (colourId, std::make_unique<desk::SwatchRow> (desk::themeIndex(), picked), nullptr, "Colour theme");
}

/** The menu's answer: true when it was one of the looks (it has been applied to every window and remembered; the caller redraws whatever it draws
    itself and lays itself out again). */
inline bool chosen (int result, const std::shared_ptr<int>& picked, juce::PropertiesFile& settings)
{
    if (result == styleId || result == styleId + 1)
    {
        const int style = result - styleId;
        if (style != theme::styleIndex()) { applyStyle (style); settings.setValue ("uiStyle", theme::styleIndex()); settings.saveIfNeeded(); }
        return true;
    }
    if (result == colourId && picked != nullptr && *picked >= 0)
    {
        applyDeskColour (*picked);
        settings.setValue ("deskColour", desk::themeIndex()); settings.saveIfNeeded();
        return true;
    }
    return false;
}
} // namespace td::live::look
