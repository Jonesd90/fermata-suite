#pragma once
#include "Common.h"
#include <atomic>
#include <vector>

namespace td
{
/** The listening copy: ONE .html file that holds the recording itself, a waveform, play / pause / stop, and a form where a listener adds comments
    (each pinned to the time they pressed the button, with an optional bar written as text). It opens with a double-click in any current browser
    (Windows, Mac, iPhone / iPad, Android) with nothing to install. The listener's comments are kept in their browser as they go ("Save my comments");
    "Export comments for sending back to the sender" makes a small .fmnotes file (JSON inside), named like the page with "listening notes" in place
    of "listening copy", which Fermata imports. One page can also hold all the pieces (a list of pieces, played one after another like a CD). */
namespace reviewpage
{
/** 3.51.0 (asked for on 10 November 2026): the conversation so far, shown in the listening copy - every comment made on every version of the piece
    and the editor's replies, one lane for each version, each at the place its music has in THIS copy. A lane holds what was said in that
    round: the listeners' comments on that version's listening copy, and what the editor wrote while making that version - so the editor's
    replies to the comments on Edit 2 are in Edit 3's lane ("what the editor says about this version"), each saying which comment it answers.
    The listener ticks each lane on or off. Made by reviewpage::historyFor (ReviewHistory.h); the page only shows it - what a listener adds
    goes back in the .fmnotes file as before. */
struct PastNote
{
    juce::String id, who, bar, text; // the comment's key in the project, the person, the bar as written, the words
    juce::String replyTo;            // the id of the comment it answers ("" = none); that comment may be in another lane
    double       seconds = 0.0;      // in this copy's audio (may lie before 0 or past the end when its music is not in the copy: the page keeps it at the edge)
    juce::uint32 argb = 0xff35b7c9u; // the person's colour (feedback::personArgb)
    bool         editor = false;     // the editor's own: a gold ring round it
    bool         done = false;       // ticked off in fermata
    bool         adrift = false;     // its music is not in this version any more: the place is where it used to be
};
struct PastLane
{
    int          version = 0;        // Edit N
    juce::String label;              // "Edit 1", "Edit 2 - More reverb"
    bool         current = false;    // the version this copy is of: the lane holds the editor's replies and notes about THIS version
    std::vector<PastNote> notes;     // in time order; notes at one moment are a thread
};

struct Spec
{
    juce::File   audio;              // the finished WAV / FLAC / MP3 that goes inside the page
    juce::String mime;               // "audio/wav", "audio/flac" or "audio/mpeg"
    juce::String piece;              // name shown at the top
    juce::String project;            // optional second line
    juce::String editId;             // so the comments find their way back to the right edit
    int          version = 0;        // 3.42.0: which version of the edit this is ("Edit 2": 2), so the comments go to that version's lane; 0 = not said
    double       offsetSeconds = 0;  // where the file's time 0 sits on the edit's timeline
    double       durationSeconds = 0;
    std::vector<int> peaks;          // min, max pairs, -100..100 (see computePeaks)
    std::vector<PastLane> history;   // 3.51.0: the comments so far, a lane for each version that has any (empty: the page looks as it did)
};

/** Min / max of the louder of the two channels in 'columns' slices of 'file' (times 'gain'), as pairs in -100..100. */
std::vector<int> computePeaks (const juce::File& file, float gain, int columns, const std::atomic<bool>* cancel = nullptr);
int peakColumnsFor (double durationSeconds);

/** Writes the page for one piece. Returns an error text, or "" when it worked. */
juce::String write (const juce::File& html, const Spec&, const std::atomic<bool>* cancel = nullptr);

/** Writes ONE page that holds several pieces (in this order), with 'title' at the top. */
juce::String writeMany (const juce::File& html, const juce::String& title, const juce::String& project, const std::vector<Spec>& pieces, const std::atomic<bool>* cancel = nullptr);

juce::String base64 (const void* data, size_t size);
} // namespace reviewpage
} // namespace td
