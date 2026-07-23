# M3 GUI - component notes

Overture's M3 photoreal editor replicates `basilica-audio/silentium`'s M3
pilot pattern: the same suite-reusable component family under `src/gui/`,
ported essentially verbatim, plus Overture's own faceplate art and layout
table. This document is the "why" behind the code comments, adapted from
Silentium's own `docs/gui-components.md` for whoever ports this pattern to
the next plugin after Overture.

## Components (`src/gui/`)

| Component | Base class | Backs onto |
|---|---|---|
| `FilmstripKnob` | `juce::Slider` (RotaryVerticalDrag) | `knob-brass-v1` 128-frame filmstrip |
| `FilmstripToggle` | `juce::Button` | `toggle-brass-v1` 4-frame filmstrip |
| `BasilicaLookAndFeel` | `juce::LookAndFeel_V4` | interim JUCE-drawn label styling |
| `ImageDensity.h` | (free functions) | @1x/@2x tier selection shared by both |

All four are ported verbatim from Silentium (byte-identical except this
file's own copyright/attribution-free header comments) - they are plugin-
agnostic by construction: they take asset `juce::Image`s and generic config
(frame counts, titles) through their constructors, not any plugin's
parameter IDs. `PluginEditor.cpp` is the only file that knows about
Overture's actual 8-knob/1-toggle/2-choice parameter set.

**`AnalogMeter` was deliberately NOT ported.** Overture's layout manifest
(`.scaffold/gui-assets/faceplate-overture-v1/layout-manifest.json`) declares
four bays (input/clipper/output/utility) and no meter bay - Overture is a
boost/overdrive, not a gate/compressor with a gain-reduction or level reading
worth showing on the faceplate. A future Overture GUI pass that wants
metering (e.g. an output-level VU) would need a new faceplate render with a
meter bay added to the manifest first, plus the processor-side metering
atomics `AnalogMeter::setTargetDb()` expects - neither exists yet.

## Choice controls: no filmstrip art

Overture has two discrete parameters (`voicing`, `oversampling`) that
Silentium's parameter set never needed a pattern for. This asset wave ships
no combo-box/segmented-switch filmstrip art, so both remain stock
`juce::ComboBox` instances (`PluginEditor.cpp`'s `configureChoice()`),
explicitly recoloured (background/text/outline/arrow, via
`BasilicaLookAndFeel::getLabelTextColour()`/`getLabelBackingChipColour()`) to
match the gold-on-gunmetal palette rather than left at JUCE's default grey
scheme. A future asset wave that wants a fully photoreal choice control would
need its own filmstrip family (e.g. a 3-position rotary switch render) - out
of scope for this pass, which prioritised faithfully replicating the
Silentium pattern within the manifest's actual asset inventory.

## Layout table

`PluginEditor.cpp`'s anonymous namespace holds one table each of
`KnobLayoutEntry`, `ToggleLayoutEntry`, and `ChoiceLayoutEntry` (parameter ID,
label text, bay, column), expressed in the faceplate's base @1x (900x600)
pixel coordinates. Unlike Silentium's single 5x2 knob grid over one control
bay, Overture's manifest groups its 11 controls into four independently-sized
bays (input: 2, clipper: 4, output: 3, utility: 2); `PluginEditorLayout.h`'s
`inputBay1x`/`clipperBay1x`/`outputBay1x`/`utilityBay1x` rects are copied
verbatim (centre+size converted to top-left) from
`faceplate-overture-v1/layout-manifest.json`, and `resized()` lays each bay
out as its own single row of equal-width cells. The same table positions
both the control AND its `juce::Label` caption, so a later pass that bakes
real per-control text into the faceplate art only needs to hide/remove the
`juce::Label` instances, not recompute a layout.

The header bay + brand-icon roundel geometry (`headerBay1x`,
`roundelCentre1x`, `roundelRadius1x`) is derived analytically from
`.scaffold/gui-assets/render_faceplate.py`'s own auto-derived header/roundel
proportions (the script computes these from canvas size alone, the same way
for every plugin using the generalized faceplate generator - see that
script's `build_faceplate()` and `PluginEditorLayout.h`'s inline derivation
comments) - unlike Silentium's hand-built pilot plate, Overture's faceplate
went through this generalized generator, so its header numbers are computed
rather than eyeballed against a preview render.

## Known limitations / open ends

- **Choice controls are unstyled beyond flat recolouring** - no hover/press
  filmstrip states, no engraved-metal look. See "Choice controls" above.
- **The preset bar (`src/presets/PresetBar.h`) is not re-skinned** with
  photoreal assets in this pass, matching Silentium's own precedent - it
  lives in a plain strip above the faceplate art using stock
  `juce::TextButton`s (M2 shipped these; `BasilicaLookAndFeel` styles its
  `juce::Label`s suite-wide, giving it *some* visual consistency for free).
- **Stepped window scaling (100/150/200%) always redraws from the same
  source images** (`ImageDensity.h` just picks @1x vs @2x by target pixel
  size); there is no @3x/@4x tier, so 200% on a very high-density display
  still only has @2x source resolution to work with - same acceptable
  limitation as Silentium's pilot.
