# AGENTS.md: Mirrorball

Why the code is the way it is. `CLAUDE.md` is the command reference; the design is
`~/Projects/resolume/specs/SPEC-mirrorball.md`, whose "As built" section lists where this
repo departs from it. Built 2026-11-16 from the spec, in one session, local only.

## The idea

**Each facet is a flat mirror: it reflects the pin spot by the law of reflection and does
not change its divergence, so each spot is the pin spot seen through a small square
window.**

The ball is rings of equal latitude (`source/Ball.cpp`), each ring's facets at a hashed
offset; the pin spot is a uniformly bright 114 mm pupil of luminance I₀/(π(A/2)²) with the
gobo focused at infinity and a round gate of half-angle α. For every surface pixel P the
final pass finds the ideal facet normal (the bisector of P's direction and the pupil's),
searches ±K rings and ±K facets round it, and for each candidate reflects the ray from P
back through the facet (ω_in = û − 2(û·n)n), traces it to the pupil's plane and the gate,
and adds ρ L_e gobo(g) c_pupil c_gate Ω_f cos θ_P. The facet's finite size is a linear
coverage ramp across the pupil's rim and the gate's rim, half a facet (plus half a pixel's
footprint) wide; the ramp is the exact convolution of a square window with a straight
edge. The spill (the lamp's own light past the ball) is the same pupil-and-gate test with
the ball's shadow. The motor runs on the CPU in double and hands the phase over in turns.

What falls out (the README lists it): near-field spots carrying the whole gobo and
far-field spots carrying a window onto it, mirrored; ellipses at a slant; spots crowding
where each is dim; far spots racing; holes that turn with the ball; the spill's ring.

## Decisions taken without asking (the brief says decide and write it down)

- **The ramp's edge holds the disc's area (`rampEdge`).** The spec centred each coverage
  ramp on the rim and allowed the w²/(3r²) it adds. Measured, that was +3.1% of a facet's
  flux in the far field: a real bias, not a tolerance. The ramp's middle now sits at
  r_c = √(r² − w²/3) (and, when the ramp is wider than the disc, at (6wr²)^⅓ − w, where it
  never reaches 1), so the ramped disc holds exactly πr². What remains is second order:
  the light a ray carries isn't uniform over the pupil (cos³/L², the facet's cosine), and
  a ramp of half-width w raises the disc's second moment by w²/3, which moves the flux by
  about −(4/3)(w/L)². `--flux` allows 2(w/L)². The half-light contour now sits at r_c, not
  r, so `--area` and `--window` shrink their expected outlines by the same r_c, derived
  again in the harness (`rampEdgeRef`), not shared with the shader.
- **The gate is never baked into the gobo texture.** The first gobo pass masked the gate
  (|g| > 1 black) into the 256² mipmapped texture. Near field, the spot's light was 5.9%
  short: the mip levels blur the mask's edge inward, and the final pass's own analytic gate
  ramp then cut the blurred edge again. The texture is now the gobo alone, the gate only
  the final pass's ramp.
- **The clip enters the gobo premultiplied, filtered by hand (`clipAt`).** Resolume hands
  over straight alpha. Sampled through the host's bilinear filter, a transparent texel's
  colour bled into its neighbours' light (found by `--alpha`'s checker card, once mutant 24
  survived on a flat card). The gobo pass now fetches four texels, linearises and
  premultiplies each, and filters them itself.
- **The cap is K_max = 6: at most 169 candidates a pixel.** Measured with `--cap`: the
  defaults visit at most 49 (1280×720) and 81 (320×180). The worst setting the controls
  reach that I found, the 1 m ball of 5 mm facets with a 6° beam 0.9 m away, would visit
  2,401 uncapped; capped, it loses 0.77% of the light on the room (1.30% at 320×180). The
  bench gives its cost.
- **The Over's Exposure default is +5/6 stop, set from Resolume's demo clips; the source's
  stays 0.** The rule: the largest Exposure, in sixth-stop steps, at which the spots blow
  out (drive to 255 a channel the clip had below 250) at most 1% of the spot-lit pixels of
  the median clip. Spot-lit means the light's gain on the pixel's brightest channel, read
  off the frames as out/in in linear light where that channel is at least code 32, is over
  twice the room light. Over all 33 clips at 640×360, 30 frames each: 0.76% at +5/6, 1.42%
  at +1. One control table can't hold two defaults, so `DefaultOf( id, effect )` gives the
  Over its own; `tools/clips.py defaults` fails if the binary's default is not what the
  clips give. The source's 0 stays: E_ref = 3 lux was chosen so a default spot on the back
  wall reads at about 0.8 of white.
- **Room Light 0.03 and Clip As Surface are choices, not measurements.** The spec said
  both would be set on the demo clips. There is no measurement that picks an ambient level
  for a look that is mostly darkness; 3% is the source's, and keeps the clip faintly there
  between the spots. Clip As was judged by eye on a contact sheet of four clips
  (`tools/clips.py sheet`: IntoTheGlow_02, NeonRoom2_32, Beat 001, Ethnik2): Surface shows
  each clip through the spots; Gobo at the defaults turns every clip into tinted dots,
  each carrying a disc of it 0.27 of the gobo's radius, and the picture is lost. Four clips
  by eye is not a measurement; it's in the open questions.
- **Output alpha.** Source: 1 everywhere. Surface: the clip's alpha (no wall where it is
  transparent), RGB straight and zeroed where α = 0, so white painted under a transparent
  pixel changes nothing. Gobo: mix(α, 1, Mix), the room opaque wherever Mix shows it, the
  clip mixed in premultiplied. Mix 0: the clip back bit-exactly, whatever it carries under
  α = 0 (that is the host's own picture, passed through).
- **The harness writes the spec's constants itself.** `--flux` builds its lamp from
  `kA = 0.114`, `kSpecReflectance = 0.86` and the Chauvet literals (4,120 lux at 2 m,
  6°), and `--rate` sets Speed as the host value 0.6 (1 rpm), so a slipped constant in
  `Ball.h` or `Controls.cpp` fails instead of being shared by both sides. Mutants 6, 11,
  12 and 19 prove it.
- **`--missing` matches spots on a plane facing the lamp, not the back wall.** The first
  version looked for isolated spots' centre rays on the back wall and found none to
  sample. A plane square to the lamp's direction catches the facets facing the lamp, 191
  of them isolated at 1280×720. The identity checks (twins, re-initialised, 120 frames on)
  stay on the wall.
- **`--circles` predicts the light-weighted centroid, and runs at 5 rpm.** The ideal
  doubling law ignores the facet's offset R n from the centre, and the centre ray isn't
  the light's centroid either (the facet's cosine and 1/D² weight the spot); the harness
  integrates the rays over the pupil (100 × 100, in double) for the prediction, and
  allows how far the facet's own size moves it. 5 rpm covers a quarter turn in 3 s.
- **The bench renders no 4K**: the machine is shared (load near 20 at times today).
- **No GitHub workflows**, and no `.github/`.

## The traps actually hit

- **The ramp's own area** (above): +3.1% in the far field until `rampEdge`.
- **The gate baked into a mipmapped texture** (above): −5.9% in the near field.
- **Straight alpha through a bilinear filter** (above): found only once the alpha card
  had colour in its transparent texels. A flat card can't show it.
- **A check on the wrong plugin.** `--window` first built source rigs (`Rig()`), which have
  no clip to decode. `Rig( true )` is the Over.
- **No spots to sample on the wall** (above): `--missing` sampled 0 spots until it moved
  to a lamp-facing plane.
- **A tolerance tighter than the host's data.** `--sync` first held the step instants to
  1e-9 s and failed. FFGL hands the bar phase over as a float, so a beat's instant is known
  to 2⁻²⁴ bars: 1.1e-7 s at 128 BPM. The tolerance is now that; the starts are off by up
  to 3.4e-8 s.
- **The centre ray is not the centroid** (above): `--circles`' radius was off by more than
  its bound at 1280×720 until it predicted the light-weighted centroid.
- **The ramp's second moment** (above): `--flux`'s 0.6 m planes, at −1e-4 to −2.4e-4,
  were over a bound without the curve term.
- **Mutants that survived, and what they found.** Mutant 4 first turned the pupil plane's
  distance `/ den` into `* den`. For any ray that passes a 5° gate, den = ω·a is within
  0.1% of −1, so the mutant moved nothing `--area` can see: near-equivalent, replaced by
  one that moves the gate's half-light contour.
  Mutant 6 (the pupil's diameter) survived `--area` and is now `--flux`'s, which writes
  114 mm itself and catches it. Mutant 24 (the gobo ignoring the
  clip's alpha) survived a flat alpha card; the checker card that caught it found the
  straight-alpha bleed.
- **From the brief, avoided:** no GLSL reserved word as an identifier (`verify.sh` greps);
  no `||` in a mutant (`mutate.sh` splits on `|`); `std::max` drops NaN, so every "worst"
  in the harness goes through `worse()`, which keeps it; the objects and the binary are
  deleted before each mutant, and the mutant is proved to be in the binary; perl's alarm
  for a time limit (macOS has no `timeout`); `ffmpeg -nostdin`; `/usr/bin/head` and
  `command grep` in scripts; nothing heavy at 4K.

## Verified, and assumed

**Measured** (verify.sh, this Mac's GPU, 1280×720 and 320×180; the tolerances' sources are
in the next section): the law of reflection, the near-field ellipse and the far-field
disc, a facet's flux against what it intercepts, the circles and their rate, the gobo
window and its mirroring, the missing facets against the CPU's hash and the light they
take, Free's frame-rate independence, the 499,000,000 ms clock, Sync's steps on the host's
beat, the cap and its loss, alpha in both modes (on synthetic cards and on Resolume's two
DXV clips with transparency), resize, GL state, names, cues, the facet counts, `--pipe`'s
framing and refusals, both bundles through `oxbow`.

**Assumed**, each in the spec with its source or the lack of one:

- the ring layout, the ring offsets and the 10% grout (no sheet gives a layout or count);
- tile reflectance 0.86, EN 1036's floor for silvered float glass (the tiles' own not
  found);
- the pupil: 114 mm (a PAR36's diameter), uniformly bright, the gobo focused at infinity,
  a hard round gate; no LED pin spot sheet gives a lens size;
- the flux: Chauvet's 4,120 lux at 2 m with the 6° lens (141.9 lm), held as Beam changes;
  the GE 4515's own figures contradict each other;
- the room's proportions and albedos (walls 0.7, ceiling 0.5, floor 0.2), the filter
  colours, E_ref = 3 lux;
- Sync is a show-control feature, not a real motor.

**Not measured at all:** Resolume; Windows; how the picture reads at a show.

## Each claim, its tolerance, and where the tolerance comes from

Every picture check runs at 1280×720 and 320×180. Numbers are verify's run (1280×720
first).

| check | measured | tolerance | from |
| --- | --- | --- | --- |
| `--reflect` | angle off equal by 5.57e-5 rad (5.58e-5); out of plane 1.8e-7; worst at 0.09 (0.08) of the bound | per facet: (b₁ + b₂)/separation + β²(1 + tan θ_f) | b is a centroid's midpoint-rule bound, 1/(4·ramp px) of a pixel: the midpoint rule errs by \|slope change\|h²/8 per unit of edge at a linear ramp's kinks. β²(1 + tan θ_f) is the facet's cosine varying across the spot, which skews the centroid |
| `--area` near | area 1.53e-4 (7.26e-4), axis ratio 1.31e-4 (1.19e-3); 0.01 (0.02) of the bound | (√2·perimeter + 4)/area in pixels; 4× that for the axis ratio | the half-light contour can misclassify at most the pixels it crosses: √2 per pixel of perimeter, +4 for the corners |
| `--area` far | 2.05e-4 (7.84e-4); 0.02 of the bound | the same | the same |
| `--flux` | worst 2.42e-4 (2.84e-4), at 0.30 (0.12) of the bound | 2(w/L)² + perimeter/(8 w_px)/area + 1e-4 | the ramp's second moment (−(4/3)(w/L)², above), the midpoint rule at the ramp's kinks, float32 summed over a plane |
| `--circles` | centre 5.37e-5 m (7.82e-4); radius off the light-weighted centroid 1.1e-4 m (8.6e-4); rate 0.999995–1.000000 (0.999944–1.000091); worst 0.28 (0.06) | centre and radius: 2b + smear + 1e-5 m; rate: 2(b/ρ)/span + 1e-6 | two centroids' midpoint-rule bounds; `smear`, how far the facet's own size moves the centroid (computed in the harness: the shader's ramp models it only to second order); the prediction's quadrature; the rate's from the same bound over the quarter turn's angle |
| `--window` | radius 0.2715 against 0.2707 (0.2713/0.2706); worst 0.12 (0.10) | 0.4 w + 0.5 px + 2⁻¹²·2·aspect | the contour pixels sit inside the ramp's middle 40%, half a pixel, the gobo's half-float u near 0.5 |
| `--window` handedness | +1 reflected, −1 spill | sign | a classification over 224,716 and 133,652 pixels |
| `--missing` identity | 0.0 between twins, re-initialised, 120 frames on | 1e-5 of the picture | one context, one GPU: a deterministic rasteriser is all it needs |
| `--missing` spots | 0 of 191 (189) disagree | 0 | integer hash, the same on both sides |
| `--missing` light | 0.7016 (0.7019) against 0.7000 ± 0.0233 | 3σ of the binomial | weighted by each facet's flux: N_eff = (Σw)²/Σw² over 3,491 facets |
| `--layout` | 31,175 of 103,702 missing against 31,111 ± 443 | 3σ binomial | the same hash, counted on the CPU |
| `--rate` | the four rates agree to 6.2e-16 turns; 1 rpm's to 1.6e-8 | 1e-9 turns; the float control's half ulp, 2.0e-8 | double over 4 s; 0.6f is not 0.6, so 1 rpm is known to the float's half ulp × 10 rpm × 4 s |
| `--clock` | 1.0e-12 turns | 1e-9 | double phase reduced each frame |
| `--sync` | 3.4e-8 s (30 fps), 1.4e-8 s (240 fps); ψ between rates 2.1e-9 turns | 2⁻²⁴·240/BPM s = 1.1e-7 s; twice that × the glide's fastest rate for ψ | FFGL's bar phase is a float: each frame's end is off by half an ulp below 1, the beat between them by 2⁻²⁴ bars |
| `--cap` | 169 at most (2,401 uncapped); loss 0.77% (1.30%) | ≤ 169 exactly | the cap is a count |
| `--alpha` | 0 values differ (Mix 0); alpha 0 off; Room Light 1 returns the clip to 1.2e-7 | 0; 1e-6; 1e-4 | texelFetch is exact; one float mix; the sRGB round trip in float |
| demo clips alpha | within 0.00 (Surface) and 0.50 (Gobo) codes; 0 bytes changed | 0.5 code; 0 bytes | an 8-bit store rounds a half; the paint must not reach the output at all |
| `--resize` | 0 bytes | 0 | one context, deterministic |

## Would this hold on another rasteriser, at another raster?

Every tolerance above comes from pixel geometry (the midpoint rule, the boundary pixels,
half a pixel), from float formats (half-float u, float32 sums, a float bar phase), or from
counting statistics, and scales with the raster; none was fitted to this GPU. Both rasters
pass with every check at most 0.30 of its bound. Bit-exact comparisons (`--missing`'s
identities, `--resize`, `--alpha`'s Mix 0) stay within one GL context, because Apple's
software renderer is not bit-identical across contexts under load.

**Apple's software renderer** (`MBTEST_RENDERER=software`, 320×180), one run of every GL
check on 2026-11-16, each passing, with its time on a shared machine: `--reflect` 41 s
(worst at 0.08 of its bound), `--area` 27 s (0.02), `--flux` 58 s (0.12), `--circles` 72 s
(0.06), `--window` 25 s (0.14), `--missing` 109 s (0 disagreements; light kept 0.7019),
`--rate` 342 s, `--clock` 20 s, `--sync` 661 s (3.4e-8 s), `--cap` 35 s (loss 1.30%),
`--alpha` 96 s, `--resize` 470 s (0 bytes, both sizes), `--state` 23 s. Each check's worst fraction of
its bound is the GPU's at 320×180 except `--window`'s (0.14 against 0.10: radius 0.2717
against 0.2713); single numbers differ in their last digits (one `--flux` plane +2.41e-6
against +2.29e-6). A different rasteriser, the same answers inside the same bounds. Verify reruns the five cheapest that
between them cover the reflection, the outlines, the gobo's handedness, alpha and the GL
state (`reflect area window alpha state`); the rest are covered on the GPU at both rasters.

## The negative controls and mutants

`mbtest --negative`: 18 wrong models, each must fail its check (and each check also
asserts the thing measured happened: the spot in frame and off the edges, facets measured).

| check | the wrong model | failed |
| --- | --- | --- |
| reflect | the spot follows the facet's normal | 2 of 2 |
| reflect | reflection without its factor 2 | 2 of 2 |
| area | the gate's cone from the lamp's mirror image (apex at L + D) | 4 of 4 |
| area | the pupil ignored: every spot the gate's full cone (the brief's literal "divergence equals the pin spot's") | 2 of 4: the far field's, 4.4 times the area |
| flux | the facet's solid angle without its cosine | 7 of 8 (the near-field facet at 1.2° incidence moves only +6.6e-4, inside 320×180's bound of 8.8e-4; 720p's 2.0e-4 catches it) |
| flux | the facet's solid angle without 1/D² | 8 of 8 |
| circles | each ring turning at ω cos(latitude) | 2 of 2 |
| window | the pupil ignored: every spot the whole gobo | 2 of 4: the far field's |
| window | the reflected gobo read unmirrored | 4 of 4 |
| missing | the hash salted by the frame | 4 of 6 |
| rate | Free advancing 1/60 s a frame whatever the frame's length | 1 of 1 |
| clock | the phase from the host's absolute clock in float | 1 of 1 |
| sync | a step at its frame, not where the beat fell inside it | 1 of 1 |
| cap | the cap lifted (K up to 24) | 2 of 2 |
| alpha | Gobo passes the clip's alpha through | 1 of 5 |
| alpha | Surface keeps the colour under alpha 0 | 1 of 5 |
| resize | the motor reset when the frame changes size | 2 of 2 |
| cues | every control ramped between keys | 1 of 1 |

`tools/mutate.sh`: 27 one-character mutants of the shipped GLSL and C++, each built
arm64-only from deleted objects, proved to be in the binary, and run at 320×180 under a
600 s alarm (a timeout counts as caught). All 27 caught:

| # | the mutant | caught by |
| --- | --- | --- |
| 1 | GLSL: reflection's factor 2 made 3, 2 -> 3 | `--reflect` |
| 2 | GLSL: the facets placed against the ring's offset, + -> - | `--reflect` |
| 3 | C++: the CPU's facet normals mirrored (the GPU's are not), - -> + | `--reflect` |
| 4 | GLSL: the gate's half-light contour moved out by a fifth of its ramp, 5 -> 7 | `--area` |
| 5 | GLSL: the facet's ramp on the pupil from D - L, + -> - | `--area` |
| 6 | C++: the pupil a millimetre wide of the PAR36's (the near field's luminance 1.7% low), 4 -> 5 | `--flux` |
| 7 | GLSL: the ramp's edge set for w^2/4, not w^2/3, 3 -> 4 | `--flux` |
| 8 | GLSL: the wide ramp's edge (the second branch) from 7 w r^2, 6 -> 7 | `--flux` |
| 9 | GLSL: the facet's solid angle over 2D, not D^2, * -> + | `--flux` |
| 10 | GLSL: the wall's cosine from behind it, - -> + | `--flux` |
| 11 | C++: the Chauvet anchor misread, 2 -> 3 | `--flux` |
| 12 | C++: the facet's reflectance 0.88, 6 -> 8 | `--flux` |
| 13 | GLSL: the gather's window turned against the ball, - -> + | `--circles` |
| 14 | GLSL: the reflected gobo read unmirrored, ' ' -> - | `--window` |
| 15 | GLSL: the spill's gobo mirrored, ' ' -> - | `--window` |
| 16 | GLSL: the GPU's missing hash salted differently from the CPU's, 4 -> 5 | `--missing` |
| 17 | C++: the CPU's missing hash salted differently from the GPU's, 4 -> 5 | `--missing` |
| 18 | C++: Free's rpm over 50 s, 6 -> 5 | `--rate` |
| 19 | C++: Speed's range +-6 rpm, not +-5, 5 -> 6 | `--rate` |
| 20 | C++: the beat's place in its frame from the sum of phases, - -> + | `--sync` |
| 21 | C++: the glide's raised cosine starting at 0.1, 5 -> 6 | `--sync` |
| 22 | C++: the glide's length from 50 s a minute, 6 -> 5 | `--sync` |
| 23 | C++: the own clock's beats at 50 s a minute, 6 -> 5 | `--sync` |
| 24 | GLSL: the gobo ignores the clip's alpha, 0 -> 1 | `--alpha` |
| 25 | GLSL: the Over's alpha mixed toward clear, 1 -> 0 | `--alpha` |
| 26 | GLSL: Surface blanks every pixel, not only the clear ones, 0 -> 1 | `--alpha` |
| 27 | C++: the facets 20% apart, not 10%, 1 -> 2 | `--layout` |

## The sweep's honest list

`python3 tools/sweep.py` sweeps all 19 of the source's controls that have pixels and 20 of
the Over's (the About block has none; the source has no Clip As or Mix, the Over no View)
at both ends of their range and finds every one live. `--bare` drops the per-control
contexts and finds the same four dead in both plugins, for the reason each context exists:

- **Seed**: it only picks which facets are missing; at Missing 0 there are none.
- **Step On**, **Step Angle**, **Glide**: they act only in Sync; the default Motor is Free.

## Render cost

`mbtest --bench`, GPU time by GL_TIME_ELAPSED around the whole frame, median (and worst)
of 120 after 240 of warm-up, from verify's final run on 2026-11-16 with other builds on the
machine (load 6 to 11). No 4K: the machine is shared.

| | 720p | 1080p |
| --- | --- | --- |
| source, defaults (Room view) | 0.57 ms (worst 0.72) | 1.25 ms (1.40) |
| source, Wall view | 0.53 (0.65) | 1.19 (1.38) |
| source, 1 m ball of 5 mm facets (103,702), 6° beam at 0.9 m: the cap's worst case | 3.36 (3.62) | 7.12 (7.65) |
| Over, defaults (Surface) | 0.53 (0.61) | 1.27 (1.43) |
| Over, Gobo | 0.61 (0.67) | 1.38 (1.46) |
| Over, the cap's worst case | 3.50 (3.74) | 7.56 (7.98) |

At the defaults 1080p is about 7.5% of a 60 fps frame. The cap's worst case, every pixel
visiting 169 facets, is 43–45% at 1080p: the cap is what keeps it there (uncapped it would
visit 2,401). An earlier verify run the same hour gave the source's defaults 0.69 ms at
720p (worst 2.79, one frame) and the cap's worst case up to 6% less: the machine is shared.
One GPU only; a small GPU or the software renderer is another matter.

## Open questions

- **Clip As: Surface or Gobo?** Gobo is the idea in the brief ("the clip as the spot's
  gobo"), but at the defaults (lamp 5 m away, far field) each spot carries only a disc of
  the clip 0.27 of the gobo's radius across, and the picture is lost in tinted dots. With
  the lamp inside L\* (Throw under 1.31 m at 5°) every spot carries the whole clip. Should
  the Over's default Throw be near field, with Gobo the default?
- **Room Light 0.03 in the Over** leaves the clip nearly black between the spots. A VJ may
  want the clip more present (Room Light 0.1–0.2) and the spots as highlights.
- **Two Exposure defaults.** The source's is 0 (E_ref chosen), the Over's +5/6 (measured
  on the clips). Is a per-plugin default acceptable, or should one E_ref serve both?
- **The photometry is two fixtures.** Chauvet's LED flux, a PAR36's lens. A tungsten
  PAR36 pin spot's real figures would move the default exposure, not the look.
- **The 10% grout** sets the light lost between facets (17%); the sheets say "gapless".
- **The cap** drops 0.77% of the light at the worst setting found; a costlier K_max would
  remove it.
- **Never in Resolume.** Nothing is known about Arena's beat or clock reaching it, or how
  it reads at a show.
- **Never built on Windows.**
