# Overture — Design Brief v2 (binding; supersedes v1's implicit spec)

Guitar tight boost / pre-distortion sharpener for modern-metal rhythm tone, built on the
publicly-documented "tube-screamer-in-front-of-a-high-gain-amp" technique — packaged as a
single deterministic, oversampled, tested plugin. Research-driven rewrite: every default
below is sourced (see `docs/research-notes.md`) or explicitly reasoned where no source exists.
**No brand or person names in parameters, UI or marketing copy** — generic descriptors only
("Tight", "Bite", "Push"); the manual/research notes may cite public sources (Tube Screamer
circuit analyses, Horizon Devices Precision Drive, named engineers' documented workflows)
freely, since those are the honestly-disclosed reference class, not implied endorsement.

## Why v1 falls short (the three core corrections)

1. **The tightening mechanism is the wrong shape.** v1's Tight control is a static,
   level-independent pre-clip high-pass filter — bass is removed uniformly, regardless of how
   hard the clipper is being driven. The reference class instead achieves "tightness" via
   **frequency-dependent gain inside the clipping stage itself**: bass is clipped *less* than
   treble, dynamically, more pronounced the harder the stage is driven ("bass notes are
   clipped least, so the distortion is frequency selective" — see research notes §3). A
   separate pre-clip filter is a reasonable engineering approximation of the *result* but
   misses the *mechanism* — it can't reproduce the level-dependent character where the
   tightening effect itself intensifies as you push Drive harder.
2. **The tone stage can only darken.** The reference class's post-clip tone control is a
   **bidirectional tilt** anchored on a fixed passive roll-off (cut *or* boost relative to a
   ~3.2 kHz corner). v1's Tone is a cut-only 4th-order low-pass — there is no way to brighten
   the signal at all, a real functional gap, not a cosmetic one.
3. **The canonical use case is under-represented in the defaults.** The best-documented modern
   workflow this plugin is named after is "clipper drive near zero, level/push high, the amp
   does the actual distorting" (Misha Mansoor's documented approach; Horizon Devices' own
   purpose-built pedal explicitly instructs "start with drive near zero," then "1–2 out of
   10"). v1 ships with no presets at all and a single 8 dB Drive default that, while
   defensible as a middle ground, doesn't make the purest, best-sourced version of the
   technique discoverable without documentation-reading.

## Topology (fixed)

```
in → [Tight: pre-clip HPF, unchanged role] → [Drive] → [Nx oversampled]
       ↳ Voicing clipper with FREQUENCY-DEPENDENT GAIN (new: feedback-style tilt
         inside the drive stage, not just a filter ahead of it) + DRIVE-DEPENDENT
         KNEE SOFTENING (new) + variable Asymmetry amount (new, was fixed a=0.2)
     → [Bite: post-clip TILT, cut/boost around a fixed corner] (was cut-only Tone)
     → [Level] → [Mix] → out
Bypass unchanged (reuses Mix path, crossfaded, latency-stable).
Oversampling unchanged (2x/4x/8x, next-prepare semantics, real-time-safety rationale intact).
```

- Everything from Tight through Level remains the "wet" path owned by `OvertureEngine`,
  dry path delay-compensated via `DryWetMixer`, latency = oversampler latency only — none of
  this v1 architecture is being replaced. v2 changes what happens *inside* Drive→Clipper→Tone,
  not the surrounding real-time-safety/oversampling/latency contract.
- The frequency-dependent-gain and knee-softening behaviors must stay inside the oversampled
  block (same reasoning as v1: new high-frequency harmonic content from a more level-dependent
  nonlinearity needs anti-aliasing exactly as much as the existing clippers do).

## Module specifications (authentic behaviors, generically named)

### Tight — pre-clip high-pass (kept, re-anchored to sourced numbers)
- Range and role unchanged from v1: **20–400 Hz, pre-clip, log-taper knob.** Research
  confirms this range comfortably brackets both the documented workflow sweet spot
  (**80–120 Hz**, "cutting everything below 80-120Hz... crucial for fast palm-muted riffs on
  low-tuned guitars") and the classic mod-cap ladder for this exact class of circuit
  (**34 / 72 / 154 / 339 / 720 Hz** — the discrete corner frequencies guitarists get by
  swapping the input coupling cap on the reference circuit).
- **Default changes from 130 Hz → 100 Hz.** 130 Hz was a reasonable guess; 100 Hz sits
  centrally inside the *specifically documented* 80–120 Hz workflow sweet spot rather than
  just above it, and is the more research-supported "out of the box, sounds like the
  technique" starting point. (Reasoned, not a hard number from a single source — the sourced
  range is 80–120 Hz, 100 Hz is its midpoint.)
- No structural change otherwise: 2nd-order Butterworth HPF, unchanged Q, unchanged
  smoothing. This module was already the least generic part of v1.

### Drive → Voicing clipper — the core rebuild
- **`drive_gain`** (was `Drive`): 0–40 dB, range unchanged (research confirms the reference
  circuit's own gain-stage range, ≈21.5–41.5 dB stock, comfortably sits inside 0–40 dB).
  **Default changes from 8 dB → 3 dB.** Sourced: the best-documented canonical workflow is
  "near zero" drive with the *level* doing the pushing (Mansoor: drive "at or near zero";
  Horizon Devices' own manual: "start with this near zero... slowly turn the drive knob up to
  around 1-2" out of 10, i.e. roughly the bottom 10-20% of travel). 3 dB is a small,
  non-zero push that keeps the clipper audibly alive (this plugin's whole job) while sitting
  in the researched "mostly clean push" region rather than v1's more OD-forward 8 dB. Users
  wanting the plugin to be its own distortion source (also a documented, legitimate use —
  v1's own manual already covers it) turn Drive up further; that use case doesn't need a
  reference-anchored default, it needs headroom, which the unchanged 0–40 dB range provides.
- **`bite_amount`** (NEW, 0–100%, default 65%): the frequency-dependent-gain behavior. At 0%,
  the clipper's gain is flat with frequency (equivalent to v1's plain tanh/clamp — full
  backward-compatible behavior). Above 0%, a first-order low-shelf **inside the drive-into-
  clipper gain path** (not before it — this is the architectural fix from §1) progressively
  reduces the *drive* fed to the clipper below a shelf corner, scaled by `bite_amount`, so bass
  is clipped less than treble — reproducing the "frequency-selective distortion" mechanism
  rather than approximating its output with a separate filter. Shelf corner is a fixed
  ≈700 Hz (anchored to the sourced ≈720 Hz reference-circuit feedback corner — not exposed as
  a separate control in v2; a future version could expose it, out of scope here). Default
  65% (not 100%) because full authenticity intensity at all Drive levels risks sounding
  over-scooped on some sources; 65% is a reasoned starting compromise, not sourced to a
  specific number — flagged as such.
- **`knee_soften`** (NEW, 0–100%, default 40%): drive-dependent knee softening. Blends each
  voicing's fixed-shape transfer function toward a softer-kneed variant as a function of both
  this control AND the instantaneous drive level feeding the clipper (louder into the clipper
  → softer effective knee, mirroring the reference circuit's own 51 pF-cap behavior:
  "softens the corners of the clipped waveform... most noticeable when the drive control is
  maxed out"). At 0%, all three voicings behave exactly as in v1 (bit-identical transfer
  functions) — this keeps v1 presets/sessions sonically reproducible at `knee_soften = 0`.
  Applies to all three voicings including Hard Clip (which in v1 has *zero* knee ever — real
  diode hard-clippers never do, per research). Default 40% is reasoned (moderate, audible but
  not smoothing away Hard Clip's identity), not sourced to a specific number.
- **`asymmetry_amount`** (was a fixed internal constant `a = 0.2`, now exposed 0–100% mapping
  to bias `a` in **0.0–0.5**, default 40% → `a ≈ 0.2`, preserving v1's exact default sound at
  the new default): only active for the Asymmetric voicing (unchanged: Soft Symmetric and
  Hard Clip ignore it, as in v1). Sourced range end (`a` up to ~0.5) is reasoned from the
  documented real-world spectrum of asymmetric mods (from fully symmetric — remove the bias —
  to fully asymmetric — remove one diode leg entirely) rather than a single measured number;
  flagged as reasoned-not-measured.
- Voicing enum (`Asymmetric` / `Soft Symmetric` / `Hard Clip`) is **unchanged and its
  persisted indices are frozen exactly as in v1** — this is a hard backward-compatibility
  constraint carried forward from v1's own contract, not something v2 is allowed to break.

### Bite — post-clip tilt (replaces Tone, cut-only → bidirectional)
- **Structural change:** where v1's Tone was purely a low-pass (1–8 kHz, cut-only,
  4th-order), v2's `bite_tilt` (−100%…+100%, default 0%, i.e. flat) is a **shelf tilt**
  anchored at a fixed ≈3 kHz corner (reasoned proxy for the sourced ≈3.2 kHz reference-circuit
  tone-control corner — rounded for a clean parameter feel, not claimed as a precise circuit
  citation). Negative values darken (low-pass-shelf character, subsuming v1's entire cut-only
  range so no v1 use case is lost), positive values brighten (shelf boost above the corner) —
  the capability v1 entirely lacked.
- Implementation: two complementary shelf filters (or a single tilt-EQ topology) replacing the
  two cascaded low-pass IIR sections; steepness is intentionally gentler than v1's 4th-order
  low-pass (a tilt control is meant for shaping, not brick-wall fizz removal) — target roughly
  first-order/6 dB-oct shelf slopes on each side, consistent with the reference circuit's own
  ±6 dB/octave tilt behavior (sourced).
- **Migration:** old `tone` parameter (1–8 kHz cut-only) has no exact equivalent; state
  import maps old `tone` values to an equivalent negative `bite_tilt` position (fully cut at
  1 kHz → maximally negative tilt; fully open at 8 kHz → flat/0%) so old sessions/presets land
  close to their original character rather than silently resetting to flat. This is a lossy,
  best-effort migration, not a mathematically exact one — documented as such (tolerant import
  per Versioning below).

### Level, Mix, Bypass, Oversampling — unchanged
- No sourced reason to change any of these from v1. Level ±24 dB, Mix 0–100% (default 100%),
  Bypass (crossfaded, latency-stable), Oversampling 2x/4x/8x (default 4x, next-prepare
  semantics) all carry forward exactly as specified in v1's architecture doc.

## Factory Presets (for the M2 preset system — proposed, not yet implemented)

Generic descriptors only, no names/brands. Settings are starting points, not exact renders.

| Preset | Intent | Rough settings |
|---|---|---|
| **Clean Push** | The purest documented version of the technique: near-zero clipper drive, amp does all the distorting. | Tight 100 Hz · Drive 1 dB · Voicing Asymmetric · Bite 65% · Knee Soften 40% · Asymmetry 40% · Bite Tilt 0% · Mix 100% |
| **Classic Boost** | v1-compatible default character, slightly more clipper presence than Clean Push. | Tight 100 Hz · Drive 3 dB · Voicing Asymmetric · Bite 65% · Knee Soften 40% · Asymmetry 40% · Bite Tilt 0% · Mix 100% (the shipped default) |
| **Drop-Tune Tight** | Heavier low-cut for drop-tuned, fast palm-muted rhythm parts. | Tight 220 Hz · Drive 4 dB · Voicing Asymmetric · Bite 80% · Knee Soften 30% · Asymmetry 40% · Bite Tilt −10% · Mix 100% |
| **Smooth Push** | Smoother, more amp-like saturation under a high-gain amp sim, less biased/tube-like edge. | Tight 90 Hz · Drive 5 dB · Voicing Soft Symmetric · Bite 55% · Knee Soften 55% · Bite Tilt 0% · Mix 100% |
| **Own Distortion** | Overture as the main distortion source (clean amp / DI use case), not just a pusher. | Tight 120 Hz · Drive 22 dB · Voicing Hard Clip · Bite 40% · Knee Soften 60% · Bite Tilt +10% · Mix 100% |
| **Fuzz-Adjacent Lead** | Aggressive, brighter lead-boost character. | Tight 150 Hz · Drive 30 dB · Voicing Hard Clip · Bite 25% · Knee Soften 70% · Bite Tilt +25% · Level +3 dB · Mix 100% |
| **Parallel Grit** | Blend a driven signal under a clean DI (hybrid rhythm tone), the one place Mix < 100% is idiomatic. | Tight 100 Hz · Drive 12 dB · Voicing Asymmetric · Bite 65% · Knee Soften 40% · Bite Tilt 0% · Mix 35% |
| **De-Fizz Cleanup** | Demonstrates Bite Tilt's new darkening range for a fizzy high-gain amp sim downstream. | Tight 100 Hz · Drive 6 dB · Voicing Soft Symmetric · Bite 65% · Knee Soften 45% · Bite Tilt −60% · Mix 100% |

## Guarantees & tests (Catch2; keep all still-valid v1 cases, extend for the new controls)

1. **Backward-compatible null cases at the "off" settings:** `bite_amount = 0` and
   `knee_soften = 0` reproduce v1's exact transfer function bit-for-bit (within float
   tolerance) for all three voicings, at every Drive level tested — this is the core
   regression guarantee that v2 doesn't silently change v1's sound for anyone who dials the
   new controls back to zero.
2. **Frequency-dependent gain proof (Bite):** feed a low-frequency sine (e.g. 80 Hz) and a
   higher-frequency sine (e.g. 2 kHz) at matched input level through the clipper at a fixed
   Drive with `bite_amount` swept 0% → 100%; assert the measured harmonic/clip-onset level
   difference between the two frequencies *increases monotonically* with `bite_amount`
   (spectral/THD-based proof that bass is progressively clipped less than treble, not just
   filtered out beforehand).
3. **Drive-dependent knee softening proof:** for a fixed `knee_soften > 0`, measure the
   transfer-function knee sharpness (e.g. second-derivative magnitude near the clip onset, or
   THD at a fixed input level) at low vs. high Drive; assert the knee is measurably softer at
   high Drive than at low Drive. At `knee_soften = 0`, assert knee sharpness is
   Drive-invariant (matches v1 behavior exactly).
4. **Asymmetry sweep:** `asymmetry_amount` 0% → 100% maps monotonically to bias `a` in
   0.0–0.5; assert positive/negative half-cycle peak levels diverge monotonically with the
   control (0% = symmetric peaks, 100% = maximally asymmetric), and that `asymmetry_amount`
   at the default 40% reproduces v1's fixed `a = 0.2` behavior exactly (see guarantee 1).
5. **Bite Tilt bidirectionality:** assert `bite_tilt` negative values reduce high-frequency
   content monotonically (subsuming v1's Tone cut range — a fully-negative setting must be at
   least as dark as v1's old fully-closed Tone), and positive values measurably boost content
   above the shelf corner relative to flat (0%) — the capability gap being closed. Flat (0%)
   must be a true no-op (≤ small tolerance) relative to a reference unfiltered signal.
6. **State migration tolerance:** old (v1) saved state with only `tone` (no `bite_tilt`,
   `bite_amount`, `knee_soften`, `asymmetry_amount`) loads without crashing or throwing;
   `tone`'s value is mapped into an equivalent `bite_tilt` value per the documented (lossy)
   migration rule; all new parameters fall back to their v2 defaults, not zero/garbage.
   Conversely, v2 state with unknown-to-v1 IDs must not crash a hypothetical older build
   (forward-tolerant round-trip test using the same unknown-ID-ignored pattern as the rest of
   the suite).
7. **NaN/Inf robustness on all new controls:** sweep `bite_amount`, `knee_soften`,
   `asymmetry_amount`, `bite_tilt` to their extremes combined with extreme Drive/Tight/Level
   and confirm no NaN/Inf propagates through the engine (carries forward v1's existing
   robustness-test pattern to the new parameters).
8. **Real-time-safety carry-forward:** no new allocation on the audio thread from any new
   control (all are per-block-recomputed coefficients/smoothed scalars, same pattern as
   existing Tight/Tone); oversampling/latency/bypass/dry-wet-mixer contracts from v1
   (`docs/architecture.md`) remain unmodified and their existing tests remain green unchanged.
9. **Preset round-trip:** every factory preset in the table above loads, all parameter values
   land within tolerance of their specified settings, and produces no NaN/Inf/silence on a
   standard test signal.

## Honesty & framing

- `docs/research-notes.md` ships the sourced findings (quotes + URLs) — the voicing/behavior
  changes in this brief are **research-derived from published circuit analyses, a purpose-
  built commercial pedal's own documentation, and publicly reported artist workflows — not
  measured against physical reference hardware or original-manufacturer schematics/datasheets
  by this project.** Say so in the manual.
- Two of the cited numeric anchors (the ≈720 Hz feedback-loop corner and the ≈3.2 kHz tone-tilt
  corner) come from secondary technical-analysis sources that converge with each other, not
  from a primary-source page reached directly in this pass (see research notes §6 for the
  specific access gap) — treat as well-corroborated but not independently re-verified.
- Several new defaults (`bite_amount` 65%, `knee_soften` 40%, `asymmetry_amount` mapping
  ceiling of `a=0.5`, `bite_tilt` shelf corner ≈3 kHz) are **reasoned engineering choices
  anchored to the sourced qualitative behavior, not numbers taken directly from a source** —
  each is called out individually above; do not represent them as measured hardware values.
- Manual notes that "Tube Screamer," "Horizon Devices Precision Drive," "Misha Mansoor," and
  "Ola Englund" are cited as documented public sources for the *technique*, without implying
  endorsement, sponsorship, or affiliation by any person or brand — consistent with the
  existing README's non-affiliation language.
- Out of scope for v2 (explicitly): exposing the Bite shelf corner or Bite Tilt corner as user
  controls, a stepped/discrete alternate mode for Tight (mirroring the Precision Drive's
  switch-style UX) — tracked as an M3+ candidate issue, not a v2 requirement. Custom GUI
  remains M3 as in v1's roadmap; this brief is DSP/parameter-layer only.

## Versioning

Ships as **v0.2.0** (breaking parameter changes are acceptable pre-1.0, per suite convention;
the Voicing enum's persisted indices remain a hard-frozen exception carried forward from v1).
State migration = tolerant import (old `tone`-only state loads and is lossily mapped to
`bite_tilt`, per guarantee 6; unknown IDs in either direction are ignored, not fatal).
CHANGELOG documents the Tone→Bite structural change and the new Bite/Knee Soften/Asymmetry
controls prominently as the headline v0.2.0 change.
