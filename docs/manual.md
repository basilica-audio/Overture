<p align="center"><img src="assets/icon.png" alt="Overture icon" width="120"/></p>

# Overture — user manual

*The 808 boost — tighten your low end before the gain hits.*

## What it is

Overture is a TS-808-style tight boost/overdrive for metal guitar. It is not a full amp-in-a-box; it is the small pedal you'd run on the floor **in front of** a high-gain amp, doing exactly the job a "modded 808" does in a real rig:

1. Strip low end out of the signal *before* it hits any clipping/distortion, so palm mutes and low-string chugs stay tight and articulate instead of turning into a low-frequency mush once the amp's own gain stage saturates them.
2. Add a controlled amount of its own drive/clipping character on top, voiced to push the front of an already-distorted amp rather than to be a distortion pedal in its own right - as of v0.2.0, with genuinely frequency-selective, drive-dependent clipping behaviour rather than a fixed-shape nonlinearity (see [What changed in v0.2.0](#what-changed-in-v020) below).

## Where it sits in a heavy production chain

Overture is a **pre-amp tightening/boost stage**, not a cab sim, not an EQ, not a compressor. A typical chain:

```
Guitar -> noise gate -> Overture (tight boost) -> amp sim / real amp front end -> cab sim -> reverb/mix bus
```

Run it ahead of whatever provides the "wall of gain" in your chain (a real tube amp's input, or another plugin doing high-gain amp simulation). Overture's own Drive/Voicing controls are deliberately modest by default (see [Tips](#tips)) - the point is to *shape what hits the gain stage*, not to be the gain stage itself. If you want Overture's clipper to be the main distortion source (e.g. for a boost-only rig with a clean amp), push Drive further and pick a more aggressive Voicing - see the **Own Distortion** and **Fuzz-Adjacent Lead** factory presets.

## Signal flow

```
Input -> Gate (optional) -> Tight (HPF, 20-400 Hz) -> Drive (0-40 dB) -> [oversampled]
           Bite shelf (~700 Hz, inside the drive-to-clipper path)
           -> Voicing clipper (variable Asymmetry) -> Knee Soften blend
                                                                     |
      Output <-- Mix <-- Level <-- Bite Tilt (+/-3 kHz shelf) <-- DC blocker <-+
        ^
        |
   delay-compensated dry path (also used by Bypass; NOT gated)
```

With **Voicing** set to **Feedback**, the Bite shelf, the memoryless clipper and the Knee Soften blend are replaced by a single circuit-solved feedback-clipper stage, and the DC blocker is always active. The DC blocker is otherwise only in circuit when **Clip Quality** is set to Enhanced.

The clipper (and the Bite shelf ahead of it) runs inside an oversampled block (2x/4x/8x, selectable via **Oversampling**) so harmonics don't alias back into the audible band. The dry path used by **Mix** (and by **Bypass**) is automatically delay-compensated against that oversampling latency, and the plugin reports its total latency to the host so playback stays sample-accurately aligned with every other track. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown.

### Reported latency

Oversampling is the plugin's only source of reported latency, and the figure is handed to your host so its plugin delay compensation keeps every other track aligned. Measured at 48 kHz:

| Oversampling | Reported latency |
|---|---|
| 2x | 4 samples |
| 4x (default) | 6 samples |
| 8x | 6 samples |

A higher factor never reports *less* latency, but it does not always report more: the oversampler rounds each cascaded 2x stage's fractional latency to the nearest whole sample independently, which is why the 4x -> 8x step can land on the same total. The exact figures depend on the sample rate.

Nothing else in the plugin adds reported latency. The gate has no lookahead, and **Clip Quality = Enhanced** introduces a half-sample delay at the *oversampled* rate rather than at your host's - so switching the gate on, selecting the Feedback voicing or turning Enhanced on never changes what the host has to compensate for, and never shifts your track by a sample.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Tight** | 20 – 400 | 100 | Hz | High-pass filter placed *before* the clipper. Raising it strips more low end out of the signal that reaches the Drive/clipper stage, keeping palm mutes and low-string chugs tight instead of farting out once the amp's own gain stage saturates them. This is the core "808 mod" trick the plugin is built around. Lower it (toward 20 Hz) for a fuller, less tightened low end; raise it (toward 300-400 Hz) for maximum palm-mute articulation on drop-tuned guitars. v0.2.0's default (100 Hz, was 130 Hz) sits centrally inside the documented 80-120 Hz workflow sweet spot - see [What changed in v0.2.0](#what-changed-in-v020). |
| **Drive** | 0 – 40 | 3 | dB | Gain applied to the signal right before the clipper (selected by **Voicing**). At 0 dB the clipper barely engages; higher values push harder into the chosen nonlinearity. v0.2.0's default (3 dB, was 8 dB) sits in the best-documented "near-zero drive, Level does the pushing" region of the technique - see [What changed in v0.2.0](#what-changed-in-v020). |
| **Bite** | 0 – 100 | 65 | % | Frequency-dependent gain *inside* the drive-to-clipper stage (new in v0.2.0) - a fixed ~700 Hz low-shelf that progressively reduces the drive reaching the clipper below the shelf, scaled by this control, so bass is clipped *less* than treble. This is the actual mechanism the reference circuit uses for "tightness" (not a separate filter ahead of Drive - that's still Tight's job). At 0%, the clipper's gain is flat with frequency, identical to how v0.1's clipper behaved. |
| **Knee Soften** | 0 – 100 | 40 | % | Drive-dependent knee softening (new in v0.2.0) - blends each voicing's transfer function toward a softer-kneed variant, more pronounced the harder Drive is pushing the clipper. Applies to all three voicings, including Hard Clip (which otherwise has zero knee at any Drive level). At 0%, every voicing keeps its exact fixed-knee shape at every Drive level, matching v0.1. |
| **Asymmetry** | 0 – 100 | 40 | % | Exposes the Asymmetric voicing's internal bias (new in v0.2.0 - was a fixed constant in v0.1), mapping to a bias of 0.0 (fully symmetric) to 0.5 (maximally asymmetric). Soft Symmetric and Hard Clip ignore it entirely. The **Feedback** voicing also responds to it (as of v0.3.0), but by a different mechanism: instead of biasing a curve it morphs the *diode law itself* toward the asymmetric variant, so at 0% that stage is genuinely symmetric (its even harmonics disappear) and at 100% the second harmonic becomes a prominent part of what you hear. The default (40%) reproduces v0.1's fixed bias exactly for the Asymmetric voicing. |
| **Voicing** | Asymmetric / Soft Symmetric / Hard Clip / Feedback | Asymmetric | – | Selects the clipper nonlinearity the oversampled Drive/Bite stage feeds into. **Asymmetric** is the original "808 boost" voicing: a single-ended, biased tanh curve (op-amp/diode-style, bias set by Asymmetry), producing both odd and even harmonics for a slightly asymmetric, "tube-like" push. **Soft Symmetric** is a plain, unbiased tanh curve - smoother, more even-handed saturation with only odd harmonics, closer to a push-pull amp stage. **Hard Clip** is a straight clamp with no soft knee (unless softened via Knee Soften) - the brightest and most aggressive of the three, closer to a fuzz/comparator-style clip; use it when you want Overture itself to be doing real distortion work rather than just tightening/boosting. **Feedback** (new in v0.3.0) is a different animal from the other three: instead of a fixed transfer curve it solves the actual op-amp/diode/RC feedback loop sample by sample, so its gain, its knee and its high-frequency rounding all move together with how hard you are hitting it and with where Drive sits. Bite and Knee Soften have no effect in this voicing (the circuit's own ~720 Hz pre-emphasis *is* the bite mechanism, and its knee is physical), and Drive controls the circuit's feedback resistance rather than a gain stage in front of it. **Asymmetry** does still apply, as a change to the diode law itself - see its row above. **It is a programme-level clipper, not a clean boost**: it starts clipping around -40 dBFS at Drive 0 and a normal -12 dBFS track will drive it hard - that is the reference circuit's actual behaviour, and it is what makes the voicing touch-sensitive. Expect to pull **Level** down when you select it. Switching Voicing is a discrete change (like a stompbox toggle), not a smoothly-automatable control, so expect an audible step at the instant you switch, not a crossfade. |
| **Bite Tilt** | -100 – +100 | 0 | % | Post-clip bidirectional tilt around a fixed ~3 kHz corner (new in v0.2.0, replaces v0.1's cut-only Tone). Negative values darken (subsuming v0.1's entire Tone cut range); positive values brighten - a capability v0.1 entirely lacked. Flat (0%, the default) is a true no-op. See [What changed in v0.2.0](#what-changed-in-v020) for how an old v0.1 session's Tone setting maps onto this control. |
| **Level** | -24 – +24 | 0 | dB | Output trim, applied after Bite Tilt and before the dry/wet Mix. Use it to match Overture's output level to the rest of your chain, especially if you've pushed Drive hard. |
| **Mix** | 0 – 100 | 100 | % | Dry/wet blend of the whole "wet" chain (everything from Tight through Level) against the untouched input. At 100% (the default) Overture behaves like a real boost pedal - fully in the signal path. Lower values blend in some of the original, unprocessed signal; at exactly 0% the output is a sample-accurate (delay-compensated) passthrough of the input. |
| **Bypass** | Off / On | Off | – | Host-visible bypass. Unlike a plain "mute the plugin" bypass, Overture keeps its internal oversampler running while bypassed so the reported plugin latency (and your host's delay compensation) never changes - engaging/disengaging Bypass crossfades smoothly (over roughly a tenth of a second) rather than clicking or popping, and never introduces a timing glitch on other tracks. |
| **Gate** | Off / On | Off | – | Built-in noise gate (new in v0.3.0), placed at the very front of the wet chain - before Tight, before Drive - so hum and hiss never reach the clipper at all. Off by default; switching it on mid-note neither clicks nor mutes. The **dry** path (anything you blend in with Mix below 100%) is deliberately *not* gated: the gate is part of the pedal chain, the way an outboard gate in front of an amp would be. Detection is **stereo-linked** - one detector fed by both channels, one gain applied to both - so the stereo image never wanders as the gate works. It opens fast enough not to blunt a pick attack (the gain travels from 10% to 90% in well under a millisecond), and when shut it attenuates deeply rather than hard-muting: the floor is 90 dB down, not digital silence. Adds no latency. |
| **Gate Threshold** | -80 – -20 | -50 | dB | The level the gate opens at. The detector is a 5 ms mean-square follower, so a full-scale sine reads about -3 dB and a typical high-output DI's noise floor sits around -60…-45 dB. The gate *closes* 4 dB below this (fixed hysteresis), which is what stops it chattering on a decaying note. Start well below your playing level and raise it until the noise between chugs disappears. |
| **Gate Release** | Auto / Fast / Slow | Auto | – | How fast the gate closes once you stop playing. **Auto** watches the programme itself: an abrupt palm mute closes the gate almost instantly, while a ringing chord is released at its own decay rate so the tail is never truncated - one setting for both, and the reason this is the default. **Fast** (800 dB/s) and **Slow** (60 dB/s) are fixed alternatives for when you want a specific, predictable behaviour. |
| **Knee Response** | Drive / Signal | Drive | – | Where Knee Soften's intensity comes from (new in v0.3.0). **Drive** scales it by the Drive knob, exactly as v0.2.0 did. **Signal** scales it by how hard the clipper is *actually* being hit, so a quiet passage at Drive 40 keeps a hard knee and a slammed input gets the soft one - closer to how the circuit behaves, and generally the more musical choice for dynamic playing. |
| **Clip Quality** | Classic / Enhanced | Classic | – | A configuration choice, not a performance control (new in v0.3.0). **Classic** is the exact v0.2.0 clipper path, bit for bit. **Enhanced** adds antiderivative anti-aliasing to the three memoryless voicings plus a 5 Hz DC blocker on the clipper output: measurably cleaner (about 19 dB less alias energy at 2x oversampling, and roughly what plain 4x buys you, for far less CPU), with the harmonic content otherwise unchanged. Leave it on Classic if you need a session to null bit-exactly against an older one; otherwise Enhanced is the better-sounding setting, especially at low oversampling factors. It has no effect on the Feedback voicing, which is a circuit solver rather than a transfer curve. |
| **Oversampling** | 2x / 4x / 8x | 4x | – | Oversampling factor around the clipper (and Bite shelf). Higher factors give a cleaner (less aliased) clipper at the cost of more CPU. **Changing this parameter takes effect the next time your host re-initialises the plugin** (e.g. on transport stop/start, a sample-rate change, or reopening the project) - not instantly while audio is running. This is a deliberate real-time-safety choice: reconfiguring the oversampler requires a memory allocation, which must never happen on the audio thread. If you want to hear a change immediately, stop and restart playback (or reopen the plugin) after changing it. |

## Presets

Overture ships with eleven factory presets (a certified **Default** plus ten use-case-driven starting points - see [`docs/presets.md`](presets.md) for the full list and intent behind each). The preset bar docked at the top of the editor lets you browse factory/user presets, save your own (`~/Library/Audio/Presets/Yves Vogl/Overture/` on macOS), import/export single presets or zip banks, and mark any preset (including your own) as the default that loads on a fresh instance.

## What changed in v0.3.0

v0.3.0 adds a built-in gate, a circuit-solved clipper voicing, an opt-in anti-aliasing mode and a signal-dependent knee. **Every new control defaults to a neutral value, so a v0.2.0 project loads and sounds bit-identical.**

- **Gate / Gate Threshold / Gate Release** (new): see the parameter reference above. Off by default.
- **Voicing gains a fourth entry, Feedback** (new): a circuit-solved feedback clipper rather than a transfer curve. Read the Voicing row above before you reach for it - it clips at programme level by design.
- **Clip Quality** (new): Classic keeps the v0.2.0 path bit-identical; Enhanced adds anti-aliasing and a DC blocker.
- **Knee Response** (new): Drive keeps the v0.2.0 behaviour; Signal derives the knee from the actual signal level.
- **Automation smoothness**: filter coefficients now update roughly sixteen times more often per block, so automating Tight or Bite Tilt no longer zippers.

### Where the new controls are (interim state)

**v0.3.0 ships the five new parameters without dedicated on-screen controls.** They are fully automatable and appear in your host's generic parameter view (in Logic: the plugin's "Controls" view; in Reaper: the FX parameter list; in Live: the plugin's parameter list) - they simply have no knob in Overture's own window yet. Photoreal controls for all five arrive with the M3 GUI. The one editor-visible change in this release is the Voicing menu's new **Feedback** entry, which appears automatically.

### Two compatibility notes

- **If you have automated the Voicing menu, re-check those lanes.** Saved projects and presets store the voicing by *index*, so they load exactly as before. Host automation lanes, though, store a normalised 0-1 value: a lane at maximum used to mean "Hard Clip" (2 of 2) and now means "Feedback" (3 of 3).
- **Reported latency is unchanged** at every oversampling factor. The gate adds none, and Enhanced's half-sample delay lives at the oversampled rate.

## What changed in v0.2.0

v0.2.0 is a research-driven rework of the Drive -> Clipper -> Tone portion of the chain, sourced from published circuit analyses of the reference-class "tube-screamer-in-front-of-a-high-gain-amp" technique, a purpose-built commercial pedal's own documentation, and publicly reported artist workflows - **not measured against physical reference hardware or original-manufacturer schematics/datasheets by this project**. See [`docs/research-notes.md`](research-notes.md) for the full sourced findings and [`docs/design-brief.md`](design-brief.md) for the reasoning behind every change below.

- **Bite** (new) replaces the assumption that a pre-clip filter alone (Tight) fully explains "tightness" - the reference circuit actually clips bass *less* than treble, dynamically, inside its own clipping stage. Bite reproduces that mechanism; Tight keeps doing its original, separate pre-clip job.
- **Knee Soften** (new) and **Asymmetry** (new) expose behaviours v0.1's clippers had no control over: a knee that softens as Drive increases, and a variable degree of asymmetric bias (previously fixed).
- **Bite Tilt** replaces Tone: the reference circuit's tone control is a boost/cut tilt around a fixed corner, not a cut-only low-pass. An old session's Tone value is lossily, automatically mapped onto an equivalent Bite Tilt position on load (fully closed Tone -> maximally dark Bite Tilt; fully open Tone -> flat) - not a mathematically exact equivalence, since the two controls have genuinely different shapes.
- **Defaults changed**: Tight 130 -> 100 Hz (the documented 80-120 Hz workflow sweet spot's midpoint); Drive 8 -> 3 dB (the best-documented canonical workflow is near-zero clipper drive, with Level/the amp doing the actual distorting - see the **Clean Push** and **Classic Boost** presets).
- Several new defaults (Bite 65%, Knee Soften 40%, Asymmetry's 0.5 mapping ceiling, Bite Tilt's ~3 kHz corner) are **reasoned engineering choices anchored to the sourced qualitative behaviour, not numbers taken directly from a source** - flagged individually in `docs/design-brief.md`, not represented as measured hardware values.
- "Tube Screamer," "Horizon Devices Precision Drive," "Misha Mansoor," and "Ola Englund" are cited in the research notes as documented public sources for the *technique*, without implying endorsement, sponsorship, or affiliation by any person or brand.

## Tips

- **Start with Tight, not Drive.** The whole point of the "808 boost in front of a high-gain amp" trick is the high-pass filter, not the clipper. Dial in Tight first (100–200 Hz is a good starting range for drop-tuned rhythm parts) with Drive low, and only add Drive once the low end feels controlled.
- **Try Drive near zero first.** The most-documented version of this technique pushes an already-driven amp with the clipper barely engaged (Drive 1–3 dB) and lets Level and the amp's own gain stage do the actual distorting - see the **Clean Push** preset. Turning Drive up further (10 dB+) makes Overture more of a distortion source in its own right, which is also a legitimate, supported use (see **Own Distortion**/**Fuzz-Adjacent Lead**) but a different character.
- **Bite is the "tightness" control for the clipper itself**, distinct from Tight's pre-clip filtering. Raise it for more frequency-selective (bass-forgiving) clipping character, especially audible at higher Drive; 0% gives you v0.1's plain, frequency-flat clipper.
- **Knee Soften rounds off harsh corners, especially at higher Drive.** It's most dramatic on Hard Clip (which has zero knee at 0%) and least noticeable on Soft Symmetric (already the softest-kneed voicing to begin with).
- **Keep Drive modest if you're pushing a real amp/amp-sim afterwards.** Overture's clipper is meant to nudge the front end of whatever gain stage comes next, not fight it. If everything starts sounding thin or brittle, back Drive off before reaching for Bite Tilt.
- **Use Voicing to match the character you're after**, not just to add more gain. Asymmetric (default) for a classic 808-in-front-of-a-Marshall push; Soft Symmetric for a smoother, more "amp-like" saturation that sits well under a high-gain amp sim; Hard Clip when you want Overture itself to be a genuinely distorted signal (e.g. driving a clean amp, or as a fuzz-adjacent lead boost).
- **Bite Tilt is a cleanup *and* a voicing tool now.** Negative values tame fizz introduced by the clipper (like v0.1's Tone did); positive values brighten in a way v0.1 couldn't do at all - see the **De-Fizz Cleanup** and **Fuzz-Adjacent Lead** presets for both directions.
- **Mix below 100% is for parallel/blended tones**, e.g. blending a small amount of tightened/driven signal under a clean DI for a hybrid rhythm tone - see the **Parallel Grit** preset. For a normal "boost pedal in front of the amp" use case, leave Mix at 100%.
- **Set the gate threshold from the noise, not from the notes.** Mute your hands, look at what the noise floor is doing, and put the threshold just above it. The 4 dB of built-in hysteresis and the 20 ms hold mean you do not need to leave a huge safety margin - and with Gate Release on Auto you do not need to trade "cuts off my sustain" against "leaves the hiss in", which is the compromise a single fixed release time forces.
- **The gate is on the wet path only.** If you run Mix below 100% you will still hear the ungated dry signal underneath. That is deliberate - the gate belongs to the pedal chain, not to the plugin as a whole - but it does mean a parallel-blend patch needs its gating done elsewhere.
- **Feedback voicing: drop Level before you audition it.** It has 21 dB of in-band gain built into the circuit at Drive 0, and Drive adds more on top. `Circuit Drive` ships a vetted starting point (Level -9 dB); start from there rather than from your current patch.
- **Enhanced clip quality is close to free.** It costs about one extra transcendental per oversampled sample and buys roughly what doubling the oversampling factor would. If you are running 2x to save CPU, turning Enhanced on is the better trade.
- **Bypass, not Mix at 0%, for A/B comparisons during a mix.** Both null the wet chain, but Bypass is the one hosts treat as "native" bypass (automation lane, right-click bypass in most DAWs), and it's what keeps latency reporting stable if you're comparing across multiple instances.
- **Leave Oversampling at 4x unless you have a specific reason to change it.** 2x saves CPU at a small aliasing cost (mostly audible on very high Drive + Hard Clip); 8x is for tracking/committing a final take where you want the cleanest possible clipper at the cost of extra CPU load.
