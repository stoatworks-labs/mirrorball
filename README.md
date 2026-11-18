# mirrorball

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on macOS or Windows, and has never been built on Windows. Everything below is
> measured by an offline harness, `mbtest`, that drives the real plugin classes in a
> headless GL context on a synthetic clock, on this Mac's GPU at two rasters and partly on
> Apple's software renderer, and reads linear lux straight off the plugin's own float
> output. The light is checked against optics, not against screenshots. A facet's spot,
> caught on two planes, leaves at the angle it arrived to **5.6e-5 rad**. One facet's light
> summed over a plane is what it intercepted from the pin spot, at any distance and slant,
> to **2.8e-4** of it. Near the lamp each spot is the gate cone's ellipse to **7.3e-4** of
> its area. Spots on a ceiling run round circles at the ball's own rate to **1e-4**. Which
> facets are missing is the CPU's hash, spot for spot (**0** disagreements in 191). Sync
> steps start on the host's beat to **3.4e-8 s**. **18** deliberately wrong models and
> **27** one-character mutants of the shipped GLSL and C++ are all caught. The facets'
> layout, the pin spot's optics and its brightness are **assumptions**: see
> [Status](#status).

A mirror ball and its pin spot, for [Resolume](https://resolume.com) Arena and Avenue, as
two FFGL plugins: **SW Mirrorball**, a source that is a turning ball in a room full of its
spots, and **SW Mirror Over**, an effect in which the spots land on your clip, or carry it
as the pin spot's gobo.

![A dark hall seen from the front, a small dark ball hanging in the middle, and hundreds of
small white spots all over the ceiling and the walls, swirling round a point on the
ceiling, round on the right wall and stretched long and thin toward the ceiling's far
corners; a bright thin ring of light on the left wall](docs/mirrorball.png)

<sub>One frame of the source on its defaults, rendered by `mbtest`, the offline harness, not
captured from Resolume, on 16 November 2026 (`--size 1280x720`, 3 s in): a 50 cm ball of
6,452 10 mm facets, the pin spot 5 m off to the right. The ring on the left wall is the pin
spot's own light passing the ball, with the ball's shadow in it.</sub>

![The same hall in amber: each spot is now a whole five-pointed star, sharp near the ball and
stretched into long streaks toward the edges; on the left wall a larger star with the ball's
round shadow in its middle](docs/near.png)

<sub>The pin spot in close: a 20 cm ball, a 12° beam 0.5 m away, the Star gobo, Amber
(`--set "Diameter=0" --set "Throw=0" --set "Beam=1" --set "Gobo=Star" --set "Bearing=0.6"
--set "Colour=Amber"`). Every spot is a whole, mirrored copy of the gobo.</sub>

![Resolume's IntoTheGlow_02 clip, a copper four-pointed star of neon tubes, seen only through
a field of small round spots of light: inside each spot the clip shows in its own colour,
copper at the edges and lilac in the middle; between the spots the clip is nearly black](docs/over.png)

<sub>The Over on its defaults on the same harness, Resolume's bundled `IntoTheGlow_02` as
the clip, its 150th frame through `--pipe` (`--over --pipe --size 1280x720 --fps 30`).</sub>

## The one idea

**Each facet is a flat mirror. It reflects the pin spot by the law of reflection and does
not change its divergence, so each spot is the pin spot seen through a small square
window.**

The ball is rings of small square mirrors. One pin spot, a 114 mm lens with the gobo
focused at infinity, is aimed at it. For every pixel on the walls, the floor and the
ceiling, the plugin finds the few facets that could send it light, traces the ray back
through each to the pin spot, asks whether it passes the lens and the gate, reads the gobo
for that ray, and sums. Nothing is drawn as a spot.

So the plugins are the ball, the lamp and the motor:

- **the ball** is rings of equal latitude, each with as many facets as fit (6,452 on the
  default 50 cm ball of 10 mm glass, 103,702 on the 1 m ball of 5 mm), every facet a flat
  square with 10% grout, reflecting 86% (EN 1036's floor for silvered float glass);
- **the pin spot** is a uniformly bright 114 mm lens (a PAR36's diameter) of 141.9 lm, the
  flux Chauvet's LED Pinspot 2 measures (4,120 lux at 2 m with its 6° lens), held as the
  Beam changes; its gate is round, its gobo upright to someone standing behind it;
- **the motor** turns the ball about a vertical axis at a steady rpm (Free), or steps a set
  angle on the beat with a raised-cosine glide (Sync).

## What falls out

- **Two kinds of spot, and the throw decides which.** Nearer than L\* = A/(2 tan α) (1.31 m
  at the default 5° beam), each facet sees the whole gate and every spot is a whole,
  mirrored copy of the gobo. Further away, a facet sees only the lens, and its spot is a
  window onto the gobo, 0.27 of the gobo's radius at the defaults, centred on the facet's
  place in the beam: the clip arrives in pieces scattered round the room, each piece
  mirrored.
- **Spots stretch where they land at a slant.** A spot is a cone cut by a wall: round
  square-on, an ellipse twice as long as it is wide at 60°, and long thin streaks across
  the ceiling's far edge.
- **Spots crowd where each one is dim.** A sphere of mirrors sends the same light into
  every direction, so behind the ball, where the facets are grazing, the spots are packed
  tight and each carries little.
- **They turn as one, but the far ones race.** Every spot keeps the ball's angular rate, so
  its speed is ωρ: near the axis they crawl, toward the horizon they streak.
- **Calm.** At 1 rpm a spot 10 m away moves at about 1 m/s.
- **Holes.** A missing facet is a gap in the pattern that turns with the ball, the same gap
  in every instance and every frame.
- **The spill.** What passes the ball lands on the room with the ball's shadow in it. A
  beam that just fills the ball leaves a thin ring, lit only by the edge of the lens.

## Controls

| Group | |
| --- | --- |
| **Ball** | Diameter (0.2–1 m), Facet Size (5–20 mm), Missing (0–50% of the facets), Seed (which ones). |
| **Motor** | Motor (Free, Sync), Speed (−5 to 5 rpm; its sign is the Sync step's direction), Step On (Half Beat, Beat, 2 Beats, Bar), Step Angle (1–90°), Glide (0–1 of the step interval). |
| **Spot** | Beam (3–12°), Throw (0.5–10 m from the ball), Elevation (−90° to 90°), Bearing (−180° to 180°, 0 toward the camera), Gobo (Open, Dots, Ring, Star, Bars), Colour (White, Red, Amber, Green, Cyan, Blue, Magenta). |
| **Room** | Room (6–30 m across, half as high), View (source: Room, Wall), Room Light (0–1 of white), Exposure (±4 stops). |
| **Over** | Clip As (Surface, Gobo), Mix. |

**The defaults** are a 50 cm ball of 10 mm glass turning at 1 rpm (the Eurolite MD-2010's
one speed), lit by a 5° pin spot 5 m away, 10° up and 75° round to the right, that just
fills it, in a 12 m hall at 3% room light, seen from the front of the room. The Over's are
the same seen on the back wall, with the clip as the wall, and Exposure +5/6 stop, set from
Resolume's demo clips (below).

**Clip As**, in the Over:

- **Surface** (the default): the clip is printed on the back wall and lit by the room and
  the spots. "The spots land on the clip." Where the clip is transparent there is no wall:
  its alpha comes through, and nothing under a transparent pixel shows.
- **Gobo**: the clip, cropped square, is the pin spot's gobo, so every spot carries a piece
  of it, or all of it with the lamp in close. Transparent is no light. The room is opaque
  wherever Mix shows it.

Mix 0 hands the clip straight back.

## Status

**v0.1.0, 16 November 2026, unreleased, and early days.** It has **never been loaded into
Resolume** on either platform and never been built on Windows. `oxbow probe` reads the
bundles as a host does (`SW Mirrorball` / `MR01` / source, `SW Mirror Over` / `MR02` /
effect) and `oxbow selftest` renders through each. There's no user guide yet, no OpenFX
port and no browser demo.

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026, six minutes with the mutants). It runs every picture check at **two rasters**, 1280×720
and 320×180, and five of them again on **Apple's software renderer** at 320×180; every GL
check passed there once (AGENTS.md). Each tolerance below comes from the error it bounds,
not from a fit; AGENTS.md says where each comes from. What it establishes:

| check | result |
| --- | --- |
| `--reflect` | 9 facets at 15°, 35° and 55° incidence, three lamp positions, each spot caught on two parallel planes: the line through the two centroids leaves at the angle of incidence to **5.6e-5 rad** and in its plane to 1.8e-7, at 0.09 of the bound (the centroids' midpoint-rule error over the planes' separation) |
| `--area` | near field (lamp 0.70 m, inside L\* 1.31 m): the spot's half-light area is the cone–plane ellipse's at 0°, 30°, 50° and 60° incidence to **1.5e-4** (7.3e-4 at 320×180), its axis ratio (1.000, 1.155, 1.558, 2.006) to 1.3e-4 (1.2e-3); far field (lamp 5 m): the lens's disc projected through the facet, to 2.1e-4; both at 0.01–0.02 of the boundary-pixel bound |
| `--flux` | one facet's light summed over a plane against the flux it intercepted from the pupil and the gate, computed in the harness from the spec's numbers (114 mm, 0.86, 4,120 lux at 2 m): far field and near field, 10 and 20 mm facets, planes 0.6–4 m away and at 40°: within **2.4e-4** at 720p and 2.8e-4 at 320×180, at most 0.30 of the bound (the ramp's second moment plus the midpoint rule) |
| `--circles` | the lamp below on the axis, a ceiling, a quarter turn at 5 rpm: three rings' spots each run round a circle centred on the axis (to 5.4e-5 m; 7.8e-4 at 320×180) at **0.999995–1.000000** of the motor's rate (0.999944–1.000091); radii 0.654, 1.147 and 1.843 m, the rays' light-weighted centroids to 1.1e-4 m (the ideal doubling law, 0.736 to 2.001 m, ignores the facet's offset from the centre) |
| `--window` | the Over in Gobo with a uv-coded clip: far field, each spot carries a disc of the gobo **0.2715** of its radius across (expected 0.2707 from the lens's angular size, bound 0.034), centred on the facet's place in the beam; near field, the whole gobo; a reflected spot is mirrored (handedness +1) and the spill is not (−1) |
| `--missing` | Missing 30%: two instances, a re-initialised one and 120 frames on are **bit-identical**; 191 isolated spots sampled, 64 missing by the CPU's hash, the GPU disagreeing at **0**; the light kept 0.7016 against 0.7000 ± 0.023 (3σ of the binomial, 3,491 facets) |
| `--rate` | Free at 1 rpm: the phase after 4 s is the same at 24, 30, 60 and 240 fps to **6e-16 turns**, and 1 rpm's to 1.6e-8 (the float control's half ulp, 2.0e-8) |
| `--clock` | 20 frames at a 499,000,000 ms host clock: each frame's step off rpm/60·dt by **1e-12 turns** |
| `--sync` | 128 BPM through a faked SetBeatInfo, at 30 and 240 fps: every step starts on its beat to **3.4e-8 s** (bound 1.1e-7, a float bar phase's resolution); the phase at 2.5 s agrees between the rates to 2.1e-9 turns; with the host's phase standing still, its own clock steps at the last BPM |
| `--cap` | the 1 m ball of 5 mm facets with a 6° beam 0.9 m away: at most **169** candidates a pixel (the cap) where it would want 2,401, losing **0.77%** of the light (1.30% at 320×180); at the defaults at most 49 |
| `--alpha` | Mix 0 returns the clip bit-exactly; Surface passes the clip's alpha and ignores what is under alpha 0; Gobo's alpha is mix(a, 1, Mix) exactly; Room Light 1 with no spots returns the clip to 1.2e-7; the source's alpha is 1 |
| `--resize`, `--state` | a raster change mid-run is **0 bytes** different from an instance at the new size all along; the host's GL state comes back, both plugins |
| `--names`, `--cues`, `--layout` | 23 and 24 parameters within FFGL's 16 characters, the About block last; a cue sheet ramps and steps them; nine balls' facet counts as the spec tabulates, no two facets overlapping |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; a bad cue, a bad option and a non-number refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| demo clips | the Over's alpha on Resolume's two DXV clips with transparency, both modes, Mix 1 and 0.5: alpha to half a code, and white painted under every transparent pixel changes **0** output bytes |
| `--negative` | **18** deliberately wrong models, **18** caught |
| mutation | **27** one-character mutants of the shipped GLSL and C++, **27** caught |
| `tools/sweep.py` | all **19** controls of the source and **20** of the Over that have pixels measurably change the picture |
| shaders | all 3 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reads their name, id and type and renders through both |

Render cost, GPU time by `GL_TIME_ELAPSED`, median of 120 frames after 240 of warm-up,
from verify.sh's run on 16 November 2026 on an Apple Silicon Mac shared with other builds.
At the defaults the source takes **0.57 ms** at 720p and **1.25 ms** at 1080p; the Over
**0.53** and **1.27 ms** (Gobo 0.61 and 1.38). At the cap's worst case, the 1 m ball of
103,702 facets with a 6° beam 0.9 m away (169 facets visited a pixel), the source takes
3.36 and 7.12 ms and the Over 3.50 and 7.56: 43–45% of a 60 fps frame at 1080p. No 4K on a
shared machine; an earlier run the same hour differed by up to 21%. macOS figures only, on
one GPU.

**Resolume's demo clips.** All 33 of the clips this Mac has (`media/AV`'s 18 and
`media/Shop74`'s 15) went through the Over by `--pipe` at 640×360, 30 frames each. The
Exposure default is the largest, in sixth-stop steps, at which the spots blow out no more
than 1% of the median clip's spot-lit pixels: **+5/6 stop**, where the median is 0.76%
(1.42% a sixth higher). At it a spot shows the median clip at about three quarters of its
own brightness. Clip As stays Surface, judged by eye on a contact sheet of four clips:
under Gobo at the defaults each spot carries a disc of the clip about a quarter of its
width, so the picture is lost in tinted dots.

### Not established

- **Resolume.** Never loaded, on either platform, so nothing here is known about how Arena
  sends its beat or clock to it, or how it looks or performs in a composition.
- **Windows.** Never built.
- **The layout is an assumption.** No ball's sheet gives a facet count or arrangement. The
  rings, the hashed ring offsets and the 10% grout are this project's.
- **The pin spot is two fixtures.** Its flux is Chauvet's LED Pinspot 2's, its lens a
  PAR36's diameter: no LED pin spot sheet gives a lens size, and the GE 4515's own
  photometry contradicts itself. The lens is uniformly bright, the gate hard, the focus at
  infinity: the real 50% and 10% beam profiles on the sheets are not modelled.
- **Perfect tiles.** Every facet is flat and exactly aligned, reflecting EN 1036's floor
  (the tiles' own figure wasn't found); no tilt scatter, no chipped or dulled tiles, no
  wobble on the motor.
- **No air, no bounce.** No beams are seen, only spots on surfaces, and walls do not light
  walls. The room is an empty box, its albedos chosen.
- **The cap.** With very wide, grazing spots (the biggest balls, the lamp in close) the far
  side of each spot is dropped: 0.77% of the light at the worst setting.

## Build

macOS (universal: Apple Silicon and Intel), from a checkout with its submodule
(`git submodule update --init --recursive`):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Mirrorball.bundle` and `Mirror Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena and they're under Sources as
**SW Mirrorball** and under Effects as **SW Mirror Over**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW). It has never been built there.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no
warnings, both bundles' architecture, plist and ad-hoc signature, a host's view of them
through `oxbow` (verify fails without it), every check at two rasters and five on the
software renderer, the pipe, Resolume's demo clips, the negative controls, the mutants, the
sweep for dead controls and the render cost. `CLAUDE.md` lists the checks one by one.

Render a frame of your own:

```bash
./build/mbtest --out /tmp/mirrorball.png --set "Gobo=Star" --set "Colour=Amber"
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
