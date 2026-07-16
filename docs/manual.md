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
Input -> Tight (HPF, 20-400 Hz) -> Drive (0-40 dB) -> [oversampled]
           Bite shelf (~700 Hz, inside the drive-to-clipper path)
           -> Voicing clipper (variable Asymmetry) -> Knee Soften blend
                                                                |
      Output <-- Mix <-- Level <-- Bite Tilt (+/-3 kHz shelf) <-+
        ^
        |
   delay-compensated dry path (also used by Bypass)
```

The clipper (and the Bite shelf ahead of it) runs inside an oversampled block (2x/4x/8x, selectable via **Oversampling**) so harmonics don't alias back into the audible band. The dry path used by **Mix** (and by **Bypass**) is automatically delay-compensated against that oversampling latency, and the plugin reports its total latency to the host so playback stays sample-accurately aligned with every other track. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Tight** | 20 – 400 | 100 | Hz | High-pass filter placed *before* the clipper. Raising it strips more low end out of the signal that reaches the Drive/clipper stage, keeping palm mutes and low-string chugs tight instead of farting out once the amp's own gain stage saturates them. This is the core "808 mod" trick the plugin is built around. Lower it (toward 20 Hz) for a fuller, less tightened low end; raise it (toward 300-400 Hz) for maximum palm-mute articulation on drop-tuned guitars. v0.2.0's default (100 Hz, was 130 Hz) sits centrally inside the documented 80-120 Hz workflow sweet spot - see [What changed in v0.2.0](#what-changed-in-v020). |
| **Drive** | 0 – 40 | 3 | dB | Gain applied to the signal right before the clipper (selected by **Voicing**). At 0 dB the clipper barely engages; higher values push harder into the chosen nonlinearity. v0.2.0's default (3 dB, was 8 dB) sits in the best-documented "near-zero drive, Level does the pushing" region of the technique - see [What changed in v0.2.0](#what-changed-in-v020). |
| **Bite** | 0 – 100 | 65 | % | Frequency-dependent gain *inside* the drive-to-clipper stage (new in v0.2.0) - a fixed ~700 Hz low-shelf that progressively reduces the drive reaching the clipper below the shelf, scaled by this control, so bass is clipped *less* than treble. This is the actual mechanism the reference circuit uses for "tightness" (not a separate filter ahead of Drive - that's still Tight's job). At 0%, the clipper's gain is flat with frequency, identical to how v0.1's clipper behaved. |
| **Knee Soften** | 0 – 100 | 40 | % | Drive-dependent knee softening (new in v0.2.0) - blends each voicing's transfer function toward a softer-kneed variant, more pronounced the harder Drive is pushing the clipper. Applies to all three voicings, including Hard Clip (which otherwise has zero knee at any Drive level). At 0%, every voicing keeps its exact fixed-knee shape at every Drive level, matching v0.1. |
| **Asymmetry** | 0 – 100 | 40 | % | Exposes the Asymmetric voicing's internal bias (new in v0.2.0 - was a fixed constant in v0.1), mapping to a bias of 0.0 (fully symmetric) to 0.5 (maximally asymmetric). Only affects the Asymmetric voicing - Soft Symmetric and Hard Clip ignore it. The default (40%) reproduces v0.1's fixed bias exactly. |
| **Voicing** | Asymmetric / Soft Symmetric / Hard Clip | Asymmetric | – | Selects the clipper nonlinearity the oversampled Drive/Bite stage feeds into. **Asymmetric** is the original "808 boost" voicing: a single-ended, biased tanh curve (op-amp/diode-style, bias set by Asymmetry), producing both odd and even harmonics for a slightly asymmetric, "tube-like" push. **Soft Symmetric** is a plain, unbiased tanh curve - smoother, more even-handed saturation with only odd harmonics, closer to a push-pull amp stage. **Hard Clip** is a straight clamp with no soft knee (unless softened via Knee Soften) - the brightest and most aggressive of the three, closer to a fuzz/comparator-style clip; use it when you want Overture itself to be doing real distortion work rather than just tightening/boosting. Switching Voicing is a discrete change (like a stompbox toggle), not a smoothly-automatable control, so expect an audible step at the instant you switch, not a crossfade. |
| **Bite Tilt** | -100 – +100 | 0 | % | Post-clip bidirectional tilt around a fixed ~3 kHz corner (new in v0.2.0, replaces v0.1's cut-only Tone). Negative values darken (subsuming v0.1's entire Tone cut range); positive values brighten - a capability v0.1 entirely lacked. Flat (0%, the default) is a true no-op. See [What changed in v0.2.0](#what-changed-in-v020) for how an old v0.1 session's Tone setting maps onto this control. |
| **Level** | -24 – +24 | 0 | dB | Output trim, applied after Bite Tilt and before the dry/wet Mix. Use it to match Overture's output level to the rest of your chain, especially if you've pushed Drive hard. |
| **Mix** | 0 – 100 | 100 | % | Dry/wet blend of the whole "wet" chain (everything from Tight through Level) against the untouched input. At 100% (the default) Overture behaves like a real boost pedal - fully in the signal path. Lower values blend in some of the original, unprocessed signal; at exactly 0% the output is a sample-accurate (delay-compensated) passthrough of the input. |
| **Bypass** | Off / On | Off | – | Host-visible bypass. Unlike a plain "mute the plugin" bypass, Overture keeps its internal oversampler running while bypassed so the reported plugin latency (and your host's delay compensation) never changes - engaging/disengaging Bypass crossfades smoothly (over roughly a tenth of a second) rather than clicking or popping, and never introduces a timing glitch on other tracks. |
| **Oversampling** | 2x / 4x / 8x | 4x | – | Oversampling factor around the clipper (and Bite shelf). Higher factors give a cleaner (less aliased) clipper at the cost of more CPU. **Changing this parameter takes effect the next time your host re-initialises the plugin** (e.g. on transport stop/start, a sample-rate change, or reopening the project) - not instantly while audio is running. This is a deliberate real-time-safety choice: reconfiguring the oversampler requires a memory allocation, which must never happen on the audio thread. If you want to hear a change immediately, stop and restart playback (or reopen the plugin) after changing it. |

## Presets

Overture ships with nine factory presets (a certified **Default** plus eight use-case-driven starting points - see [`docs/presets.md`](presets.md) for the full list and intent behind each). The preset bar docked at the top of the editor lets you browse factory/user presets, save your own (`~/Library/Audio/Presets/Yves Vogl/Overture/` on macOS), import/export single presets or zip banks, and mark any preset (including your own) as the default that loads on a fresh instance.

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
- **Bypass, not Mix at 0%, for A/B comparisons during a mix.** Both null the wet chain, but Bypass is the one hosts treat as "native" bypass (automation lane, right-click bypass in most DAWs), and it's what keeps latency reporting stable if you're comparing across multiple instances.
- **Leave Oversampling at 4x unless you have a specific reason to change it.** 2x saves CPU at a small aliasing cost (mostly audible on very high Drive + Hard Clip); 8x is for tracking/committing a final take where you want the cleanest possible clipper at the cost of extra CPU load.
