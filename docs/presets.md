# Factory presets

Nine factory presets ship with Overture v0.2.0, embedded via BinaryData from
`presets/factory/*.json` (M2 preset system, `.scaffold/specs/preset-system-m2.md`
- `src/presets/PresetManager.{h,cpp}`/`src/presets/PresetBar.{h,cpp}`, copied
verbatim from the Nave pilot per `docs/design-brief.md`). Eight of the nine
are `docs/design-brief.md`'s own "Factory Presets" table verbatim; the ninth
(**Default**) is this suite's standard explicit-default pattern. All are
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
| **Own Distortion** | Guitar | Overture as the main distortion source (clean amp/DI use case) rather than just a pusher - Hard Clip voicing at a real Drive level (22 dB). |
| **Fuzz-Adjacent Lead** | Guitar | Aggressive, brighter lead-boost character - Hard Clip at high Drive (30 dB) with a positive Bite Tilt brightening the output. |
| **Parallel Grit** | Guitar | Blends a driven signal under a clean DI (hybrid rhythm tone) at Mix 35% - the one preset where Mix < 100% is idiomatic, per the brief. |
| **De-Fizz Cleanup** | Guitar | Demonstrates Bite Tilt's new darkening range (-60%) for cleaning up a fizzy high-gain amp sim downstream - the capability v0.1's cut-only Tone also had, now reachable through the bidirectional tilt. |

See `docs/manual.md` for the full parameter reference and `docs/design-brief.md`
for the sourcing behind each preset's settings.
