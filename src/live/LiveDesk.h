#pragma once
/** fermata:Live 0.3.0: the small parts of fermata's main window that fermata:Live's windows use too - the flat entries of the menu bar, the status
    squares at its right end, the black displays of the top row, the name badge. fermata's own are inside its main window's code (which
    fermata:Live cannot use: it needs the whole of fermata's program), so these are drawn the same way from the shared look (DeskLook.h). */
#include "../ui/DeskLook.h"

namespace td::live
{
/** "fermata:Live" and the mark, at the left of the top row. */
class LiveBadge final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        drawFermataLogo (g, b.removeFromLeft (b.getHeight() * 1.3f).reduced (1.0f, b.getHeight() * 0.12f), desk::pal().cream, juce::Colour (0xffe5484d));
        g.setColour (desk::pal().cream);
        const float h = juce::jmin (24.0f, b.getHeight() * 0.72f);
        g.setFont (juce::Font (juce::FontOptions (theme::wordmarkFontName(), h, juce::Font::bold)));
        g.drawText ("fermata", b.toNearestInt().withTrimmedLeft (5), juce::Justification::centredLeft, false);
        const int w = juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (theme::wordmarkFontName(), h, juce::Font::bold)), "fermata");
        g.setColour (juce::Colour (0xffff6b5a));
        g.setFont (juce::Font (juce::FontOptions (h * 0.78f, juce::Font::bold)));
        g.drawText (":Live", b.toNearestInt().withTrimmedLeft (5 + w + 1).withTrimmedTop ((int) (h * 0.12f)), juce::Justification::centredLeft, false);
    }
};

/** One entry of the menu bar: flat text that lights up under the mouse. */
class MenuEntry final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f, 4.0f);
        if (getToggleState())  { g.setColour (theme::accent); g.fillRoundedRectangle (b, 3.0f); }
        else if (down)         { g.setColour (theme::accent.withAlpha (0.55f)); g.fillRoundedRectangle (b, 3.0f); }
        else if (over)         { g.setColour (desk::pal().menuEdge.brighter (0.25f)); g.fillRoundedRectangle (b, 3.0f); }
        g.setColour (isEnabled() ? juce::Colour (0xffeef2f6) : juce::Colour (0xffeef2f6).withAlpha (0.4f));
        g.setFont (juce::FontOptions (14.0f, getToggleState() ? juce::Font::bold : juce::Font::plain));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
    int wantedWidth() const { return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), getButtonText()) + 26; }
};

/** A status square at the right of the menu bar: dark when off, lit in its own colour when on; it can also be clicked. */
class StatusSquare final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StatusSquare (juce::String text, juce::Colour colour, juce::String tip) : label (std::move (text)), lit (colour) { setTooltip (tip); }
    void set (bool on, juce::Colour c = {}, const juce::String& text = {})
    {
        const auto col = c.isTransparent() ? lit : c;
        const auto t = text.isNotEmpty() ? text : label;
        if (on != isOn || col != shownColour || t != shownText) { isOn = on; shownColour = col; shownText = t; repaint(); }
    }
    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    juce::MouseCursor getMouseCursor() override { return onClick ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor; }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f, 4.0f);
        const auto t = shownText.isNotEmpty() ? shownText : label;
        if (isOn)
        {
            const auto c = shownColour.isTransparent() ? lit : shownColour;
            g.setColour (c); g.fillRoundedRectangle (b, 4.0f);
            g.setColour (c.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
        }
        else
        {
            g.setColour (theme::field); g.fillRoundedRectangle (b, 4.0f);
            g.setColour (theme::border); g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (theme::dimText.withAlpha (0.6f));
        }
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (t, getLocalBounds(), juce::Justification::centred, false);
    }
    int wantedWidth() const
    {
        const auto t = shownText.isNotEmpty() ? shownText : label;
        return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)), t) + 22;
    }
private:
    juce::String label; juce::Colour lit; bool isOn = false;
    juce::Colour shownColour; juce::String shownText;
};

/** A small black display with its caption to the left (the top row: the project, the rate, the disk). */
class InfoDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InfoDisplay (juce::String cap = {}) : caption (std::move (cap)) {}
    void setText (const juce::String& t, juce::Colour c = {}) { if (t != text || c != colour) { text = t; colour = c; repaint(); } }
    std::function<void()> onClick;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void paint (juce::Graphics& g) override
    {
        const auto& p = desk::pal();
        auto r = getLocalBounds();
        if (caption.isNotEmpty())
        {
            g.setColour (p.cream); g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (caption, r.removeFromLeft (captionWidth()), juce::Justification::centredLeft, false);
        }
        g.setColour (p.disp); theme::fillBox (g, r);                 // (fermata 3.73.0's corners)
        g.setColour (p.dispEdge); theme::drawBox (g, r);
        g.setColour (colour.isTransparent() ? p.cyan : colour);
        g.setFont (juce::Font (theme::figureFont (13.0f)));
        g.drawFittedText (text, r.reduced (8, 0), juce::Justification::centredLeft, 1, 0.75f);
    }
    int captionWidth() const { return caption.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)), caption) + 8; }
private:
    juce::String caption, text; juce::Colour colour;
};

/** A caption in the desk's cream ("DRIVER INPUTS"). */
class DeskCaption final : public juce::Component
{
public:
    explicit DeskCaption (juce::String t = {}) : text (std::move (t)) { setInterceptsMouseClicks (false, false); }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
    void paint (juce::Graphics& g) override
    {
        g.setColour (desk::pal().cream); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (text, getLocalBounds(), juce::Justification::centredLeft, true);
    }
private:
    juce::String text;
};
} // namespace td::live
