#pragma once
/** fermata:Live 0.3.0: a session's timeline as the stations read it - from the primary drive's chunks while they are there (the open one too, as
    far as the drive has written it), and from a take's merged files once its chunks have gone; silence where there is nothing (a gap). The
    session index (session.json) is read again when it changes. Reading never touches the recorder: the files are opened for reading only, in
    a way that lets the recorder go on writing, and deleting, them (JUCE's FileInputStream shares read, write and delete on Windows).

    Used by one thread at a time (a station's player, or its ReTouch job: each has its own). */
#include "SessionIndex.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>

namespace td::live
{
/** A WAV file read straight from its bytes: the format from its "fmt " chunk, the audio from its "data" chunk to the end of the file, whatever
    the header says (a chunk still being written has a header that is behind). 16-, 24- and 32-bit PCM and 32-bit float. */
class RawWavReader
{
public:
    bool open (const juce::File& f)
    {
        file = f; in = std::make_unique<juce::FileInputStream> (f);
        if (in->failedToOpen()) { in.reset(); return false; }
        unsigned char h[12];
        if (in->read (h, 12) != 12 || std::memcmp (h + 8, "WAVE", 4) != 0 || (std::memcmp (h, "RIFF", 4) != 0 && std::memcmp (h, "RF64", 4) != 0)) { in.reset(); return false; }
        juce::int64 pos = 12;
        const juce::int64 len = in->getTotalLength();
        bool haveFmt = false;
        while (pos + 8 <= len)
        {
            unsigned char ch[8];
            in->setPosition (pos);
            if (in->read (ch, 8) != 8) break;
            const juce::uint32 size = (juce::uint32) ch[4] | ((juce::uint32) ch[5] << 8) | ((juce::uint32) ch[6] << 16) | ((juce::uint32) ch[7] << 24);
            if (std::memcmp (ch, "fmt ", 4) == 0 && size >= 16)
            {
                unsigned char f[40] = {};
                in->read (f, (int) juce::jmin<juce::uint32> (size, 40));
                int tag = f[0] | (f[1] << 8);
                channels = f[2] | (f[3] << 8);
                rate = (double) ((juce::uint32) f[4] | ((juce::uint32) f[5] << 8) | ((juce::uint32) f[6] << 16) | ((juce::uint32) f[7] << 24));
                bits = f[14] | (f[15] << 8);
                if (tag == 0xfffe && size >= 26) tag = f[24] | (f[25] << 8);        // (WAVE_FORMAT_EXTENSIBLE: the sub-format)
                isFloat = tag == 3;
                haveFmt = (tag == 1 || tag == 3) && channels > 0 && (bits == 16 || bits == 24 || bits == 32);
            }
            else if (std::memcmp (ch, "data", 4) == 0) { dataStart = pos + 8; break; }
            pos += 8 + (juce::int64) size + (size & 1);
        }
        if (! haveFmt || dataStart <= 0) { in.reset(); return false; }
        blockAlign = channels * (bits / 8);
        return true;
    }
    bool isOpen() const noexcept            { return in != nullptr; }
    int numChannels() const noexcept        { return channels; }
    double sampleRate() const noexcept      { return rate; }
    /** The frames in the file now (it may still be growing). */
    juce::int64 frames() const              { return in == nullptr || blockAlign <= 0 ? 0 : juce::jmax<juce::int64> (0, (file.getSize() - dataStart) / blockAlign); }
    /** Frames [from, from + n) of channel 'channel' (or all channels into dest[0..channels-1] when channel < 0) as floats; what is not in the
        file is silence. Returns the frames read. */
    int read (juce::int64 from, int n, float* const* dest, int destChannels)
    {
        for (int c = 0; c < destChannels; ++c) if (dest[c] != nullptr) juce::FloatVectorOperations::clear (dest[c], n);
        if (in == nullptr || n <= 0 || from < 0) return 0;
        const juce::int64 avail = frames();
        const int take = (int) juce::jmin<juce::int64> (n, avail - from);
        if (take <= 0) return 0;
        bytes.resize ((size_t) take * (size_t) blockAlign);
        in->setPosition (dataStart + from * blockAlign);
        const int got = in->read (bytes.data(), (int) bytes.size());
        const int frames = got / blockAlign;
        const int bps = bits / 8;
        for (int c = 0; c < juce::jmin (destChannels, channels); ++c)
        {
            float* d = dest[c];
            if (d == nullptr) continue;
            const unsigned char* p = bytes.data() + c * bps;
            for (int i = 0; i < frames; ++i, p += blockAlign)
            {
                if (bits == 24)      { const int v = (int) ((juce::uint32) p[0] << 8 | (juce::uint32) p[1] << 16 | (juce::uint32) p[2] << 24) >> 8; d[i] = (float) v / 8388608.0f; }
                else if (bits == 16) { const int v = (int) (juce::int16) (p[0] | (p[1] << 8)); d[i] = (float) v / 32768.0f; }
                else if (isFloat)    { float f; std::memcpy (&f, p, 4); d[i] = f; }
                else                 { juce::int32 v; std::memcpy (&v, p, 4); d[i] = (float) ((double) v / 2147483648.0); }
            }
        }
        return frames;
    }

private:
    juce::File file;
    std::unique_ptr<juce::FileInputStream> in;
    int channels = 0, bits = 0, blockAlign = 0; double rate = 0.0; bool isFloat = false;
    juce::int64 dataStart = 0;
    std::vector<unsigned char> bytes;
};

class ChunkTimeline
{
public:
    /** The primary drive's session folder (where session.json and the chunks are). */
    void setSession (const juce::File& folder)
    {
        if (folder == sessionFolder) return;
        sessionFolder = folder; readers.clear(); haveIndex = false; lastIndexTime = 0; lastCheckMs = 0;
        refresh (true);
    }
    juce::File folder() const               { return sessionFolder; }
    /** The recorder's word for how far the drive has written: an open chunk is not read past it. */
    void setWrittenTo (juce::int64 s) noexcept { writtenTo = s; }
    /** Reads the index again if its file has changed (at most every 'everyMs'). */
    void refresh (bool force = false, int everyMs = 500)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (! force && now - lastCheckMs < (juce::uint32) everyMs) return;
        lastCheckMs = now;
        const auto f = sessionFolder.getChildFile (kIndexFileName);
        const auto t = f.getLastModificationTime().toMilliseconds();
        if (! force && haveIndex && t == lastIndexTime) return;
        SessionIndex idx;
        if (idx.load (f)) { index = idx; haveIndex = true; lastIndexTime = t; }
    }
    bool valid() const noexcept             { return haveIndex; }
    const SessionIndex& sessionIndex() const noexcept { return index; }
    double sampleRate() const noexcept      { return index.sampleRate; }

    /** One session track's channels for the timeline's samples [start, start + n): dest[c] for each of its channels (n floats each). Silence where
        nothing is there. Returns true if any of it was audio. */
    bool read (int trackIndex, juce::int64 start, int n, float* const* dest)
    {
        if (! haveIndex || ! juce::isPositiveAndBelow (trackIndex, (int) index.tracks.size()) || n <= 0) return false;
        const auto& t = index.tracks[(size_t) trackIndex];
        for (int c = 0; c < t.channels; ++c) juce::FloatVectorOperations::clear (dest[c], n);
        bool any = false;
        const juce::int64 end = start + n;
        std::vector<float*> sub ((size_t) t.channels);
        for (auto& c : index.chunks)
        {
            const juce::int64 cs = c.firstSample;
            juce::int64 ce = c.samples >= 0 ? cs + c.samples : juce::jmin (cs + index.chunkSamples, writtenTo > 0 ? writtenTo : cs + index.chunkSamples);
            if (ce <= start || cs >= end) continue;
            const juce::int64 a = juce::jmax (start, cs), b = juce::jmin (end, ce);
            auto* r = readerFor (SessionIndex::chunkFile (sessionFolder, t, c));
            if (r != nullptr)
            {
                for (int k = 0; k < t.channels; ++k) sub[(size_t) k] = dest[k] + (a - start);
                if (r->read (a - cs, (int) (b - a), sub.data(), t.channels) > 0) any = true;
            }
            else if (auto* m = mergedFor (trackIndex, a))                   // (the chunk has gone: its take was merged)
            {
                for (int k = 0; k < t.channels; ++k) sub[(size_t) k] = dest[k] + (a - start);
                if (m->first->read (a - m->second, (int) (b - a), sub.data(), t.channels) > 0) any = true;
            }
        }
        // stretches no chunk lists any more (deleted chunks drop out of the index too): the merged take files
        if (! any)
            if (auto* m = mergedFor (trackIndex, start))
            {
                if (m->first->read (start - m->second, n, dest, t.channels) > 0) any = true;
            }
        return any;
    }

private:
    RawWavReader* readerFor (const juce::File& f)
    {
        const auto key = f.getFullPathName();
        auto it = readers.find (key);
        if (it != readers.end()) return it->second.get();
        if (! f.existsAsFile()) return nullptr;
        if (readers.size() > 96) readers.clear();                           // (a long session: the files of the oldest chunks are closed again)
        auto r = std::make_unique<RawWavReader>();
        if (! r->open (f)) return nullptr;                                  // (not kept: a file just made may have no header yet - asked again next time)
        auto* p = r.get();
        readers[key] = std::move (r);
        return p;
    }
    /** The merged file of the take that holds 'sample', for a track, and the take's first sample. */
    std::pair<RawWavReader*, juce::int64>* mergedFor (int trackIndex, juce::int64 sample)
    {
        for (auto& k : index.takes)
            if (k.merged && sample >= k.start && sample < k.end && trackIndex < k.mergedFiles.size())
            {
                auto* r = readerFor (juce::File (k.mergedFiles[trackIndex]));
                if (r == nullptr) return nullptr;
                mergedHit = { r, k.start };
                return &mergedHit;
            }
        return nullptr;
    }

    juce::File sessionFolder;
    SessionIndex index;
    bool haveIndex = false;
    juce::int64 lastIndexTime = 0, writtenTo = 0;
    juce::uint32 lastCheckMs = 0;
    std::map<juce::String, std::unique_ptr<RawWavReader>> readers;
    std::pair<RawWavReader*, juce::int64> mergedHit { nullptr, 0 };
};
} // namespace td::live
