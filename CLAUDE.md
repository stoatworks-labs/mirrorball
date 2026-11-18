# mirrorball

A mirror ball throwing a pin spot round the room, for Resolume Arena/Avenue, as two FFGL
plugins from one core: thousands of flat facets, each reflecting one narrow lamp by the law
of reflection, gathered per pixel; the clip as the spot's gobo; a slow motor, free-running
in rpm or stepping on the beat. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) +
Windows `.dll` (never built). MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the ball in a room | `MR01` | `SW Mirrorball` | `Mirrorball.bundle`, `com.stoatworks.ffgl.mirrorball` | source |
| the spots on the clip | `MR02` | `SW Mirror Over` | `Mirror Over.bundle`, `com.stoatworks.ffgl.mirrorball.over` | effect |

Read `AGENTS.md` before touching the gather (`source/Shaders.cpp`, the final pass), the
ramps' `rampEdge`, the gobo pass, the motor (`source/Motor.cpp`), the ring layout
(`source/Ball.cpp`), the Over's composite or its Exposure default. The design is
`~/Projects/resolume/specs/SPEC-mirrorball.md`; its "As built" section says where the code
departs from it.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64` (iteration builds here are
  `build-arm64`)
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/mbtest --out /tmp/mirrorball.png` (the source on its defaults
  at 120 BPM, 3 s in)
- The Over: `./build/mbtest --over --out /tmp/over.png` (a test card as the clip), or
  `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/mbtest --list` (`--over` for the effect's)
- Set anything by name: `./build/mbtest --set "Gobo=Star" --set "Throw=0.3"` (a standard
  control takes the host's 0..1; an option takes its name; anything unparsable, or not
  finite, exits 2)
- Change something later: `./build/mbtest --at 100 "Motor=Sync"`
- Clock and beat: `--fps N` (default 60), `--bpm N` (default 120; 0 sends no beat, and the
  plugin runs its own clock)
- Debug views: `--hooks 1` raw linear lux, `2` coverage, `4` spots only (bits)
- Film: `./build/mbtest --film 600 --size 1280x720 --script cues | ffmpeg -nostdin -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -nostdin -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/mbtest --over --pipe --size WxH | ffmpeg …`
  (a partial last frame is dropped; a reader that hangs up ends the run with exit 1)
- Cue sheets: `frame Name value` per line; an option by its name; standard controls ramp
  between keys, options step.

## Verify
- Everything: `OXBOW=… TMPDIR=… tools/verify.sh [BUILD_DIR]` (reserved-word and WebGL2
  greps, `glslc`, the submodule pin, a fresh universal build in `build-verify` with no
  warnings, both bundles through lipo, nm, plist, ad-hoc signature, `oxbow probe` and
  `selftest`; every check below at two rasters; five on Apple's software renderer;
  `--offline`; `--pipe` for both; Resolume's demo clips; the negative controls; 27
  mutants; the sweep; the bench).
- **Set `OXBOW`** to a built `oxbow` binary, or verify **fails**: nothing else checks what
  a host reads off the bundle. Build it out of tree: `cmake -S ~/Projects/resolume/oxbow -B
  <scratch>/oxbow-build && cmake --build <scratch>/oxbow-build` (never inside ~/Projects).
- **Set `TMPDIR`** to a writable scratch directory in a sandboxed Claude session, or the
  heredocs in `glslc.sh` and `mutate.sh` hang.
- **The optics** (GL, 1280x720 and 320x180 each): `--reflect` (the reflected direction from
  two planes' centroids: equal angles, one plane), `--area` (the spot's half-light contour
  against the gate cone's ellipse and the pupil's projection), `--flux` (one facet's light
  over a plane against what it intercepted), `--circles` (spots on a ceiling run round
  circles at the ball's rate), `--window` (which part of the gobo each spot carries, and
  that it is mirrored), `--missing` (the missing facets are the CPU's hash; the light kept),
  `--cap` (169 candidates at most; the light the cap loses).
- **The motor**: `--rate` (Free at any frame rate), `--clock` (a 499,000,000 ms host
  clock), `--sync` (steps start on the host's beat, inside the frame).
- **The rest**: `--alpha`, `--resize`, `--state`, `--names` (no GL), `--cues` (no GL),
  `--layout` (no GL: the facet counts).
- `--offline` runs the no-GL checks and their negatives (what CI would run).
- The checks can fail: `./build/mbtest --negative` (18 wrong models), `tools/mutate.sh`
  (27 one-character mutants, each built arm64-only and run at 320x180 under a 600 s alarm;
  `ONLY=n` runs one).
- No dead controls: `python3 tools/sweep.py --build build` (both plugins; `--bare` drops
  the per-control contexts and lists the controls that only act with another one set).
- The demo clips: `python3 tools/clips.py alpha|defaults --build build`, and
  `tools/clips.py sheet out.png` for a contact sheet.
- Render cost: `./build/mbtest --bench` (720p and 1080p; no 4K on a shared machine).
- `MBTEST_RENDERER=software` runs any of it on Apple's software renderer (slow; AGENTS.md
  lists what passed there).

## Notes
- **Units.** Metres; x right, y up, z toward the camera; the ball's centre is the origin,
  at 0.75 of the room's height. Irradiance is lux; display = albedo × (Room Light +
  E/3 lux × 2^Exposure), clamped in linear, no tone curve.
- **The motor runs on the CPU in double** (`source/Motor.cpp`); the phase goes to the GPU
  as a float in turns, reduced modulo 1. A Sync step's instant is found inside the frame.
- **The gather** is one fullscreen pass: for each surface pixel, the ideal facet normal,
  then ±K rings and ±K facets round it, each traced back to the pupil and the gate, with
  linear coverage ramps. K is capped at 6 (169 candidates). The gobo is a small mipmapped
  texture drawn first; the gate is applied analytically in the final pass, never baked
  into the texture.
- **No trig built-ins in GLSL**: `cosTurn`/`sinTurn`/`atan2Turn`. GLSL reserved words
  must not be identifiers; `verify.sh` greps for them.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element
  index. The Over's Exposure default is `DefaultOf`'s, not the table's.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `mirrorball_core` is an OBJECT library: each plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, don't edit here:** `source/StoatworksAbout.h` (stoatworks-backend's
  `sync-about.py`, from the website's projects.json: the hook and the User guide button)
  and `ATTRIBUTIONS.md` (`sync-attributions.py`, from the backend's `derived.json`).
  Registered 2026-11-18; the About block makes it 24 and 25 parameters.
- **Workflows.** `.github/workflows/ci.yml` builds on macOS (arm64, `mbtest --list` and
  `--offline`, `glslc`) and on Windows (MSVC, vcpkg GLEW, then `tools/wincheck.ps1` loads
  both DLLs as a host does: name, id, type, parameter count and the first parameter's
  name). `release.yml` builds the universal bundles and the x64 DLLs, zips, a dmg and an
  NSIS installer (`--ffgl`, per user into Extra Effects), and publishes only on a `v*` tag;
  a `workflow_dispatch` run builds without publishing. Both are haze's, adapted. The
  repo is public, so Actions minutes are free; `-Params` in both changes whenever a
  control or the About block does.
- Fleet repo: `~/Projects/resolume/mirrorball`, public at
  <https://github.com/stoatworks-labs/mirrorball> since 2026-11-18. Edit it in a worktree.

## Not done yet
- Run in Resolume only on one layer over a still: Arena 7.29.0 on a Mac (by REST, 2026-11-18)
  and Arena 7.27.1 on win-lab (the fleet's Arena gate, software rendering). Never in a show;
  Sync was seen to step in Arena, not checked against its beat there.
- No OpenFX port, no presets. The user guide is `docs/USER-GUIDE.md` (the website builds its
  page and PDF from it); its "Flashing" section has the measured flash rates.

## Browser demo
- `demo/` is served at https://mirrorball-demo.stoatworks-labs.com/ by the Worker in
  `wrangler.toml` (a route, plus a proxied AAAA `100::` record for `mirrorball-demo`: delete
  the record and the page goes dark while deploys stay green). `.github/workflows/deploy.yml`
  redeploys it on every push to main; by hand: `cf-run npx wrangler deploy`.
- **`demo/mirrorball-core.{js,wasm}` is the plugin's C++, compiled and committed.** After ANY
  change under `source/` (the generated `StoatworksAbout*.h` included), run
  `demo/tools/build-wasm.sh` (emscripten's `em++`) and commit both outputs (mode 644) and
  `demo/wasm/inputs.sha256`; `tools/verify.sh` fails until you do.
- `demo/shaders.js` is a copy: after changing a shader, `python3 demo/tools/check_shaders.py
  --write`. The same script checks every uniform's setter against its GLSL type and the
  .wasm's input hashes.
- The page has a photosensitivity gate (the warning at the top, links in the guide's flashing
  region open paused, presets checked against it at load). Never change a plugin default for
  it; if the guide's "Flashing" figures change, change `flashRegion()` in `demo/plugin.js`.
- Serve locally: `cd demo && python3 -m http.server 8000`. Against mbtest:
  `python3 demo/tools/crosscheck.py --mbtest build/mbtest` (Chrome on the real GPU).
- `demo/vendor/` is the shared kit (`stoatworks-backend/resolume-demo`); never edit it.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/mirrorball/mirrorball.YYYY-MM-DD.log
    %LOCALAPPDATA%\mirrorball\logs\mirrorball.YYYY-MM-DD.log

`MIRRORBALL_LOG_DIR` overrides the folder.
