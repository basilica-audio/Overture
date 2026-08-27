# Factory presets

Eleven factory presets ship with Overture, embedded via BinaryData from
`presets/factory/*.json` (M2 preset system, `.scaffold/specs/preset-system-m2.md`
- `src/presets/PresetManager.{h,cpp}`/`src/presets/PresetBar.{h,cpp}`, copied
verbatim from the Nave pilot per `docs/design-brief.md`). Eight of the nine
are `docs/design-brief.md`'s own "Factory Presets" table verbatim; the ninth
(**Default**) is this suite's standard explicit-default pattern, and the last
two were added in v0.3.0 to exercise the new gate and the new circuit-solved
voicing. **The nine v0.2.0 files are byte-frozen** (asserted by hash in
`tests/PresetManagerTests.cpp`): they carry none of the v0.3.0 parameters, and
the tolerant JSON importer therefore leaves those at their neutral defaults. All are
sourced starting points, not exact renders - see the brief's own "Honesty &
framing" section for what these numbers are and aren't calibrated against
(research/manual/forum-derived, not measured against physical hardware).

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The certified out-of-the-box state - identical settings to Classic Boost (this plugin's shipped `ParameterLayout` defaults), exposed as an explicit, selectable preset so there's always a one-click way back to it. |
| **Clean Push** | Guitar | The purest documented version of the "tight boost in front of a high-gain amp" technique: near-zero clipper Drive (1 dB), Level/the amp doing all the actual distorting. |
| **Classic Boost** | Guitar | v0.1-compatible default character, slightly more clipper presence than Clean Push (Drive 3 dB) - this is the plugin's shipped default. |
| **Drop-Tune Tight** | Guitar | Heavier low-cut (Tight 220 Hz) and more Bite (80%) for drop-tuned, fast palm-muted rhythm parts. |
| **Smooth Push** | Guitar | Smoother, more amp-like saturation (Soft Symmetric voicing, higher Knee Soften) under a high-gain amp sim, less biased/tube-like edge than the Asymmetric voicing. |
| **Own Distortion** | Guitar | Overture as the main distortion source (clean amp/DI use case) rather than just a pusher - Hard Clip voicing at a real Drive level (22 dB), with Level at -2.68 dB to keep that on the right side of full scale. |
| **Fuzz-Adjacent Lead** | Guitar | Aggressive, brighter lead-boost character - Hard Clip at high Drive (30 dB) with a positive Bite Tilt brightening the output. Level ships at -4.77 dB: Hard Clip at 30 dB of Drive sits on its own ceiling, and the tilt boost is on top of that. |
| **Parallel Grit** | Guitar | Blends a driven signal under a clean DI (hybrid rhythm tone) at Mix 35% - the one preset where Mix < 100% is idiomatic, per the brief. |
| **De-Fizz Cleanup** | Guitar | Demonstrates Bite Tilt's new darkening range (-60%) for cleaning up a fizzy high-gain amp sim downstream - the capability v0.1's cut-only Tone also had, now reachable through the bidirectional tilt. |
| **Tight Rhythm Gate** *(new in v0.3.0)* | Guitar | The plugin's stated use case, complete: the built-in gate on at -45 dB with the program-dependent Auto release, a heavier low-cut (Tight 180 Hz) and plenty of Bite for palm-muted low-tuned rhythm work, plus the signal-dependent Knee Response and Enhanced clip quality. Silence between chugs is the point. |
| **Circuit Drive** *(new in v0.3.0)* | Guitar | The circuit-solved **Feedback** voicing at Drive 12 dB - a touch-sensitive, programme-level clipper rather than a clean boost. Level ships at -9 dB because the circuit carries 21 dB of in-band gain of its own; treat this as the vetted starting point for that voicing rather than a finished tone. Bite and Knee Soften have no effect here by design. |

See `docs/manual.md` for the full parameter reference and `docs/design-brief.md`
for the sourcing behind each preset's settings.

## Output headroom

Every factory preset is gated by `tests/PresetHeadroomTests.cpp`: rendered through the real
processor at 48 kHz against the suite reference programme (four plucked notes spanning
E1 41.203 Hz to A5 880.000 Hz, twelve harmonics each, peak-normalised to -12 dBFS), a factory
preset's output peak must stay below 0 dBFS. The shipped `level` values target -0.3 dBFS on
that fixture, so there is 0.3 dB between "a voicing tweak moved the peak" and "the gate goes
red", and a preset added later that clips cannot ship.

Only two presets ever needed a trim, and only after the underlying defect was fixed: *Fuzz-
Adjacent Lead* and *Own Distortion* were the two presets with a **positive** Bite Tilt, and
until v0.3.1 a positive Bite Tilt applied roughly one dB of broadband boost per percent of
knob travel (see `docs/design-brief.md`'s Bite Tilt section). Fuzz-Adjacent Lead rendered the
reference at **+27.10 dBFS**, of which +23.27 dB was that shelf alone. Capping the boost
direction at +12 dB brought it to +7.46 dBFS; the derived `level` trims took the two of them
the rest of the way. No other preset's `level` was touched - presets already below the target
are deliberately not raised, since that would be level-matching the set rather than removing
an overshoot.
