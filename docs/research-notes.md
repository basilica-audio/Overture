# Overture — Research Notes (sourced findings for design-brief v2)

Status: this file was found EMPTY at resume (previous researcher was killed before writing
anything). This version was written from scratch in this pass — full research, not a
continuation. Reference class: TS808/TS9-style tube-screamer-derived "tight boost in front
of a high-gain amp" — the widely-documented modern-metal technique Overture's README already
name-checks as its inspiration ("the 808 boost").

## 1. What v1 actually is (read from the repo, not research — baseline for the gap analysis)

Source: `/Users/yves/Development/Audio/overture/{README.md,docs/architecture.md,docs/manual.md,src/**}`

- Signal flow: `Tight (2nd-order Butterworth HPF, 20–400 Hz, default 130 Hz, pre-clip)` →
  `Drive (0–40 dB gain, default 8 dB)` → `[oversampled 2x/4x/8x] Voicing clipper` (Asymmetric
  `tanh(x+a)-tanh(a)` with FIXED `a=0.2`, default / Soft Symmetric `tanh(x)` / Hard Clip
  `clamp(x,-1,1)`) → `Tone (4th-order Butterworth LPF, 1–8 kHz, default 6 kHz, post-clip,
  cut-only)` → `Level (±24 dB output trim)` → `Mix (0–100%, default 100%)` → `Bypass`.
- Tight and Tone are both simple, static, linear pre/post filters — level- and
  frequency-independent inside the clip itself. The clipper is a single fixed-shape
  nonlinearity per voicing; asymmetry amount is not a control.
- No factory presets exist yet (M2 = "Presets & state recall", explicitly planned/not done).
- `docs/architecture.md`/`CLAUDE.md` explicitly say Overture is engineered as a clean, modern
  DSP chain (oversampled, tested, real-time-safe) rather than a hardware emulation — v1 never
  claimed circuit-accuracy. The gap is about *behavioral* authenticity (why the reference
  class sounds "tight" the way it does), not about being a literal SPICE model.

## 2. Reference class identified

1. **Ibanez Tube Screamer (TS808 / TS9 circuit topology)** — the "808" Overture's own name and
   README ("the 808 boost") directly reference. Primary sources: circuit-analysis sites
   (ElectroSmash — DNS-unreachable during this session, cited via secondary summaries only;
   GeoFex "The Technology of the Tube Screamer"; stompboxelectronics.com TS-9 analysis).
2. **Horizon Devices Precision Drive** — the purpose-built *modern-metal* descendant of the
   "TS-in-front-of-a-high-gain-amp" trick, designed by Misha Mansoor (Periphery) specifically
   to replace a real Tube Screamer for this job. Closest existing product-category analog to
   what Overture is trying to be (a dedicated tight-boost pedal, not a generic overdrive).
   Source: horizondevices.com official user guide.
3. **The "808-mod-in-front-of-the-amp" workflow/lore** as documented by working metal
   guitarists/engineers (Misha Mansoor, Ola Englund) and metal-production education sites
   (Nail The Mix, Fractal Audio forum, Sweetwater InSync) — this is the *use-case* reference
   class, not a single product.

## 3. How the Tube Screamer actually achieves "tightness" — and why it's architecturally
   different from v1's static pre-clip HPF

- The TS clipping stage is an op-amp with soft-clipping diodes **in its feedback loop**, not
  a passive clipper after a fixed-gain stage. Diodes: silicon signal diodes, turn-on/threshold
  around 0.5–0.7 V ("silicon signal diodes with a turn-on voltage of about 0.5 to 0.6v" —
  GeoFex; "roughly 0.6V to 0.7V (for silicon diodes)... practical non-linearity onset
  ~200–250 mV in simulation" — stompboxelectronics.com). Below threshold the diodes are
  effectively open and the stage is close to linear/unity-ish; above threshold, "the gain of
  the opamp stage changes, going down to just over 1" (GeoFex) — i.e. **gain compresses
  hard once the diodes conduct**, not a fixed static nonlinearity shape independent of level.
- **The frequency-selective part — this is the core architectural difference from v1's Tight
  knob:** the feedback network itself contains a high-pass branch (stock values: 4.7 kΩ +
  0.047 µF → **≈720 Hz corner**, per GeoFex/ElectroSmash summaries and stompboxelectronics.com
  independently converging on ~720 Hz). Because this filter sits *inside the clipping
  feedback loop*, not ahead of it, "harmonics above 720 Hz get the full gain of the distortion
  stage, and everything below it gets progressively less gain and distortion... bass notes
  are clipped least, so the distortion is frequency selective" (GeoFex, via search summary).
  In other words: **the TS doesn't remove bass before clipping — it clips bass LESS than
  treble, dynamically, as part of the same gain stage that does the distortion.** This is a
  fundamentally different mechanism from Overture v1's Tight control, which is a linear
  pre-clip filter that removes bass uniformly regardless of level or how hard the clipper is
  being driven.
  - Confirms Overture's underlying intuition (strip/reduce bass before the higher frequencies
    saturate) is directionally right — but the *implementation* the reference class actually
    uses is frequency-dependent clipping gain, not (only) a separate filter stage.
- Mod lore confirms the 720 Hz corner is exactly the "tightness" knob guitarists already mod
  by ear: changing the input-side coupling cap changes this corner —
  **0.047 µF → 720 Hz (stock) / 0.1 µF → 339 Hz / 0.22 µF → 154 Hz / 0.47 µF → 72 Hz /
  1.0 µF → 34 Hz** (freestompboxes.org mod-value table, cross-referenced via search). This is
  a real, sourced, discrete "sweet-spot ladder" for a tightening HPF corner, spanning almost
  exactly Overture's existing 20–400 Hz Tight range, with the stock/reference default sitting
  at 720 Hz (i.e. near the *top* of a metal-tightening range, not the middle) and the common
  metal-mod choices clustering around 150–350 Hz.
- Post-clip, the TS has a **passive 1 kΩ/0.22 µF low-pass at 723.4 Hz (−6 dB/oct)** feeding an
  **active Tone control that is a boost/cut TILT around a ~3.2 kHz corner** — "at the (+) side
  ... another −6 dB/oct high-frequency roll-off... at the (−) side ... +6 dB/octave above
  3.2 kHz [which] actually just levels off the −6 dB/octave induced by the [passive] network
  ... so the treble is just not being cut any more" (search-summarized from schematic-analysis
  source). **This is a bidirectional darker/brighter tilt control anchored on a fixed passive
  roll-off, not a plain low-pass.** Overture's Tone (4th-order Butterworth LPF, cut-only,
  1–8 kHz) can only ever darken the signal — it has no brightening/tilt behavior at all. This
  is a real functional gap versus the reference class, not just a "generic naming" issue.
- The small 51 pF cap across the clipping diodes "softens the corners of the clipped
  waveform... most noticeable when the drive control is maxed out, softening the distortion
  most when the gain and distortion is highest" (GeoFex) — i.e. the knee itself gets *softer*
  as drive increases, a level-dependent knee-softening effect. v1's clippers all have a fixed
  knee shape at every drive level (tanh's knee doesn't change shape with input level beyond
  what tanh's math already does, and Hard Clip has literally zero knee at any drive level).
- Gain range: TS drive pot spans roughly **11.85×–118× (≈21.5–41.5 dB)** of clean gain into
  the clipping stage (stompboxelectronics.com, derived from the feedback-network gain
  formula) — close to, and validating, Overture's existing 0–40 dB Drive range.

## 4. The modern-metal "808-in-front-of-the-amp" workflow — how it's actually used

- **Canonical modern workflow is "drive near zero, level/volume high," not "drive up for
  distortion."** Misha Mansoor's (Periphery) approach: "keep the pedal's drive control at or
  near zero while turning the volume (or level) up high, which pushes the front end of the
  amplifier harder, tightening the low-end frequencies and adding percussive bite and
  articulation to the strings" (search-summarized from Sweetwater/Reverb coverage of Mansoor's
  rig). Ola Englund, independently: "there's always going to be this part where you need to
  make the amp sound more metal or modern. Usually you use a Tube Screamer... to boost your
  amp" (search-summarized quote).
  - **Gap vs v1:** Overture's Drive defaults to 8 dB (a real, non-trivial push into the
    clipper) with Mix at 100%/fully wet — closer to "boost pedal with some of its own OD
    character" than the purest documented "clean-ish push, amp does all the distorting"
    variant of the technique. 8 dB is defensible as a middle ground (Overture explicitly also
    supports being the main distortion source via Hard Clip at high Drive, per its own
    manual's Tips section) but the *canonical* reference-class default leans toward less
    clipper-generated distortion than 8 dB implies, with the tightening effect coming
    primarily from the HPF/gain-staging, not the clipper.
  - Horizon Devices' own user guide gives the same shape of advice for its purpose-built
    metal tight-boost pedal: **"start with [drive] near zero at first"** then **"slowly turn
    the drive knob up to around 1-2 [out of 10] until you get the level of saturation [you
    want]"** — a small fraction of the control's travel, confirming "mostly clean push, a
    little clipper" is the reference-class default region, not "moderate-to-heavy clipper
    drive."
- **HPF/low-cut placement and target range, independent of the TS's own internal 720 Hz:**
  Nail The Mix / Fractal Audio forum material (search-summarized) consistently lands on
  **cutting roughly 80–120 Hz** ahead of the amp/gain stage for tight, fast palm mutes on
  drop-tuned guitars — "By using a high-pass filter on the DI signal (cutting everything below
  80-120Hz), you remove unnecessary low-end mud before it gets amplified and distorted,
  resulting in a much tighter, more focused, and less 'flubby' distorted tone, especially
  crucial for fast palm-muted riffs on low-tuned guitars." Also: "input EQ low cut at around
  80-100Hz depending on tuning and how tight you want it" attributed to the
  Misha-Mansoor/Ola-Englund-style "808 in front, drive low, level maxed" workflow.
  - This roughly brackets Overture's 130 Hz default (sits just inside/above the 80–120 Hz
    "sweet spot," consistent with a slightly tighter-than-typical starting point, which is
    reasonable for a plugin whose whole premise is the tightening trick) but suggests the
    *documented* sweet spot worth calling out explicitly (in manual/preset copy, not
    necessarily changing the numeric default) is 80–120 Hz, with 150–350 Hz as the
    heavier/drop-tuned-metal end of the mod-cap ladder from §3.
- **Purpose-built reference pedal control shape (Horizon Devices Precision Drive):** its
  low-cut/tightening control ("Attack") is a **discrete 6-position stepped switch**, not a
  continuous knob — "a 6-position Attack knob to control how tight your tone is... towards the
  left is more of a lower-mids punch, and to the right can get very defined and pick-y" (own
  user guide + review summaries). This is a genuine UX/authenticity data point: the
  reference-class product in this exact category (metal tight-boost, same job as Overture)
  ships stepped low-cut voicings rather than a sweepable-everywhere Hz knob, framed as
  discrete tonal *choices* ("thick/old-school" ... "modern/clear") rather than a lab-style
  frequency parameter. Its own user guide gives no exact Hz values per step (only qualitative
  descriptions), so this is UX/character lore, not a numeric spec to copy.
- **Calibrated unity/level relationship:** "6 on the Precision Drive is roughly equal to 10 on
  a Tubescreamer. So starting the volume at noon is usually a good bet" (Precision Drive user
  guide) — evidence that purpose-built tight-boost pedals in this category document an
  explicit "where to start the level knob" calibration point rather than leaving it as a
  blank ±dB trim, useful lore for preset/manual copy (not a DSP spec).

## 5. Gap analysis — concretely what v1 gets wrong/generic vs. the researched reference class

1. **Tightening mechanism is the wrong shape.** v1's Tight control is a static, level-
   independent pre-clip filter. The reference class achieves "tightness" via frequency-
   dependent gain *inside* the clipping stage (bass clipped less than treble, dynamically,
   more pronounced the harder you drive it) — a fundamentally more "alive"/program-dependent
   effect than a fixed HPF. This is the single biggest authenticity gap.
2. **Tone stage is cut-only; the reference class is a bidirectional tilt.** TS's Tone control
   boosts as well as cuts relative to a fixed passive roll-off point (~3.2 kHz). Overture's
   Tone can only darken (1–8 kHz LPF). No way to brighten at all.
3. **Clipper knee doesn't soften with drive.** Real diode clippers get *softer*-cornered as
   drive increases (the 51 pF cap effect); v1's clippers have one fixed knee shape per voicing
   at every drive level, and Hard Clip has literally zero knee ever.
4. **Asymmetry amount is fixed, not a spectrum.** Real asymmetric mods range from fully
   symmetric (both diode legs) to fully asymmetric (one leg removed); v1 hard-codes
   `a = 0.2` with no user control over degree of asymmetry — only a binary
   Asymmetric-vs-Symmetric *voicing choice*.
5. **Default gain-staging philosophy skews toward "boost is also the OD" more than the
   canonical modern-metal technique**, which is closer to "near-zero clipper drive, amp does
   the distorting." 8 dB Drive default is a reasonable middle ground but is not the most
   research-supported "authentic" starting point for the specific tight-boost-in-front-of-a-
   metal-amp use case the plugin is named after.
6. **No factory presets** capturing any of the above lore (boost-only / classic-808-push /
   heavier-OD-of-its-own) — explicitly planned for M2, which this brief's presets section
   should seed.
7. **Tight range/defaults aren't anchored to the two concrete, sourced numeric ladders**
   found in research (mod-cap ladder 34/72/154/339/720 Hz; workflow sweet spot 80–120 Hz) —
   v1's 20–400 Hz/130 Hz default isn't *wrong*, but the brief should make the anchoring
   explicit rather than leaving it merely "sounds about right."

## 6. Explicit gaps in this research pass (things not verified — for the honesty section)

- ElectroSmash (electrosmash.com) — the single most-cited primary technical source for TS
  circuit analysis — was DNS-unreachable in this environment; all ElectroSmash-attributed
  figures above are corroborated via independent secondary sources (GeoFex, stompboxelectronics
  .com) that converge on the same numbers (720 Hz corner, silicon diode 0.5–0.7 V threshold),
  not read from the primary page directly.
- No hardware measurement, oscilloscope capture, or SPICE simulation was performed in this
  pass — all figures are as reported by the cited secondary sources, not independently
  re-derived or measured against real hardware.
- Exact per-position Hz values for the Horizon Devices Precision Drive's 6-position Attack
  switch are not published in its own user guide (only qualitative "punchy" ↔ "picky"
  descriptions) — treat as UX/character lore only, not a numeric spec.
- Misha Mansoor / Ola Englund quotes above are as paraphrased/summarized by secondary
  coverage (Sweetwater/Reverb-sourced search summaries), not verified against a primary
  interview transcript in this pass.

## Sources

- GeoFex, "The Technology of the Tube Screamer" — http://www.geofex.com/article_folders/tstech/tsxtech.htm
- stompboxelectronics.com, "An Analysis of the Ibanez TS-9 Clipping Circuit" — https://stompboxelectronics.com/2023/04/03/an-analysis-of-the-ibanez-ts-9-clipping-circuit/
- ElectroSmash, "Tube Screamer Circuit Analysis" (cited via secondary corroboration only, page unreachable this session) — https://www.electrosmash.com/tube-screamer-analysis
- freestompboxes.org, Tube Screamer input-cap corner-frequency mod table — https://www.freestompboxes.org/viewtopic.php?f=1&t=27785
- Horizon Devices, Precision Drive official user guide — https://horizondevices.com/pages/users-guide-pd
- Horizon Devices, Precision Drive product page — https://horizondevices.com/products/precision-drive
- Nail The Mix, "What Are Your Go-To Parameters for Tightening a High-Gain Tone for Low Fast
  Palm Mutes?" (Fractal Audio Systems Forum, search-summarized) — https://forum.fractalaudio.com/threads/what-are-your-go-to-parameters-for-tightening-a-high-gain-tone-for-low-fast-palm-mutes.185649/
- Recording Base, "How to Build a Crushing Metal Guitar Tone for Free" — https://recordingbase.com/how-to-build-a-crushing-metal-guitar-tone-for-free/
- Reverb News / Sweetwater InSync coverage of Misha Mansoor's pedalboard/workflow
  (search-summarized) — https://reverb.com/news/misha-mansoor-of-periphery-outlines-the-ultimate-metal-pedalboard ; https://www.sweetwater.com/insync/how-to-dial-in-a-modern-metal-tone-with-misha-mansoor/
- Ibanez Tube Screamer schematic/tone-control analysis (3.2 kHz tilt corner,
  search-summarized) — https://schematron.org/ibanez-tube-screamer-schematic.html
