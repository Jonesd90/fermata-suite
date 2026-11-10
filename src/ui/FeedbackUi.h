#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "MarkerStrip.h"
#include "../core/Edit.h"
#include "../core/Feedback.h"
#include "../core/TimeFormat.h"

namespace td
{
/** Listeners' comments drawn on an edit window: a flag in the top strip for every moment someone commented (several comments at one moment are one flag, "+2" for the replies).
    Click a flag to read the thread; tick comments off as done; the flag goes grey when all of them are.

    3.51.0: every person has a colour of their own (feedback::personArgb), and the editor - the person at fermata - can answer a comment or
    write a note of their own: those are drawn with a gold ring, here and in the next listening copy, which carries the whole conversation. */
namespace feedbackui
{
inline juce::Font flagFont() { return juce::Font (juce::FontOptions (11.0f, juce::Font::bold)); }

inline juce::String flagText (const std::vector<FeedbackNote>& list, const std::vector<int>& thread)
{
    const auto& first = list[(size_t) thread.front()];
    juce::String t = first.bar.isNotEmpty() ? first.bar : timefmt::minSecText (first.seconds);
    if (thread.size() > 1) t += "  +" + juce::String ((int) thread.size() - 1);
    return t;
}
inline int flagWidth (const juce::String& text) { return juce::jlimit (26, 150, juce::GlyphArrangement::getStringWidthInt (flagFont(), text) + 16); }

/** Every listener's comment of the thread is ticked. (The editor's own notes are not something to tick off: a thread of nothing else is never "done".) */
inline bool allDone (const std::vector<FeedbackNote>& list, const std::vector<int>& thread)
{
    bool any = false;
    for (int i : thread) { const auto& n = list[(size_t) i]; if (n.editor) continue; any = true; if (! n.done) return false; }
    return any;
}
inline bool hasEditorNote (const std::vector<FeedbackNote>& list, const std::vector<int>& thread)
{
    for (int i : thread) if (list[(size_t) i].editor) return true;
    return false;
}
inline juce::Colour personColour (const FeedbackNote& n) { return juce::Colour (feedback::personArgb (n)); }
inline juce::Colour editorGold() { return juce::Colour (feedback::kEditorGold); }
/** The gold ring of the editor's notes, round a flag. */
inline void paintEditorRing (juce::Graphics& g, juce::Rectangle<float> flag)
{
    g.setColour (editorGold()); g.drawRoundedRectangle (flag.reduced (1.0f), 2.5f, 2.0f);
}

/** What the editor's notes need from the window: the name they are signed with (kept in the program's settings) and a window to open boxes over. */
struct EditorHand
{
    std::function<juce::String()> name;
    std::function<void (const juce::String&)> setName;
    juce::Component::SafePointer<juce::Component> over;
    int version = 0;                                                       // the version of the edit being worked on now (FeedbackNote::madeIn of what is written)
};

/** 3.52.0 (asked for on 10 November 2026): in the box a reply is written in, Enter is "Add" and Shift + Enter starts a new line. */
struct ReplyEditor : public juce::TextEditor
{
    std::function<void()> onEnter;
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::returnKey && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown())
        {
            if (k.getModifiers().isShiftDown()) insertTextAtCaret ("\n");
            else if (onEnter) onEnter();
            return true;
        }
        return juce::TextEditor::keyPressed (k);
    }
};

/** The box a note of the editor's is written in: the words, and the name it is signed with. 'done' gets both when "Add" (or "Save") is pressed with
    some words in the box. */
inline void askEditorNote (const juce::String& title, const juce::String& about, const juce::String& text, const EditorHand& hand, bool changing,
                           std::function<void (const juce::String& who, const juce::String& text)> done)
{
    auto* w = new juce::AlertWindow (title, about + (about.isNotEmpty() ? "\n" : "") + "Enter adds it; Shift + Enter starts a new line.", juce::MessageBoxIconType::NoIcon, hand.over.getComponent());
    auto* words = new ReplyEditor();
    words->setMultiLine (true, true); words->setReturnKeyStartsNewLine (false); words->setScrollbarsShown (true);
    words->onEnter = [w, words] { if (words->getText().trim().isNotEmpty()) w->exitModalState (1); };       // (Enter with nothing written does nothing)
    words->setFont (juce::FontOptions (14.0f)); words->setText (text, false); words->setSize (440, 130);
    words->setTextToShowWhenEmpty ("What you want them to read", juce::Colours::grey);
    w->addCustomComponent (words);
    w->addTextEditor ("who", hand.name ? hand.name() : juce::String(), "Your name (shown with your notes):");
    w->addButton (changing ? "Save" : "Add", 1, juce::KeyPress (juce::KeyPress::returnKey));      // (the positive action on the left, Cancel on the right; Enter in the name box presses it too)
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::Component> focus (words);
    juce::MessageManager::callAsync ([focus] { if (focus != nullptr) focus->grabKeyboardFocus(); });
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, words, hand, done] (int r)
    {
        words->onEnter = nullptr;
        const auto t = words->getText().trim();
        std::unique_ptr<juce::Component> ownWords (w->removeCustomComponent (0));       // (a custom component is the caller's to delete)
        auto who = w->getTextEditorContents ("who").trim();
        if (r != 1 || t.isEmpty()) return;
        if (who.isEmpty()) who = "Editor";
        if (hand.setName) hand.setName (who);
        if (done) done (who, t);
    }), true);
}

inline void paintFlags (juce::Graphics& g, const std::vector<FeedbackNote>& list, const std::function<int (double)>& xOf, int stripY, int leftLimit, int totalHeight, const juce::String& selectedKey)
{
    const int h = markerui::kStripH;
    g.setFont (flagFont());
    for (auto& th : feedback::threads (list))
    {
        const auto& first = list[(size_t) th.front()];
        const int x = xOf (first.seconds);
        if (x < leftLimit) continue;
        const bool done = allDone (list, th), sel = first.key == selectedKey;
        const auto body = done ? juce::Colour (0xff6d7078) : personColour (first);          // (3.51.0: the person's colour; it was teal for everyone, with the website's colour as a stripe)
        g.setColour (body.withAlpha (sel ? 0.9f : 0.4f));
        g.fillRect (x, stripY + h, sel ? 2 : 1, juce::jmax (0, totalHeight - stripY - h));
        const auto text = flagText (list, th);
        auto f = juce::Rectangle<int> (x, stripY, flagWidth (text), h - 1);
        g.setColour (body); g.fillRoundedRectangle (f.toFloat(), 3.0f);
        if (hasEditorNote (list, th)) paintEditorRing (g, f.toFloat());
        if (sel) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (f.toFloat().reduced (0.5f), 3.0f, 1.5f); }
        g.setColour (done ? juce::Colour (0xffd0d2d8) : juce::Colour (0xff0b1b1e));
        g.drawText (text, f.reduced (8, 0), juce::Justification::centredLeft, true);
        if (done) g.fillRect (f.getX() + 3, f.getCentreY(), f.getWidth() - 6, 1);
    }
}

/** The thread whose flag is at x (in the strip), as indices into 'list' - empty when none. */
inline std::vector<int> threadAt (const std::vector<FeedbackNote>& list, int x, const std::function<int (double)>& xOf)
{
    std::vector<int> hit;
    for (auto& th : feedback::threads (list))
    {
        const int fx = xOf (list[(size_t) th.front()].seconds), w = flagWidth (flagText (list, th));
        if ((x >= fx && x < fx + w) || std::abs (x - fx) <= 3) hit = th;               // (the last one drawn is on top)
    }
    return hit;
}

/** The pop-up that opens on a flag: every comment of the thread, with its bar, who wrote it, a Done tick and a Delete.
    3.51.0: "Write Reply..." writes the editor's reply under it; the editor's own notes have a gold ring and "Change..." in place of the tick. */
class ThreadBox : public juce::Component
{
public:
    ThreadBox (std::vector<FeedbackNote> notes_, std::function<void (const juce::String&, bool)> onDone, std::function<void (const juce::String&)> onDelete, std::function<void()> onGo,
               std::function<void()> onAnswer = {}, std::function<void (const juce::String&)> onChange = {}, std::vector<juce::String> answers_ = {})
        : notes (std::move (notes_)), answers (std::move (answers_)), doneCb (std::move (onDone)), delCb (std::move (onDelete)), goCb (std::move (onGo)), answerCb (std::move (onAnswer)), changeCb (std::move (onChange))
    {
        answers.resize (notes.size());
        addAndMakeVisible (goBtn);
        goBtn.setButtonText ("Go here"); goBtn.onClick = [this] { if (goCb) goCb(); };
        addChildComponent (answerBtn);
        answerBtn.setButtonText ("Write Reply..."); answerBtn.setVisible (answerCb != nullptr);
        answerBtn.setTooltip ("Write your reply under this comment. It is kept with the comment and both go out in the next listening copy, where the listener reads what you did about it (or why not).");
        answerBtn.onClick = [this] { auto cb = answerCb; if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); if (cb) juce::MessageManager::callAsync (cb); };     // (once this box has gone)
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto key = notes[i].key;
            juce::Button* t = nullptr;
            if (notes[i].editor && changeCb != nullptr)
            {
                t = ticks.add (new juce::TextButton ("Change..."));
                t->onClick = [this, key] { auto cb = changeCb; if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); if (cb) juce::MessageManager::callAsync ([cb, key] { cb (key); }); };
            }
            else
            {
                t = ticks.add (new juce::ToggleButton ("Done")); t->setToggleState (notes[i].done, juce::dontSendNotification);
                t->onClick = [this, i, key] { if (doneCb) doneCb (key, ticks[(int) i]->getToggleState()); notes[i].done = ticks[(int) i]->getToggleState(); repaint(); };
            }
            addAndMakeVisible (t);
            auto* d = dels.add (new juce::TextButton ("Delete"));
            d->onClick = [this, key] { if (delCb) delCb (key); if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };
            addAndMakeVisible (d);
        }
        layoutNow();
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff23262b));
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i]; const auto r = blocks[i];
            if (n.editor) { g.setColour (editorGold()); g.drawRoundedRectangle (juce::Rectangle<int> (r.getX() - 4, r.getY() - 3, r.getWidth() + 6, r.getHeight() - 1).toFloat(), 4.0f, 2.0f); }
            g.setColour (personColour (n)); g.fillRect (r.getX(), r.getY(), 4, r.getHeight() - 6);
            g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            auto head = (n.who.isNotEmpty() ? n.who : juce::String ("Someone")) + (n.editor ? juce::String (" (editor)") : juce::String()) + juce::String::fromUTF8 ("  \xC2\xB7  ") + timefmt::minSecText (n.seconds);
            if (n.bar.isNotEmpty()) head += juce::String::fromUTF8 ("  \xC2\xB7  ") + n.bar;
            g.drawText (head, r.getX() + 12, r.getY(), r.getWidth() - 174, 20, juce::Justification::centredLeft, true);
            int ty = r.getY() + 22;
            if (answers[i].isNotEmpty())
            {
                g.setColour (juce::Colour (0xff9a9da6)); g.setFont (juce::FontOptions (12.0f, juce::Font::italic));
                g.drawText (answers[i], r.getX() + 12, ty - 2, r.getWidth() - 16, 16, juce::Justification::centredLeft, true); ty += 16;
            }
            juce::AttributedString as; as.setJustification (juce::Justification::topLeft);
            as.append (n.text, juce::Font (juce::FontOptions (13.0f)), n.done ? juce::Colour (0xff9a9da6) : juce::Colour (0xffe8e8e8));
            juce::TextLayout tl; tl.createLayout (as, (float) (r.getWidth() - 16));
            tl.draw (g, juce::Rectangle<float> ((float) r.getX() + 12, (float) ty, (float) r.getWidth() - 16, tl.getHeight() + 2.0f));
        }
    }
    void resized() override { layoutNow(); }

private:
    void layoutNow()
    {
        constexpr int w = 440;                                              // (3.51.0: 360 - "Sarah Whitfield . 0:12 . Bar 50" did not fit beside the keys)
        blocks.clear();
        int y = 6;
        goBtn.setBounds (w - 76, 6, 66, 22);
        answerBtn.setBounds (10, 6, 104, 22);
        y = 36;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            juce::AttributedString as; as.append (notes[i].text, juce::Font (juce::FontOptions (13.0f)), juce::Colours::white);
            juce::TextLayout tl; tl.createLayout (as, (float) (w - 36));
            const int h = 22 + (answers[i].isNotEmpty() ? 16 : 0) + (int) std::ceil (tl.getHeight()) + 10;
            blocks.push_back (juce::Rectangle<int> (10, y, w - 20, h));
            const bool wide = dynamic_cast<juce::TextButton*> (ticks[(int) i]) != nullptr;                // ("Change...")
            ticks[(int) i]->setBounds (wide ? w - 164 : w - 150, y, wide ? 76 : 60, 20); dels[(int) i]->setBounds (w - 84, y, 60, 20);
            y += h + 6;
        }
        setSize (w, juce::jmin (y + 6, 420));
    }
    std::vector<FeedbackNote> notes;
    std::vector<juce::String> answers;                                     // for each note: "answers Sarah's comment on Edit 1" when what it answers is not in this thread ("" otherwise)
    std::function<void (const juce::String&, bool)> doneCb; std::function<void (const juce::String&)> delCb; std::function<void()> goCb, answerCb; std::function<void (const juce::String&)> changeCb;
    juce::TextButton goBtn, answerBtn;
    juce::OwnedArray<juce::Button> ticks; juce::OwnedArray<juce::TextButton> dels;
    std::vector<juce::Rectangle<int>> blocks;
};

/** 3.51.0: the editor's note put into an edit's comments (the live edit's, or a frozen version's): at 'seconds', answering 'replyTo' ("" = none). */
inline void addEditorNote (EditDef& e, double seconds, const juce::String& who, const juce::String& text, const juce::String& replyTo, int madeIn)
{
    FeedbackNote n; n.madeIn = madeIn;
    n.key = juce::Uuid().toString(); n.who = who; n.text = text; n.seconds = juce::jmax (0.0, seconds); n.editor = true; n.replyTo = replyTo; n.source = "fermata";
    e.feedback.push_back (n);
    feedback::sortNotes (e.feedback);
}

/** Opens the thread under 'flagArea' (screen coordinates). 'changed' is called after a tick / delete / answer; 'goTo' puts the playhead at the thread's time.
    'hand' (3.51.0): with it the box has "Write Reply..." and the editor's notes can be changed. 'whoOf': the writer of a comment anywhere in the edit's
    versions, by its key, as "Sarah (Edit 1)" - for an answer whose comment is in another lane. */
inline void showThread (std::function<EditDef*()> getEdit, const std::vector<int>& thread, juce::Rectangle<int> flagArea, std::function<void()> changed, std::function<void (double)> goTo,
                        EditorHand hand = {}, std::function<juce::String (const juce::String&)> whoOf = {})
{
    auto* e = getEdit(); if (e == nullptr) return;
    std::vector<FeedbackNote> notes; for (int i : thread) if (i < (int) e->feedback.size()) notes.push_back (e->feedback[(size_t) i]);
    if (notes.empty()) return;
    const double at = notes.front().seconds;
    std::vector<juce::String> answers;
    for (auto& n : notes)
    {
        juce::String a;
        bool here = n.replyTo.isEmpty(); for (auto& o : notes) if (o.key == n.replyTo) here = true;
        if (! here) { const auto w = whoOf ? whoOf (n.replyTo) : juce::String(); a = w.isNotEmpty() ? "answers " + w : juce::String ("answers an earlier comment"); }
        answers.push_back (a);
    }
    std::function<void()> onAnswer; std::function<void (const juce::String&)> onChange;
    if (hand.name)
    {
        // the answer goes under the last thing a listener said in the thread (or under the first note, when it is all the editor's own)
        juce::String target = notes.front().key, targetWho = notes.front().who;
        for (auto& n : notes) if (! n.editor) { target = n.key; targetWho = n.who; }
        onAnswer = [getEdit, changed, hand, at, target, targetWho]
        {
            askEditorNote ("Write a reply" + (targetWho.isNotEmpty() ? " to " + targetWho : juce::String()),
                           "Your reply is kept under the comment, with a gold ring. It goes out, with the comment it answers, in the next listening copy.", {}, hand, false,
                           [getEdit, changed, at, target, madeIn = hand.version] (const juce::String& who, const juce::String& text)
                           { if (auto* ed = getEdit()) { addEditorNote (*ed, at, who, text, target, madeIn); if (changed) changed(); } });
        };
        onChange = [getEdit, changed, hand] (const juce::String& key)
        {
            auto* ed = getEdit(); if (ed == nullptr) return;
            juce::String was; for (auto& n : ed->feedback) if (n.key == key) was = n.text;
            askEditorNote ("Change your note", {}, was, hand, true,
                           [getEdit, changed, key] (const juce::String& who, const juce::String& text)
                           { if (auto* ed2 = getEdit()) { for (auto& n : ed2->feedback) if (n.key == key && n.editor) { n.text = text; n.who = who; } if (changed) changed(); } });
        };
    }
    auto box = std::make_unique<ThreadBox> (notes,
        [getEdit, changed] (const juce::String& key, bool done) { if (auto* ed = getEdit()) { for (auto& n : ed->feedback) if (n.key == key) n.done = done; if (changed) changed(); } },
        [getEdit, changed] (const juce::String& key)
        {
            if (auto* ed = getEdit())
            {
                ed->feedback.erase (std::remove_if (ed->feedback.begin(), ed->feedback.end(), [&] (const FeedbackNote& n) { return n.key == key; }), ed->feedback.end());
                if (changed) changed();
            }
        },
        [goTo, at] { if (goTo) goTo (at); }, onAnswer, onChange, answers);
    juce::CallOutBox::launchAsynchronously (std::move (box), flagArea, nullptr);
}
} // namespace feedbackui
} // namespace td
