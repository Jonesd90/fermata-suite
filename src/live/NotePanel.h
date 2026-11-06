#pragma once
/** fermata:Live 0.5.0: the note panel - a station's note opened in a box at the side of its take window, to write in as much as is wanted. What is
    typed is kept by itself a moment after the typing stops (and when the panel closes); Done (or Esc) closes it; Delete note takes the note away.
    A note that cannot be changed from here (the Fixer's, from an earlier session: it is in the project, which the Fixer never writes) is shown
    read only, with why. Message thread only. */
#include "../ui/Uikit.h"

namespace td::live
{
class NotePanel final : public juce::Component, private juce::Timer
{
public:
    NotePanel()
    {
        heading.setFont (juce::FontOptions (12.0f, juce::Font::bold)); heading.setColour (juce::Label::textColourId, juce::Colour (0xffffc94d));
        heading.setMinimumHorizontalScale (0.7f);
        why.setFont (juce::FontOptions (11.5f)); why.setColour (juce::Label::textColourId, theme::dimText);
        editor.setMultiLine (true, true); editor.setReturnKeyStartsNewLine (true); editor.setScrollbarsShown (true);
        editor.setFont (juce::FontOptions (15.0f));
        editor.setTextToShowWhenEmpty ("Write the note here (Enter starts a new line; Esc or Done closes it - it is kept as you type; a note left empty goes)", theme::dimText.withAlpha (0.6f));
        editor.onTextChange = [this] { if (editable) startTimer (700); };
        editor.onEscapeKey = [this] { close(); };
        done.onClick = [this] { close(); };
        remove.onClick = [this] { if (onDelete && id.isNotEmpty()) { const auto x = id; stopTimer(); id.clear(); onDelete (x); finish(); } };
        done.setTooltip ("Close the note (what you wrote is kept) [Esc]");
        remove.setTooltip ("Take the note away");
        for (auto* c : std::initializer_list<juce::Component*> { &heading, &why, &editor, &done, &remove }) addAndMakeVisible (c);
        done.setWantsKeyboardFocus (false); remove.setWantsKeyboardFocus (false);
    }
    ~NotePanel() override { stopTimer(); }

    std::function<void (const juce::String& id, const juce::String& text)> onSave;   // a pause in the typing, and closing
    std::function<void (const juce::String& id)> onDelete;
    std::function<void()> onClosed;

    /** Opens a note: where it is and who wrote it (the heading), its text. 'whyNot' non-empty: read only, saying why. */
    void open (const juce::String& noteId, const juce::String& head, const juce::String& text, const juce::String& whyNot = {})
    {
        if (id.isNotEmpty() && id != noteId)
        {
            if (editable && editor.getText().trim().isEmpty() && onDelete) { stopTimer(); const auto x = id; onDelete (x); }   // (an empty note goes)
            else saveNow();
        }
        id = noteId; editable = whyNot.isEmpty();
        heading.setText (head, juce::dontSendNotification);
        why.setText (whyNot, juce::dontSendNotification);
        editor.setText (text, false);
        saved = text;
        editor.setReadOnly (! editable);
        remove.setEnabled (editable);
        setVisible (true);
        resized();
        if (editable) { editor.grabKeyboardFocus(); editor.moveCaretToEnd(); }
    }
    /** Keeps what was typed and closes. A note left with no text at all goes (N pressed by mistake leaves nothing behind). */
    void close()
    {
        if (editable && id.isNotEmpty() && editor.getText().trim().isEmpty() && onDelete)
        {
            stopTimer();
            const auto x = id; id.clear();
            onDelete (x);
            finish();
            return;
        }
        saveNow();
        id.clear();
        finish();
    }
    bool isOpen() const noexcept                { return id.isNotEmpty() && isVisible(); }
    const juce::String& noteId() const noexcept { return id; }
    bool typing() const                         { return editor.hasKeyboardFocus (false); }
    /** The text box (0.6.0: the second seat's keyboard types into it when the seat opened the note). */
    juce::Component& textBox() noexcept         { return editor; }
    /** The heading again (the note's place may change while its take records). */
    void setHeading (const juce::String& head)  { if (heading.getText() != head) heading.setText (head, juce::dontSendNotification); }
    /** The text again, when it changed elsewhere and is not being typed in here. (0.6.0: nor when what was typed here is not kept yet - the second
        seat's keyboard types without the box having the keyboard, and the first letters went before the pause that keeps them.) */
    void refreshText (const juce::String& text)
    {
        if (! typing() && ! isTimerRunning() && editor.getText() == saved && editor.getText() != text) { editor.setText (text, false); saved = text; }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::field.darker (0.15f)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (juce::Colour (0xffffc94d).withAlpha (0.6f)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        heading.setBounds (r.removeFromTop (18));
        if (why.getText().isNotEmpty()) why.setBounds (r.removeFromTop (30)); else why.setBounds ({});
        auto buttons = r.removeFromBottom (26);
        done.setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (6);
        remove.setBounds (buttons.removeFromRight (100));
        r.removeFromBottom (4);
        editor.setBounds (r);
    }

private:
    void timerCallback() override { stopTimer(); saveNow(); }
    void saveNow()
    {
        stopTimer();
        if (editable && id.isNotEmpty() && onSave && editor.getText() != saved) { saved = editor.getText(); onSave (id, saved); }
    }
    void finish()
    {
        setVisible (false);
        if (onClosed) onClosed();
    }

    juce::String id, saved;
    bool editable = true;
    juce::Label heading, why;
    juce::TextEditor editor;
    juce::TextButton done { "Done" }, remove { "Delete note" };
};
} // namespace td::live
