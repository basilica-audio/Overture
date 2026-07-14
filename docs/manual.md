<p align="center"><img src="assets/icon.png" alt="Overture icon" width="120"/></p>

# Overture — user manual

*The 808 boost — tighten your low end before the gain hits.*

## What it is

Overture is a TS-808-style tight boost/overdrive for metal guitar. It is not a full amp-in-a-box; it is the small pedal you'd run on the floor **in front of** a high-gain amp, doing exactly the job a "modded 808" does in a real rig:

1. Strip low end out of the signal *before* it hits any clipping/distortion, so palm mutes and low-string chugs stay tight and articulate instead of turning into a low-frequency mush once the amp's own gain stage saturates them.
2. Add a controlled amount of its own drive/clipping character on top, voiced to push the front of an already-distorted amp rather than to be a distortion pedal in its own right.

## Where it sits in a symphonic-metal chain

Overture is a **pre-amp tightening/boost stage**, not a cab sim, not an EQ, not a compressor. A typical chain:

```
Guitar -> noise gate -> Overture (tight boost) -> amp sim / real amp front end -> cab sim -> reverb/mix bus
```

Run it ahead of whatever provides the "wall of gain" in your chain (a real tube amp's input, or another plugin doing high-gain amp simulation). Overture's own Drive/Voicing controls are deliberately modest by default (see [Tips](#tips)) - the point is to *shape what hits the gain stage*, not to be the gain stage itself. If you want Overture's clipper to be the main distortion source (e.g. for a boost-only rig with a clean amp), push Drive further and pick a more aggressive Voicing.

## Signal flow

```
Input -> Tight (HPF, 20-400 Hz) -> Drive (0-40 dB) -> [oversampled] Voicing clipper
                                                                |
      Output <-- Mix <-- Level (output trim) <-- Tone (LPF, 1-8 kHz) <--+
        ^
        |
   delay-compensated dry path (also used by Bypass)
```

The clipper runs inside an oversampled block (2x/4x/8x, selectable via **Oversampling**) so its harmonics don't alias back into the audible band. The dry path used by **Mix** (and by **Bypass**) is automatically delay-compensated against that oversampling latency, and the plugin reports its total latency to the host so playback stays sample-accurately aligned with every other track. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Tight** | 20 – 400 | 130 | Hz | High-pass filter placed *before* the clipper. Raising it strips more low end out of the signal that reaches the Drive/clipper stage, keeping palm mutes and low-string chugs tight instead of farting out once the amp's own gain stage saturates them. This is the core "808 mod" trick the plugin is built around. Lower it (toward 20 Hz) for a fuller, less tightened low end; raise it (toward 300-400 Hz) for maximum palm-mute articulation on drop-tuned guitars. |
| **Drive** | 0 – 40 | 8 | dB | Gain applied to the signal right before the clipper (selected by **Voicing**). At 0 dB the clipper barely engages; higher values push harder into the chosen nonlinearity. Kept modest by default because Overture is meant to *push* an already-driven amp, not replace it - see [Tips](#tips). |
| **Voicing** | Asymmetric / Soft Symmetric / Hard Clip | Asymmetric | – | Selects the clipper nonlinearity the oversampled Drive stage feeds into. **Asymmetric** is the original "808 boost" voicing: a single-ended, biased tanh curve (op-amp/diode-style), producing both odd and even harmonics for a slightly asymmetric, "tube-like" push. **Soft Symmetric** is a plain, unbiased tanh curve - smoother, more even-handed saturation with only odd harmonics, closer to a push-pull amp stage. **Hard Clip** is a straight clamp with no soft knee - the brightest and most aggressive of the three, closer to a fuzz/comparator-style clip; use it when you want Overture itself to be doing real distortion work rather than just tightening/boosting. Switching Voicing is a discrete change (like a stompbox toggle), not a smoothly-automatable control, so expect an audible step at the instant you switch, not a crossfade. |
| **Tone** | 1000 – 8000 | 6000 | Hz | Low-pass filter placed *after* the clipper, tames the fizz/harshness the clipper's harmonics add without touching the fundamental. It's a steep (4th-order, 24 dB/octave) filter, so it's an effective "de-fizz" control even close to its upper range. Left fairly bright (6 kHz) by default so your amp's own tone stack - not this pre-clip tightening stage - does the final top-end voicing. |
| **Level** | -24 – +24 | 0 | dB | Output trim, applied after Tone and before the dry/wet Mix. Use it to match Overture's output level to the rest of your chain, especially if you've pushed Drive hard. |
| **Mix** | 0 – 100 | 100 | % | Dry/wet blend of the whole "wet" chain (everything from Tight through Level) against the untouched input. At 100% (the default) Overture behaves like a real boost pedal - fully in the signal path. Lower values blend in some of the original, unprocessed signal; at exactly 0% the output is a sample-accurate (delay-compensated) passthrough of the input. |
| **Bypass** | Off / On | Off | – | Host-visible bypass. Unlike a plain "mute the plugin" bypass, Overture keeps its internal oversampler running while bypassed so the reported plugin latency (and your host's delay compensation) never changes - engaging/disengaging Bypass crossfades smoothly (over roughly a tenth of a second) rather than clicking or popping, and never introduces a timing glitch on other tracks. |
| **Oversampling** | 2x / 4x / 8x | 4x | – | Oversampling factor around the clipper. Higher factors give a cleaner (less aliased) clipper at the cost of more CPU. **Changing this parameter takes effect the next time your host re-initialises the plugin** (e.g. on transport stop/start, a sample-rate change, or reopening the project) - not instantly while audio is running. This is a deliberate real-time-safety choice: reconfiguring the oversampler requires a memory allocation, which must never happen on the audio thread. If you want to hear a change immediately, stop and restart playback (or reopen the plugin) after changing it. |

## Tips

- **Start with Tight, not Drive.** The whole point of the "808 boost in front of a high-gain amp" trick is the high-pass filter, not the clipper. Dial in Tight first (100–200 Hz is a good starting range for drop-tuned rhythm parts) with Drive low, and only add Drive once the low end feels controlled.
- **Keep Drive modest if you're pushing a real amp/amp-sim afterwards.** Overture's clipper is meant to nudge the front end of whatever gain stage comes next, not fight it. If everything starts sounding thin or brittle, back Drive off before reaching for Tone.
- **Use Voicing to match the character you're after**, not just to add more gain. Asymmetric (default) for a classic 808-in-front-of-a-Marshall push; Soft Symmetric for a smoother, more "amp-like" saturation that sits well under a high-gain amp sim; Hard Clip when you want Overture itself to be a genuinely distorted signal (e.g. driving a clean amp, or as a fuzz-adjacent lead boost).
- **Tone is a cleanup tool, not a voicing tool.** Because it's steep (24 dB/octave), small movements make an audible difference. Use it to tame fizz introduced by the clipper, not as your main tone-shaping control - that's what your amp/amp-sim's own EQ is for.
- **Mix below 100% is for parallel/blended tones**, e.g. blending a small amount of tightened/driven signal under a clean DI for a hybrid rhythm tone. For a normal "boost pedal in front of the amp" use case, leave Mix at 100%.
- **Bypass, not Mix at 0%, for A/B comparisons during a mix.** Both null the wet chain, but Bypass is the one hosts treat as "native" bypass (automation lane, right-click bypass in most DAWs), and it's what keeps latency reporting stable if you're comparing across multiple instances.
- **Leave Oversampling at 4x unless you have a specific reason to change it.** 2x saves CPU at a small aliasing cost (mostly audible on very high Drive + Hard Clip); 8x is for tracking/committing a final take where you want the cleanest possible clipper at the cost of extra CPU load.
