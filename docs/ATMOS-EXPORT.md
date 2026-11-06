# fermata's own Dolby Atmos (ADM BWF) export: the brief (3.40.0)

Written 6 November 2026 (Opus), for building with Claude Fable the same day. The user's decision (6 November): **fermata writes the ADM BWF files
itself**; the user then opens them in the **Fiedler Mastering Console**, which adds Dolby's own metadata block (dbmd: binaural render modes, the
downmix settings, the trims), measures and sets the loudness, and writes the delivery files. No Dolby Atmos Composer, Beam or real-time pass in this
path (the Composer can stay for binaural monitoring while mixing; the 3.39.0 Atmos pass stays as it is).

The two Dolby documents the user sent are in this session's scratchpad as text: `spec/admprofile.txt` (Dolby Atmos Master ADM Profile v1.1, 5 January
2022, 31 pages) and `spec/delivery.txt` (Dolby Atmos Music Master Delivery Specification 2022.07, 4 pages); the PDFs are beside them. The user owns
them: they are not in the repository.

## What the delivery spec asks of the file (the parts fermata controls)

- ADM BWF conforming to the Dolby Atmos Master ADM profile; at most 128 PCM channels and 118 objects; **24-bit PCM at 48 kHz** (a 96 kHz project:
  a DAMF for archiving; the Mastering Console can batch-convert 96 kHz ADM files to 48 kHz).
- **Conformed and synced to the stereo deliverable**: no extra silence at the start compared with the stereo file; at the end less than one frame
  (2,000 samples at 48 kHz) for a single track; for a gapless album each boundary within half a frame (1,000 samples) and no extra silence at the end.
  fermata's answer: the Atmos file of a piece covers **exactly the stretch the Virtual Master's stereo file covers** (first audio to last audio plus
  the tail: `masterLengthSamples`, `MasterJobItem::editOffsetSeconds`), so the two match to the sample by construction - and the tab can say so.
- LFE (if used) **should** be low-passed between 100 and 150 Hz. (An optional low-pass on export is cheap; ask the user.)
- Left to the Mastering Console (the dbmd block, not public - EBU Tech 3285 Supplement 6 is from 2009 and has no Atmos segments): binaural render
  mode per bed channel and object (the same across an album), 5.1 / 5.1.x downmix "Direct Render", 5.1 to 2.0 "Lo/Ro" (the same across an album),
  trims; loudness at most -18 LKFS integrated per track and true peak at most -1 dBTP, measured on the 5.1 re-render; 24 fps; FFOA unassigned.

## What the ADM profile asks (the parts that shape the writer)

The profile is ITU-R BS.2076-0 with restrictions; **anything not listed in it makes the file non-compliant** (section 2.1). The essentials:

- **XML**: UTF-8, XML 1.0, numbers in decimal without leading zeros. Use the `ebuCore_2016` namespace (the one that conforms to the schema; the
  2014 one has known faults: Table 4-1). audioObject's start attribute is `start` (not `startTime`) and is `00:00:00.00000`.
- **Counts** (Table 2-1): up to 64 beds (audioObject typeDefinition DirectSpeakers) and 118 objects; 1..128 channels; objects + beds at most 123;
  exactly one audioProgramme.
- **Beds** (DirectSpeakers): only these sets, in this channel order (Table 2-21): 2.0 `L R`; 3.0; 5.0; 5.1 `L R C LFE Ls Rs`; 7.0; 7.1
  `L R C LFE Lss Rss Lrs Rrs`; 7.0.2; 7.1.2 `L R C LFE Lss Rss Lrs Rrs Lts Rts`. Custom definitions only, with the names of Table 2-11
  (`RoomCentricLeft` ...) and in each audioBlockFormat exactly `cartesian` 1, a `speakerLabel` (`RC_L` ...) and three `position` coordinates as in
  Table 2-14 (L -1,1,0; R 1,1,0; C 0,1,0; LFE -1,1,-1; Lss -1,0,0; Rss 1,0,0; Lrs -1,-1,0; Rrs 1,-1,0; Lts -1,0,1; Rts 1,0,1; Ls -1,-1,0; Rs 1,-1,0).
  One audioBlockFormat per DirectSpeakers channel, **without** rtime / duration.
- **Objects** (one channel each): audioBlockFormat with `cartesian` 1, three `position` coordinates in [-1, 1], `jumpPosition` 1 whose
  `interpolationLength` is `0.000000` in the first block (`AB_..._00000001`) and `0.005208` in any later one; optional gain, importance, width /
  depth / height (all three, equal), diffuse, channelLock (no maxDistance), zoneExclusion; nothing else. A fixed object needs one block (rtime 0,
  duration = the programme's).
- **IDs** (section 3): AP_yyyyxxxx / AC_yyyyxxxx with yyyy 0001 (DirectSpeakers) or 0003 (Objects), xxxx from 0x1001, continuous; AB_ follows its
  channel, zzzzzzzz from 1; AS_ / AT_: their yyyyxxxx equals the channel format's (AS_00011003 refers to AC_00011003: section 2.3; the reference
  master numbers them so, per type), AT_..._01; APR_1001; ACO_ from 0x1001; AO_ from 0x1001 for beds (a bed of
  T tracks at O makes O+1 .. O+T-1 unusable) and from 0x100b for objects, sharing one counter; ATU_ from 0x00000001, continuous.
- **References**: audioStreamFormat carries BOTH audioChannelFormatIDRef and audioPackFormatIDRef (section 2.3); audioStreamFormatName = "PCM_" +
  the channel format's name; audioTrackFormatName = the stream's name; formatLabel 0001 / formatDefinition PCM on both. audioTrackUID: sampleRate
  (48000 / 96000, = the fmt chunk) and bitDepth 24, with audioTrackFormatIDRef and audioPackFormatIDRef. audioContent: `<dialogue
  mixedContentKind="0">2</dialogue>`. audioProgramme start / end: their difference = the length of the PCM.
- **Names**: audioObjectName / audioContentName come from Dolby's bed / object descriptions and groups; defaults `Atmos_Bed_M` (M in 1..128),
  `Atmos_Obj_N` (N in 10..128), `Atmos_Master_Content`. Any name 1..64 characters.
- **The file** (section 7): PCM (format 1), 24-bit, 48 / 96 kHz, at most 128 channels. Under 4 GB of RIFF data: "RIFF" with a JUNK chunk of at most
  64 bytes; 4 GB or more: **"RF64" (never "BW64")** with a ds64 chunk of at most 64 bytes. The chna chunk: numUIDs = numTracks, one UID per track,
  no unused entries. (A 9.1.6 file passes 4 GB after about 29 minutes at 48 kHz; several beds much sooner: RF64 is needed for real work.)
- **dbmd**: fermata does not write it (the Mastering Console does).

## The user's answers (6 November, 09:16)

- **LFE low-pass on export: yes**, with the frequency set by the user anywhere from **100 to 150 Hz** (start at 120 Hz).
- **File names: "Atmos" added** (the Virtual Master's name for the piece + " (Atmos)", e.g. "01 - Kyrie (Atmos).wav" - the 3.39.0 bounces put a
  bus's format in brackets the same way).
- **One bed or one per bus:** explained to the user (the mix as heard on the bus that feeds the speakers, written as one 7.1 bed + objects; or each
  surround bus written side by side as its own bed + objects, summed by the Mastering Console, so the reverbs can get another binaural mode than the
  dry sound - at the cost of bigger files and of any processing on the final bus). **The user's choice (09:19): each bus separately.** One design
  covers it (and the single-bed case): the tab lists the surround buses with a tick each, and every ticked bus is written as its own bed + objects,
  named after the bus. Worth handling: a ticked bus that feeds another ticked bus would be counted twice (say so, or refuse); what the ticked buses
  feed into (the speakers' Ext Bus: its fader, its inserts) is not in the file, so the tab should say so when that bus's fader is not at 0 dB or it
  has plug-ins; at most 128 channels in all (eight 9.1.6 buses).
- **A reference file:** the user offered an ADM file released on Apple Music. Read its chunk layout and its axml (and see what its dbmd holds) before
  writing the writer: it is what Dolby's tools write and what Apple accepted. If it is too big to upload, a short excerpt exported as ADM will do.

## The design (proposed - decide the open points with the user)

**Which sound.** Each ticked surround bus is written as its own bed + objects, named after the bus (the user's choice: see above), so the
Mastering Console can give the reverbs a different binaural mode from the dry sound. Ticking only the bus the speakers hear gives the single-bed file.

**Beds and objects for each format** - as the Composer builds its Composites (its manual: "a 7.1.4 Composite ... consists of a 7.1 bed and the four
height channels as dynamic objects"; the bed is the largest one the profile allows that the layout holds whole, which is why "Enforce 7.1.2 bed"
exists for a 7.1.4): 2.0, 5.1, 7.1 and 7.1.2 as beds; **5.1.4** = 5.1 bed + 4 objects (Ltf Rtf Ltr Rtr); **7.1.4** = 7.1 bed + 4 objects;
**9.1.4** = 7.1 bed + 6 objects (Lw Rw Ltf Rtf Ltr Rtr); **9.1.6** = **7.1.2 bed + 6 objects** (Lw Rw Ltf Rtf Ltr Rtr: fermata's top middles Ltm
Rtm are the bed's Lts Rts - the same position - so a 9.1.6 holds the whole 7.1.2 bed; the reference master is a 7.1.2 bed + objects too). (The
brief first guessed 7.1 + 8 for 9.1.6; `adm::layoutFor` does the above, and the test pins it.) The objects sit where the speakers are: `surround::info (spk).x / y / z` (Surround.h)
are already in the profile's room-centric coordinates (L -1,1,0 ... Ltf -1,1,1, Ltm -1,0,1, Ltr -1,-1,1); **Lw is at -1, 0.5, 0 there - check
against Dolby's own value** (the wides are the one position not in the profile's tables). The Composer has an "Enforce 7.1.2 bed" option for
distributors who demand one; leave it for later unless the user wants it.

**Channel order.** fermata's bus order is the Dolby / SMPTE one (L R C LFE Lss Rss Lrs Rrs Lw Rw Ltf Rtf Ltm Rtm Ltr Rtr); the file's tracks go bed
first (in the bed's order), then the objects - a fixed mapping per format, worth a table in Surround.h with a test.

**Rendering.** Each piece is bounced with the existing offline machinery (Bouncer: a surround bus is captured at its full width, 3.39.0) over the
Virtual Master's stretch, through each edit's own mixer as the Virtual Master does (`ownMixers`), the plug-ins told they are offline. The render
copy leaves the Atmos links out (Composer, Beams; **Spacelab too while a Composer is in the project** - if Spacelab is a reverb the user hears, the
export must say it was left out, or keep it for this render). 24-bit, dither as the Virtual Master's setting; no normalising (loudness is the
Mastering Console's job).

**Writing.** JUCE's WAV writer cannot write chna / axml and its RF64 is its own business, so the ADM file needs its own small writer: RIFF + JUNK
(or RF64 + ds64), fmt, chna, axml, data (word-aligned, pad bytes for odd sizes), sizes patched at the end. A pure function that builds the axml text
and the chna bytes from a description of the beds / objects makes the tests easy.

**The tab** (MasteringWindow: a fourth tab, "Atmos", view 3; its settings in MasteringDef, saved with it): the source bus; the pieces (the
Virtual Master's list: include, number, file name - defaulting to the Virtual Master's names), the folder, Export, progress; per file afterwards
"matches the stereo file to the sample" (length and start), and the reminders (48 kHz; open in the Mastering Console for the Dolby metadata and
the loudness). The settings in `MasteringDef` mean MasterDef.h / .cpp change: shared code, `"atmos"` in the mastering var (COMPATIBILITY.md
contract note).

**Testing here.** core_test can write a file and read it back: chunk layout and sizes, chna entries, the axml parsed (juce::XmlDocument) and checked
against the counting / ID / reference rules above, the PCM in the right tracks, the RF64 path (a test hook that switches at a small size).
**On the user's machine:** the files open in the Mastering Console with the bed and objects where they should be. A few seconds exported by the
Composer from the same project would be a perfect reference to compare the axml with - worth asking the user for.

## Built (6 November 2026, Claude Fable)

- `src/core/AdmWriter.h / .cpp`: `adm::layoutFor` (bed + objects per format), `adm::Description` / `check`, `buildAxml`, `buildChna`, `timeText`,
  `objectPosition`, `LfeLowPass`, `FileWriter` (RIFF + JUNK or RF64 + ds64, fmt, data, axml, chna; 24-bit words with TPDF dither; sizes patched at
  the end; a file not finished is deleted).
- `src/core/MasterExport.h / .cpp`: `AtmosJobSpec`, `makeAtmosSpec` (the Virtual Master's pieces and names, the ticked surround buses in the
  project's order, 48 / 96 kHz only), `atmosExportNotes` (twice in the file; the bus the ticked ones feed; Spacelab left out), `AtmosExportResult`,
  `AtmosExportJob` (a `MasterRenderBatch` with the ticked buses as surround stems, then one ADM file per piece; `rf64At` for the tests).
- `src/core/MasterRender.*`: `keepSurroundStems`. `src/core/MasterDef.*`: `AtmosExportSettings` (`"atmos"`), view 3.
- `src/ui/MasteringWindow.cpp / .h`: the Atmos tab (the Virtual Master's list on the left; the buses button with its tick list, the LFE low-pass
  and its frequency, dither, the notes, the folder, Export Atmos files / Show in folder, the progress bar and the status).
- `tests/core_test.cpp`: the section "3.40.0 fermata's own Dolby Atmos master (ADM BWF)": the layouts; the axml parsed and checked against the
  profile's rules (counts, the IDs and their continuity, every reference resolving, the elements' attributes and sub-elements, the names, the object
  blocks); the chna; the whole export through a small project (a 9.1.6 and a 5.1.4 bus: the chunk order and sizes, the fmt, the PCM landing in the
  right tracks, the file's length = the Virtual Master's); the RF64 path; the LFE filter's response.

To try on the user's machine: the files open in the Mastering Console with the beds and objects where they should be; the Mastering Console's
loudness / metadata / export round trip; a file over 4 GB (RF64).

## Files this will likely touch

Shared (src/core, both programs rebuild): a new AdmWriter.h / .cpp (and its line in `cmake/SharedCore.cmake`, plus the scratchpad's qcore.sh list),
MasterDef.h / .cpp (the tab's settings), maybe MasterExport.* / Bounce.* (the render), Surround.h (the bed / object table), tests/core_test.cpp.
fermata: src/ui/MasteringWindow.cpp / .h. Docs: CHANGELOG.md (3.40.0, with a Shared: line), COMPATIBILITY.md, docs/DEVELOPMENT-NOTES.md, this file.

## Also in 3.40.0: the post-fader slot (built by Opus, 6 November - for Fable to look over)

- `kPostSlot` / `kChanSlots` (Common.h): the strips' and buses' `slots[]` have one more element, the post-fader slot, so everything that keeps,
  copies, prepares or closes slots handles it; code meaning the four before the fader counts to `kNumSlots`.
- `Project::postFaderSlots` (Project menu > Post-fader slots; saved as `"postFaderSlots"` only when on); the slot saved as `"postSlot"` (a list of one)
  only when it holds something, so older versions and unused projects read and write as before. `Project::postSlotPlugins` for the question asked
  before switching off with plug-ins in such slots (they are kept, and left out of the sound).
- Engine (`AudioEngine.cpp`): `Node::post` (null while the switch is off) runs on `post` right after the fader and mute, before the meters; `Node::takesPost`
  decides which sends take its output (post-fader + follow mute; of a track only the stereo Direct ones - a track's send into a surround bus leaves from
  its own channels before the stereo fold); a bus's Pan / Fold / Across post-fader sends then read `post`. Delay compensation: `postLat`, `sendLat`,
  `postOutLat` (Ext Bus outputs, post-fader taps); the slot's posLat = after the inserts; the latency signature counts PRE / follow-mute while a post
  slot delays its channel.
- Mixer window: a row under the fader, above the OUT / Main out row (`StripLayout::postRow`, `kPostRowH`, `StripBase::placePostSlot`); linked strips
  (Shift + Alt) insert into each other's post slots; automation lanes can drive its plug-in ("Post-fader: name").
- Test: core_test section "3.40.0 the post-fader slot".
