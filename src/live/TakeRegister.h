#pragma once
/** fermata:Live 0.2.0: a merged take into the project. The take window and the take group are made exactly as fermata's own recording makes them
    (Project::addTakeWindow, TakeGroup with its files, number, label, length, time of day, lost samples), so that fermata opens the project and
    finds an ordinary take; each gap of the recording becomes a marker on the take window's timeline, named with its time of day and length. The
    project is NOT saved here: the caller saves it (Project::save), which is the shared code's job. */
#include "Merge.h"
#include "../core/Project.h"

namespace td::live
{
struct TakeRegister
{
    /** The take window called 'name' (made if the project has none). */
    static TakeWindowDef& window (Project& p, const juce::String& name)
    {
        for (auto& w : p.takeWindows) if (w->name == name) return *w;
        return p.addTakeWindow (name);
    }

    /** The project track for a recorded one: by id, else by name, else a new track of that width (its inputs as recorded). */
    static TrackDef& track (Project& p, const TrackSpec& t)
    {
        if (! t.id.isNull()) for (auto& x : p.tracks) if (x.id == t.id) return x;
        for (auto& x : p.tracks) if (x.name == t.name && x.channelCount() == t.channels) return x;
        const auto fmt = t.channels == 1 ? TrackFormat::Mono : t.channels == 2 ? TrackFormat::Stereo : TrackFormat::Surround;
        auto& made = p.addTrack (t.name, fmt, t.inputs.empty() ? -1 : t.inputs.front());
        for (int c = 0; c < t.channels && c < (int) kMaxTrackChannels; ++c) made.inputs[(size_t) c] = c < (int) t.inputs.size() ? t.inputs[(size_t) c] : -1;
        if (fmt == TrackFormat::Surround) made.surroundChannels = t.channels;
        return made;
    }

    /** Adds the merged take (res.files, one per track in the session's order) to the window as the next take on its timeline. Returns the group's id.
        A take with that number already in the window (the merge ran again after a crash) is replaced. */
    static juce::Uuid add (Project& p, TakeWindowDef& w, const SessionIndex& idx, const TakeRec& take, const MergeResult& res)
    {
        TakeGroup g;
        double oldStart = -1.0;
        for (auto it = w.groups.begin(); it != w.groups.end();)
            if (it->number == take.number && (int) it->files.size() == res.files.size() && ! res.files.isEmpty() && it->files.front().file.getFullPathName() == res.files[0])
            {   // the same take again (the merge ran twice): it keeps its place on the timeline, and its old gap markers go
                g.id = it->id; oldStart = it->startSeconds;
                const double oldEnd = it->startSeconds + it->lengthSeconds();
                for (auto m = w.markers.begin(); m != w.markers.end();)
                    if (m->name.startsWith ("Gap ") && m->seconds >= oldStart - 1.0e-6 && m->seconds <= oldEnd + 1.0e-6) m = w.markers.erase (m); else ++m;
                it = w.groups.erase (it);
            }
            else ++it;
        if (g.id.isNull()) g.id = juce::Uuid();
        g.number = take.number;
        g.label = take.label.trim() == w.name.trim() ? juce::String() : take.label.trim();
        g.sampleRate = idx.sampleRate;
        g.lengthSamples = res.lengthSamples;
        g.recordedAt = take.recordedAt;
        g.startSeconds = oldStart >= 0.0 ? oldStart : w.groups.empty() ? 0.0 : w.endSeconds() + 2.0;
        g.lostSamples = res.lostSamples;
        g.overruns = (int) res.gaps.size();
        if (! res.gaps.empty())
            g.notes = juce::String (res.gaps.size()) + " gap(s) in the live recording: " + lostText (res.lostSamples, idx.sampleRate) + " of silence (see the markers)";
        for (size_t i = 0; i < idx.tracks.size() && (int) i < res.files.size(); ++i)
        {
            auto& t = track (p, idx.tracks[i]);
            TakeFile f;
            f.trackId = t.id; f.trackName = t.name; f.file = juce::File (res.files[(int) i]); f.numChannels = idx.tracks[i].channels;
            g.files.push_back (f);
            w.unhide (t.id);
        }
        w.groups.push_back (g);
        if (take.number >= w.nextNumber) w.nextNumber = take.number + 1;
        for (auto& gap : res.gaps)
        {
            Marker m; m.id = juce::Uuid();
            m.seconds = g.startSeconds + (double) (gap.firstSample - take.start) / idx.sampleRate;
            m.name = "Gap " + gap.time + " (" + lostText (gap.samples, idx.sampleRate) + "): " + gap.why;
            w.markers.push_back (m);
        }
        p.changed();
        return g.id;
    }
};
} // namespace td::live
