#pragma once
/** fermata:Live 0.5.0: the recording's REPORT - a PDF of a piece's take window: every take, with a line along it showing where each note, flag, gap
    and fix version is, then the list of them at their places (the time of day and the time in the take), who wrote each, the Fixer's replies and
    the fix versions. Both stations' notes are in it (each station shows only its own on the screen). Written by the Primary's window (which has
    the project and the session) with the shared PDF writer (src/core/Pdf.h). Message thread. */
#include "StationNotes.h"
#include "../core/Pdf.h"

namespace td::live::report
{
struct Input
{
    juce::String projectName, piece, version;
    std::shared_ptr<const TakeLayout> layout;
    const TakeWindowDef* window = nullptr;
    std::vector<notes::Info> notes;                          // everyone's
    std::vector<FlagRec> sessionFlags;                       // the running session's (when it records this window)
    std::vector<juce::var> sessionFixes;
};

/** One line of the report. */
struct Item
{
    enum Kind { Flag, Note, Gap, Marker, Fix } kind = Note;
    double seconds = -1.0;                                   // on the timeline shown
    int take = 0;                                            // its take's number (0: in none)
    juce::String by, text, station;
    juce::Time at;
    std::vector<FlagReply> replies;
};

inline const char* kindName (Item::Kind k)
{
    switch (k) { case Item::Flag: return "Flag"; case Item::Note: return "Note"; case Item::Gap: return "Gap"; case Item::Marker: return "Marker"; case Item::Fix: return "Fix"; }
    return "";
}
inline pdf::Colour kindColour (const Item& i)
{
    switch (i.kind)
    {
        case Item::Flag:   return { 0.85f, 0.25f, 0.18f };
        case Item::Note:   return i.station == "Fixer" ? pdf::Colour { 0.18f, 0.55f, 0.30f } : pdf::Colour { 0.80f, 0.58f, 0.05f };
        case Item::Gap:    return pdf::grey (0.45f);
        case Item::Marker: return pdf::grey (0.55f);
        case Item::Fix:    return { 0.45f, 0.30f, 0.70f };
    }
    return {};
}

/** Everything the report lists, each at its place, in order. */
inline std::vector<Item> gather (const Input& in)
{
    std::vector<Item> items;
    const auto* L = in.layout.get();
    auto takeOf = [L] (double s) { if (L == nullptr || s < 0.0) return 0; auto* t = L->takeAt (s); return t != nullptr ? t->number : 0; };
    std::set<juce::String> have;
    // the flags: the session's, then the project's
    for (auto& f : in.sessionFlags)
    {
        if (f.isNote) continue;
        have.insert (f.id);
        if (f.removed) continue;
        Item i; i.kind = Item::Flag; i.seconds = L != nullptr ? L->windowSecondsOf (f.sample) : -1.0; i.by = f.by.isNotEmpty() ? f.by : juce::String ("Primary");
        i.text = f.note; i.at = f.pinnedAt; i.station = "Primary";
        for (auto& r : f.replies) i.replies.push_back ({ r.text, r.by, r.at });
        items.push_back (i);
    }
    if (in.window != nullptr)
        for (auto& m : in.window->markers)
        {
            if (m.liveNote) continue;                                      // (the notes come from 'notes')
            if (m.flag && have.count (m.id.toString()) != 0) continue;
            Item i; i.seconds = L != nullptr ? L->shownOfWindow (m.seconds) : m.seconds; i.at = m.pinnedAt;
            if (m.flag) { i.kind = Item::Flag; i.text = m.note; i.by = m.by.isNotEmpty() ? m.by : juce::String ("Primary"); i.replies = m.replies; i.station = "Primary"; }
            else if (m.name.startsWith ("Gap ")) { i.kind = Item::Gap; i.text = m.name.fromFirstOccurrenceOf ("Gap ", false, false); }
            else { i.kind = Item::Marker; i.text = m.name; }
            items.push_back (i);
        }
    for (auto& n : in.notes)
    {
        Item i; i.kind = Item::Note; i.seconds = n.seconds; i.by = n.by.isNotEmpty() ? n.by : n.station; i.text = n.text; i.at = n.at; i.station = n.station;
        items.push_back (i);
    }
    // the fix versions: the session's, then the project's
    std::set<juce::String> fixes;
    auto addFix = [&] (int takeNumber, juce::int64 from, double rate, int version, const juce::String& what, const juce::String& note, const juce::String& by, juce::Time made, int state)
    {
        Item i; i.kind = Item::Fix; i.take = takeNumber; i.by = by.isNotEmpty() ? by : juce::String ("Fixer"); i.at = made; i.station = "Fixer";
        if (L != nullptr) if (auto* t = L->findTake (takeNumber)) i.seconds = t->start + (double) from / (rate > 0.0 ? rate : 48000.0);
        i.text = "Version " + juce::String (version) + ": " + what + (note.isNotEmpty() ? " - " + note : juce::String())
                 + (state == 1 ? " (approved in the review)" : state == 2 ? " (not used in the review)" : juce::String());
        items.push_back (i);
    };
    for (auto& v : in.sessionFixes)
    {
        fixes.insert (v["id"].toString());
        addFix ((int) v["takeNumber"], (juce::int64) v["from"], (double) v["rate"], juce::jmax (1, (int) v["version"]), v["what"].toString(), v["note"].toString(),
                v["by"].toString(), juce::Time ((juce::int64) v["madeAt"]), -1);
    }
    if (in.window != nullptr)
        for (auto& v : in.window->fixVersions)
            if (fixes.count (v.id.toString()) == 0) addFix (v.takeNumber, v.startSample, v.sampleRate, v.versionNumber, v.what, v.note, v.by, v.madeAt, v.state);
    for (auto& i : items) if (i.take == 0) i.take = takeOf (i.seconds);
    std::stable_sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.seconds < b.seconds; });
    return items;
}

/** Writes the report to 'file'. */
inline bool write (const Input& in, const juce::File& file, juce::String& error)
{
    const auto* L = in.layout.get();
    if (L == nullptr) { error = "Nothing to report."; return false; }
    const auto items = gather (in);
    pdf::Document d;
    d.setTitle ("fermata:Live report - " + in.projectName + " - " + in.piece);
    d.setAuthor ("fermata:Live " + in.version);
    constexpr float left = 40.0f, right = 555.0f, bottom = 800.0f;
    float y = 0.0f;
    const auto ink = pdf::grey (0.1f), dim = pdf::grey (0.42f);
    auto newPage = [&]
    {
        d.newPage();
        y = 52.0f;
        d.text (left, y, "fermata:Live report", pdf::Bold, 15.0f, ink);
        d.textRight (right, y, pdf::Document::fit (in.projectName + "  -  " + in.piece, pdf::Regular, 10.0f, 300.0f), pdf::Regular, 10.0f, dim);
        y += 8.0f; d.line (left, y, right, y, 0.6f, pdf::grey (0.6f)); y += 18.0f;
    };
    auto room = [&] (float need) { if (y + need > bottom) newPage(); };
    newPage();
    {   // who and when
        std::set<juce::String> primary, fixer;
        for (auto& i : items)
            if (i.by.isNotEmpty() && i.kind != Item::Gap && i.kind != Item::Marker) (i.station == "Fixer" ? fixer : primary).insert (i.by);
        auto join = [] (const std::set<juce::String>& s) { juce::StringArray a; for (auto& x : s) a.add (x); return a.joinIntoString (", "); };
        d.text (left, y, "Project: " + in.projectName + "     Piece: " + in.piece, pdf::Regular, 10.5f, ink); y += 14.0f;
        if (! primary.empty()) { d.text (left, y, "Primary: " + join (primary), pdf::Regular, 10.5f, ink); y += 14.0f; }
        if (! fixer.empty()) { d.text (left, y, "Fixer: " + join (fixer), pdf::Regular, 10.5f, ink); y += 14.0f; }
        d.text (left, y, "Made " + juce::Time::getCurrentTime().formatted ("%d/%m/%Y %H:%M") + " by fermata:Live " + in.version + ".  "
                + juce::String ((int) items.size()) + " item(s) in " + juce::String ((int) L->takes.size()) + " take(s).", pdf::Regular, 9.5f, dim);
        y += 22.0f;
    }
    // the columns
    constexpr float cNum = left, cClock = left + 20.0f, cIn = left + 82.0f, cKind = left + 136.0f, cBy = left + 170.0f, cText = left + 250.0f;
    const float textW = right - cText;
    auto takeSection = [&] (const ShownTake* t, const std::vector<const Item*>& list)
    {
        room (90.0f);
        if (t != nullptr)
        {
            d.text (left, y, t->title (L->windowName), pdf::Bold, 12.5f, ink); y += 14.0f;
            juce::String sub;
            if (t->clockMs != 0)
            {
                const juce::Time a (t->clockMs), b (t->clockMs + (juce::int64) std::llround (t->lengthSeconds() * 1000.0));
                sub << "Recorded " << a.formatted ("%d/%m/%Y %H:%M:%S") << " - " << b.formatted ("%H:%M:%S") << "   ";
            }
            sub << "Length " << TakeLayout::hmsText (t->lengthSeconds(), false);
            if (t->recording) sub << "  (still recording when this report was made)";
            d.text (left, y, sub, pdf::Regular, 9.5f, dim); y += 12.0f;
            // the take as a line, a numbered tick at each item
            const float barY = y + 12.0f, barW = right - left;
            d.rect (left, barY, barW, 6.0f, pdf::grey (0.86f));
            const double len = juce::jmax (0.001, t->lengthSeconds());
            for (size_t k = 0; k < list.size(); ++k)
            {
                const float x = left + barW * (float) juce::jlimit (0.0, 1.0, (list[k]->seconds - t->start) / len);
                d.rect (x - 0.75f, barY - 4.0f, 1.5f, 14.0f, kindColour (*list[k]));
                d.text (x - 3.0f, barY - 6.0f, juce::String ((int) k + 1), pdf::Bold, 6.5f, kindColour (*list[k]));
            }
            y = barY + 22.0f;
        }
        else { d.text (left, y, "Not in a take", pdf::Bold, 12.5f, ink); y += 18.0f; }
        if (list.empty()) { d.text (left, y, "Nothing noted in this take.", pdf::Italic, 9.5f, dim); y += 20.0f; return; }
        d.text (cNum, y, "#", pdf::Bold, 8.5f, dim); d.text (cClock, y, "Time of day", pdf::Bold, 8.5f, dim); d.text (cIn, y, "In the take", pdf::Bold, 8.5f, dim);
        d.text (cKind, y, "What", pdf::Bold, 8.5f, dim); d.text (cBy, y, "By", pdf::Bold, 8.5f, dim); d.text (cText, y, "", pdf::Bold, 8.5f, dim);
        y += 4.0f; d.line (left, y, right, y, 0.4f, pdf::grey (0.75f)); y += 11.0f;
        for (size_t k = 0; k < list.size(); ++k)
        {
            const auto& i = *list[k];
            auto lines = pdf::wrap (i.text.trim().isNotEmpty() ? i.text.trim() : juce::String ("(no text)"), pdf::Regular, 9.5f, textW);
            for (auto& r : i.replies)
                for (auto& l : pdf::wrap ("-> " + r.by + (r.at.toMilliseconds() != 0 ? " (" + r.at.formatted ("%H:%M:%S") + ")" : juce::String()) + ": " + r.text, pdf::Italic, 9.0f, textW))
                    lines.add ("\x01" + l);
            room (12.0f * (float) juce::jmax (1, lines.size()) + 4.0f);
            const auto clock = L->clockMsAt (i.seconds);
            d.text (cNum, y, juce::String ((int) k + 1), pdf::Bold, 9.0f, kindColour (i));
            d.text (cClock, y, clock != 0 ? juce::Time (clock).formatted ("%H:%M:%S") : juce::String ("-"), pdf::Regular, 9.0f, ink);
            d.text (cIn, y, t != nullptr ? TakeLayout::hmsText (i.seconds - t->start, false) : juce::String ("-"), pdf::Regular, 9.0f, ink);
            d.text (cKind, y, kindName (i.kind), pdf::Bold, 9.0f, kindColour (i));
            d.text (cBy, y, pdf::Document::fit (i.by, pdf::Regular, 9.0f, cText - cBy - 6.0f), pdf::Regular, 9.0f, ink);
            for (auto& l : lines)
            {
                if (y > bottom) { newPage(); }
                const bool reply = l.startsWithChar ('\x01');
                d.text (cText + (reply ? 8.0f : 0.0f), y, reply ? l.substring (1) : l, reply ? pdf::Italic : pdf::Regular, reply ? 9.0f : 9.5f, reply ? pdf::Colour { 0.15f, 0.45f, 0.22f } : ink);
                y += 12.0f;
            }
            y += 3.0f;
        }
        y += 14.0f;
    };
    std::vector<const ShownTake*> order;
    for (auto& t : L->takes) order.push_back (&t);
    std::sort (order.begin(), order.end(), [] (const ShownTake* a, const ShownTake* b) { return a->start < b->start; });
    for (auto* t : order)
    {
        std::vector<const Item*> list;
        for (auto& i : items) if (i.take == t->number) list.push_back (&i);
        takeSection (t, list);
    }
    std::vector<const Item*> loose;
    for (auto& i : items) if (i.take == 0) loose.push_back (&i);
    if (! loose.empty()) takeSection (nullptr, loose);
    if (order.empty() && loose.empty()) { d.text (left, y, "No takes in this take window yet.", pdf::Italic, 10.0f, dim); }
    // page numbers
    const int pages = d.pageCount();
    for (int p = 0; p < pages; ++p)
    {
        d.setPage (p);
        d.text (left, 822.0f, "fermata:Live " + in.version + "  -  " + in.projectName, pdf::Regular, 8.0f, dim);
        d.textRight (right, 822.0f, "Page " + juce::String (p + 1) + " of " + juce::String (pages), pdf::Regular, 8.0f, dim);
    }
    if (! d.save (file)) { error = "Could not write " + file.getFullPathName(); return false; }
    return true;
}
} // namespace td::live::report
