# Surround Int and Ext buses (fermata 3.39.0)

fermata only. fermata:Live gets no surround (it keeps a project's bus formats, as the shared project code saves them, and its own mixer
only ever uses a stereo main Ext Bus, which a surround bus can never be).

This file is the plan, what is built, and the work list for the part still to do. It was written so that a fresh session (the user's plan:
Part 2 written with Claude Fable) can pick the work up without the conversation that led to it.

## What the user asked for (5 November 2026)

> "one of the things I will need to add to fermata (but will not need to be added to fermata:Live), is surround, or multichannel int busses
> and ext busses. In my Dolby Atmos mixing workflow I will be using Fiedler Audio Dolby Atmos Composer to create dolby atmos files. These are
> rendered during fermata's export process OR when playing back. ... I will still record only in mono or stereo audio tracks but I will use 9.1.6
> int busses as my reverb channels to create surround atmos beds routed into the DAC (Dolby Atmos Composer). Can you please code in the
> ability to have surround int and ext busses, suggest to me which ones might be appropriate based on what are industry standards."

Then: "I will be using liquid sonics 7th heaven in 9.1.6 mode to create dolby atmos reverbs - I also have the fiedler audio mastering
console, spacelab ignition reverb and Gravitas mastering dynamics tool which I will use in that work flow (well, I might do, but nailing the
DAC is the top priority)." And: "PLEASE tell me when there is a bit that would really benefit from using Fable."

The user's answers to the design questions:
- **Formats:** all of them: 5.1, 7.1, 7.1.2, 5.1.4, 7.1.4, 9.1.4, 9.1.6 (plus stereo, as every bus was).
- **A mono / stereo track into a surround bus:** a surround panner on each send (front-back, left-right, height, spread).
- **Reverb:** LiquidSonics (Seventh Heaven Professional in 9.1.6; Cinematic Rooms was the first answer). Spacelab Ignition maybe.
- **Monitoring:** speakers (9.1.6) and headphones (binaural) at once.
- **Fable:** "Parts 1+3 now, Part 2 by Fable": Claude (Opus) built the groundwork and the windows; the audio engine is for Fable.

## What the manuals say that the design must follow

From the Dolby Atmos Composer (DAC), Spacelab Ignition, Gravitas MDS and Mastering Console manuals the user uploaded (all Fiedler Audio):
- **The Composer goes on the last track, like a master.** It ignores the audio the host gives it. Beams (and Spacelab) send it their audio and
  positions directly, inside the process. It outputs the rendered monitor feed: speaker channels first and then, when headphone monitoring is on
  too, a binaural pair (DAC manual, "Monitoring"). Speakers and headphones at once is a full-version feature (not Essential).
- **Only one Composer may run on a computer** ("not in different sessions nor in different DAWs").
- **Processing order:** every track with a Beam or Spacelab must be routed (directly or through other tracks) to the Composer's track, so they are
  processed first in each block. fermata's plan orders every node after the nodes that feed it, so a bus that leads to the Composer's bus is run first.
- **The track format tells the plug-in its layout.** A Beam recognises its track's format (its VST3 speaker arrangement), up to 16 channels. The
  Composer sets its speaker layout from its track's format when they match; otherwise it plays the channel order shown under its meters.
- **Composite formats** up to 9.1.6 (a 7.1.2 bed plus objects). Spacelab's default output is "16 - 9.1.6 (Dolby Atmos)".
- **Speaker positions** (the Composer's table): L/R 30, C 0, Ls/Rs 110, Lss/Rss 90, Lrs/Rrs 135, Lw/Rw 45, Ltf/Rtf 45 at 45 up, Ltm/Rtm 90 at 45 up,
  Ltr/Rtr 135 at 45 up.
- **Downmixes:** 7.1 to 5.1 "standard": Ls = Lss + Lrs (0 dB each). 5.1 to stereo Lo/Ro: Lo = L + C at -3 dB + Ls at -3 dB (no LFE).
- **Sample rate:** 48 or 96 kHz only; best at a 512-sample buffer at 48 kHz, 1024 at 96 kHz.
- **Export:** the Composer itself writes the ADM/BWF file while its in-point-to-out-point range is processed, in real time or in the host's offline
  export. The pass must start at or before the in-point and reach past the out-point. The host must not switch the Composer off in silence.
- **Latency:** Spacelab (and the Composer, which aligns the incoming connections) need a "latency compensated song position": hosts that tell every
  plug-in the same position (Pro Tools partly, Samplitude, Sequoia) need a manual offset typed into each Beam.
- **Post-fader inserts** are recommended for Beams, so the Beam gets the fader and pan moves. (fermata has none: its inserts are before the fader.
  A Beam on a bus follows the bus's input gain and inserts, not its fader. 3.40.0: the post-fader slot - Project menu > Post-fader slots - is the place for it.)
- **Seventh Heaven Professional:** VST3 track widths up to 9.1.6 (from v1.4.5: 7.x.4, 7.x.6, 9.x.6; v1.4.6: 5.x.2, 5.x.4, 9.x.4). Its web page
  does not say whether input and output can differ in VST3 (it mentions asymmetric up-mixing in AAX).
- **Gravitas MDS:** multichannel VST3 up to 128 channels; shows LFE controls when its track's format has an LFE. Also an OBAM plug-in inside the
  Composer's own Master Channel (nothing for fermata to do).
- **Mastering Console:** a separate program working on the exported ADM/BWF files. Nothing in fermata changes for it.

## The design

- **BusDef::format** (`src/core/Surround.h`, `BusFormat`): Stereo (every bus until now), 5.1, 7.1, 7.1.2, 5.1.4, 7.1.4, 9.1.4, 9.1.6, and for an
  Ext Bus only two monitor formats for the Composer's bus: **7.1.4 + headphones** (14 channels) and **9.1.6 + headphones** (18).
- **Channel order** (fermata's, everywhere: the bus's buffer, its meters, its driver outputs, its files): the Dolby / SMPTE order of the Dolby
  Atmos Renderer: L R C LFE, then Ls Rs (5.1) or Lss Rss Lrs Rrs, then Lw Rw, then the tops front to back (Ltf Rtf, Ltm Rtm, Ltr Rtr), then HpL HpR.
  A JUCE-hosted plug-in wants another order (JUCE orders a channel set by channel type): `surround::juceIndex` maps each channel.
- **Into a surround bus:** a mono or stereo source (an audio track; a stereo Int Bus) is placed by the send's panner (`SendState::panX ...`,
  `surround::pointGains`, `sourceMatrix`); a wider source goes across channel for channel, folded where the bus lacks a speaker (`foldMatrix`).
  **Out of a surround bus into a stereo one:** Lo/Ro (`foldMatrix` to Stereo). Surround into another surround format: `foldMatrix`.
- **The panner** (room-centric, as Dolby's bed panners): rows of speakers (front, wides, sides, rear) crossfaded by y, speakers in a row by x, the
  ear-level and top layers by z, all at constant power; spread blends towards every speaker alike; centre (0..1) how much C is used between L and R.
  Defaults: front, ear level, left-right following the channel's own pan (a stereo track lands on L and R exactly; a mono spot mic where its pan is).
- **A surround Ext Bus** goes to one driver output per speaker channel, from `BusState::outFirst`, in order; a monitor format's headphone pair to
  `BusState::hpFirst`. It is never a main output (`isMain`): 'Main', the talkback and the cue mixers that follow the processing mixer stay stereo.
- **Insert layouts:** each slot knows the layout its plug-in is prepared with (`InsertSlot::setLayout`, the code passed to
  `InsertProcessor::prepare`: 2 = stereo, or the BusFormat value). A bus's own slots get its format; a send's (and an output's) slots get the format
  of where it goes (they process what goes into that bus); a strip's own slots stay stereo. `Project::applySlotLayouts` keeps them right (called
  by syncMixers, so on every plan).
- **The Atmos plug-ins are never copied into a render** (`Project::cloneForRender`): a bounce works on a private copy of the project with fresh
  plug-ins, and a second Composer cannot run, while a copy of a Beam (or of Spacelab when a Composer is present) would join the live Atmos mix while
  the bounce ran. Their slots pass the sound through in the copy; `pluginsMissingFrom` does not count them; the bounce window says they were left out.
- **The Atmos file** comes from the Composer while the piece is played through the live mixer (an "Atmos pass": Part 2). A faster-than-real-time
  export through the live mixer (stopping the device and running the live plan offline) is possible later; it is a bigger job.

## Built (Parts 1 and 3, on the `surround` branch, 5-6 November 2026)

Shared (`src/core`, both programs rebuild):
- `src/core/Surround.h` (new): the formats, their channels and speaker positions, the panner, the fold-downs, the JUCE channel sets and order.
- `src/core/Mixer.h`: `InsertProcessor::prepare` third argument is the layout; `InsertSlot::setLayout` / `layout()` (the slot's own layout is
  used by set / setKeeping / reprepare; their old channel-count argument is ignored); the scratch buffer is `kMaxBusChannels` wide; `SendState` panner
  (`panX panY panZ panWidth panSpread panCentre panFollow`, `panPos()`, `setPanPos()`, `panIsDefault()`); `BusState::hpFirst`, `chPeak[]`, `chRms[]`.
- `src/core/Project.h / .cpp`: `BusDef::format` (saved as `"format": "9.1.6"` only when not stereo; read back as stereo when missing or unknown, or
  when an Int Bus claims a monitor format); `addBus (name, external, format)`; `busFormat`, `setBusFormat` (clears Main on a surround Ext Bus,
  re-applies layouts), `applySlotLayouts`, `atmosLinkOf`, `hasAtmosComposer`; a send's panner saved as `"surroundPan"`, an output's as
  `"outSurroundPan"` (only when not the defaults); `"hpFirst"` with the Ext Bus's outputs; the first STEREO Ext Bus becomes a new mixer's main output;
  `cloneForRender` leaves the Atmos plug-ins out; `outputFedBy` / `mixerOutputsText` count a surround bus's outputs.
- `tests/core_test.cpp`: the section "3.39.0 surround buses (part 1)".

fermata's windows:
- `src/ui/SurroundUi.h` (new): `SurroundMeterBank` (a meter per channel, grouped), `SurroundPanner` (top view, height, spread, centre, width,
  follow pan), `SurroundPanButton`.
- `src/ui/MixerWindow.cpp`: + Strip > Int Bus / Ext Bus > a format; right-click a bus's name plate > Format; the coloured bar says "INT 9.1.6";
  a surround bus strip is wider, with a meter per channel (fed by `BusState::chPeak` / `chRms`: Part 2); no balance on a surround Int Bus; a surround
  Ext Bus picks "Out 1-16" (one output per speaker) and, in a monitor format, "Phones 17-18"; Main out greyed on a surround Ext Bus; a send panel into a
  surround bus has the panner (or a line saying how a wider source goes across / folds); a strip output into a surround bus gets the round panner button;
  the status line warns about two Composers, a rate other than 48 / 96 kHz, or the Composer's mixer going to no outputs.
- `src/ui/DesignWindow.cpp`: a Format column on the Int Buses and Ext Buses pages.
- `src/ui/BounceWindow.cpp`: names the Atmos plug-ins a bounce left out.

## Built (Part 2, 6 November 2026, with Claude Fable)

Shared:
- `src/core/AudioEngine.h / .cpp`: every node has a `width` (2 for a track's stereo fold and a stereo bus; a surround bus's format's count), a
  `format`, `speakerCh` and (surround) its Lo/Ro fold for the phase scope. `incoming`, `pre`, `post`, `preSend` and `sendTmp` run at that width (the work
  buffers are `kMaxBusChannels` wide). A bus's inserts, input gain, low cut (`ensureChannels (W)`), polarity, fader and taps work on all its channels;
  the balance only on a stereo bus. Meters: `chPeak[]` / `chRms[]` per channel, `meterL / R` (and the remote ones) the loudest channel of each side
  (C and LFE on both). `SendPlan::kind` (Direct, Pan, Fold, Across) is decided at plan time: a mono / stereo track or a stereo bus into a surround
  bus is placed by the send's panner (gains worked out on the audio thread from the live `SendState::pan*` and the strip's own pans, crossfaded from
  the last block's over the block), a surround bus into a stereo channel or another format goes through `surround::foldMatrix`, the same format
  straight across. A send's inserts, delay line and level then work on the destination's channels. Delay compensation: `sp.delay` at the
  destination's width, `outDelay` / `tapDelay` at the bus's. An Ext Bus writes `speakerCh` channels to `outFirst + c` and a monitor format's last two
  to `hpFirst`, `hpFirst + 1` (`touched`, the dither and `mixerHasOutput` count them; the mono check keeps to stereo buses). `isMainOut` is never a
  surround bus. `MixerPlan::keepRunning`: the mixer with the Composer runs whatever its outputs go to. `InsertSlot::posLat` (the lateness of the sound
  entering each slot, without the spot-mic alignment) is written with `autoLat`, for the strips', the buses', the sends' and the outputs' slots.
- `src/core/Mixer.h` / `HostTransport.h`: `InsertSlot::process` sets `currentSlotLatency()` (thread-local) before running its plug-in;
  `InsertProcessor::layoutNote()` and `setNonRealtime()`; `BusState::chMs[]`.
- `src/core/Surround.h`: `pluginChannelSet` (a monitor format: the speakers' set + Lc + Rc, 18 channels), `vst3Rank`, `pluginIndex` (by speaker for
  a surround format, BY POSITION in VST3 bit order for a monitor format), `speakersOf`.
- `src/core/Bounce.cpp`: a capture as wide as its node; a surround file written with all its channels in fermata's order, the format in the file
  name ("... - Verb (9.1.6).wav"); 5.1 gets the standard WAV channel mask (the same order), the rest are plain channels (no mask: the WAV mask has
  no bits for the wides or the top middles, and JUCE would call 8 plain channels 7.1 SDDS). The copy's plug-ins are told `setNonRealtime (true)`.
- `src/core/MasterRender.cpp`, `MasterExport.cpp`: the Mastering window's source and stems are never a surround bus.
- `src/core/Common.h`: `kMaxBusChannels` (18) lives here now; `LowCut.h` takes that many channels.
- `tests/core_test.cpp`: the section "3.39.0 surround buses (part 2)": a 9.1.6 bus fed through the default panner (L, R, C), a moved panner against
  `pointGains`, the crossfade of a panner jump, 9.1.6 into 7.1.4 into stereo (the folds), delay compensation across the surround path, `posLat` and
  `currentSlotLatency`, the headphone pair on `hpFirst`, per-channel meters, a 16-wide offline capture, a 16-channel bounce file, the plug-in
  channel order (by speaker, and the monitor format's by position).

fermata:
- `src/ui/PluginHost.cpp` (`Vst3Insert`): `prepare` asks the plug-in for the bus's layout on its main input and output (then, for a monitor format,
  the speakers alone; then stereo in with the layout out; then stereo in and out), and logs which took; `inMap / outMap` from the layouts it really
  has (`pluginIndex`; a stereo-only plug-in works on L and R and the other channels pass it by); `process` hands the channels over by those maps;
  `getPosition` takes `currentSlotLatency()` off the transport position; `layoutNote`, `setNonRealtime`.
- `src/ui/MixerWindow.cpp`: the slot's tooltip carries the plug-in's layout note; the **Atmos pass** button (shown while a Composer is loaded) plays
  the edit window last used from 3 s before its first sound to 10 s after its last (`EditWindowComponent::playAtmosPass`); the "goes to no outputs"
  note now says the Composer still runs.
- `src/ui/EditWindow.h / .cpp`: `playAtmosPass`.
- `src/ui/MasteringWindow.cpp`: surround buses left off the source and stem lists.

### Decisions worth knowing
- **The monitor formats hand the channels over by position** (fermata's order = the Composer's: L R C LFE Lss Rss Lrs Rrs Lw Rw Ltf Rtf Ltm Rtm Ltr
  Rtr, then the headphone pair), because the Composer's manual says a layout it does not know plays "in the channel order shown below the meters".
  The 18-channel arrangement it is given is 9.1.6 + Lc + Rc. If the Composer turns out to honour the speaker types of that arrangement instead, the
  one-line change is in `surround::pluginIndex` (use `juceIndex` of the speakers' set for c < 16 and the Lc / Rc indices for the pair). If it refuses
  18 channels altogether, `Vst3Insert::prepare` falls back to the 16-channel 9.1.6 set and the slot's tooltip says "no headphone pair".
- **A surround bus's inserts come before its fader**, as every insert does. The manuals recommend post-fader Beams; a Beam on a bus follows the
  bus's input gain and inserts, not its fader. 3.40.0 added the post-fader slot (under the fader; Project menu > Post-fader slots): a Beam there
  follows the fader, its automation and the mute.
- **The Atmos pass is a real-time play** (the Composer writes its file while the live mixer plays). A faster-than-real-time pass through the live
  mixer is possible later (stop the device, run the live plan offline), a bigger job.
- **The scope on a surround bus shows its Lo/Ro fold**; the meter bridge and the control surface see `meterL / meterR` (the loudest of each side).

## To try on the user's machine (only possible there)

- Seventh Heaven Professional on a 9.1.6 Int Bus: does it come up in 9.1.6 (its own display) with the right channels lit for a front-left send?
- A Dolby Atmos Beam on a 9.1.6 bus: does it recognise 9.1.6 (Composite)? A 7.1.4 bus?
- The Composer on a "9.1.6 + headphones" Ext Bus to outputs 1-16 and a phones pair: speakers and binaural at once?
- Timing: a Beam after a plug-in with latency stays in time with one without (after Part 2's per-slot position).
- The Atmos pass writes a complete ADM/BWF file (the Composer shows 100 % and success).
- A bounce of a project with the Composer and Beams: no second Composer, nothing joins the live mix.
