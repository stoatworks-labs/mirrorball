"""Resolume's own demo clips through `mbtest --over --pipe`: the alpha check,
and the measurement the Over's Exposure default was set from.

    tools/clips.py alpha [--build DIR]      the Over's output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   what a spot does to each clip at the Over's
                                            defaults, and the Exposure they were set by
    tools/clips.py sheet OUT.png [--build DIR] [--set Name=V ...]   a contact sheet: four clips in, out

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV
with alpha, decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal
otherwise, and a script running it in a loop loses its input). Nothing here
is a second copy of the plugin's formulas: every number is read off the
pipe's frames.

**alpha.** The Over's decisions (AGENTS.md). Surface: the clip is the wall,
so the output alpha IS the clip's, and nothing shows under a transparent
pixel. Gobo: the room fills the frame where Mix shows it, so alpha is
mix( a, 1, Mix ), and the clip enters the light premultiplied. Checked on
BattleWeapon_Tank_09 and Ethnik2, the two clips with real transparency, at
Mix 1 and 0.5 in both modes: the alpha against its rule (to half a code, the
byte's rounding), and white painted under every fully transparent pixel
changes no output byte (the clips are straight alpha over black, so without
the paint a plugin that ignored alpha would pass). The negative control
paints ~200 opaque pixels instead, which must show.

**defaults.** In Surface the wall's light multiplies the clip in linear
light: out = clip x ( Room Light + E x Gain ), clamped at white. A spot's
gain on a clip pixel is out / clip in linear light, read off the frames in
the pixel's brightest channel wherever that is bright enough to read (code
>= 32) and the gain is over twice the room light (a spot is there). The decision: the Over's Exposure is
the largest, in sixth-stop steps, at which spots blow out (drive to 255 a
channel the clip had below 250) no more than 1% of the spot-lit pixels of
the median demo clip -- a spot shows the clip, it does not bleach it. This
prints, for every demo clip (`media/AV`, `media/Shop74`) at 640x360, 30
frames from 1 s in, at the binary's default: the lit share of the frame, the
median gain and the blown-out share. The default passes when the median clip
is within the limit there and over it a sixth of a stop higher (the blown
share only grows with Exposure); otherwise it bisects the range for the
value the clips give and fails.
"""

import argparse
import math
import pathlib
import statistics
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]
BLOWN_LIMIT = 0.01
EXPOSURE_LOW, EXPOSURE_HIGH = -4.0, 4.0  # the control's range (SPEC-mirrorball.md)


def exposure_param(stops):
    return (stops - EXPOSURE_LOW) / (EXPOSURE_HIGH - EXPOSURE_LOW)


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(mbtest, raw, width, height, settings, fps=30):
    args = [str(mbtest), "--over", "--pipe", "--size", f"{width}x{height}", "--fps", str(fps)]
    for s in settings:
        args += ["--set", s]
    result = subprocess.run(args, input=raw, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"mbtest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    return np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)


def linear(code):
    c = code.astype(np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def alpha_check(mbtest):
    width, height, count = 320, 180, 20
    failures, negative_caught, controls = 0, 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        a = clip[..., 3].astype(np.float64)
        white = clip.copy()
        hidden = white[..., 3] == 0
        white[..., :3][hidden] = 255
        for mode in ("Surface", "Gobo"):
            worst, changed, results = 0.0, 0, {}
            for mix in (1.0, 0.5):
                settings = [f"Clip As={mode}", f"Mix={mix}"]
                out = pipe(mbtest, raw, width, height, settings)
                results[mix] = out
                want = a if mode == "Surface" else a + (255.0 - a) * mix
                worst = max(worst, float(np.abs(out[..., 3].astype(np.float64) - want).max()))
                # Paint every fully transparent pixel white: the output must
                # not change by a single byte.
                painted = pipe(mbtest, white.tobytes(), width, height, settings)
                changed += int(np.count_nonzero(painted != out))
            # The negative control: the same paint on a few opaque pixels must show.
            shown = clip.copy()
            opaque = np.argwhere(shown[..., 3] == 255)
            pick = opaque[:: max(1, len(opaque) // 200)]
            shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
            control = pipe(mbtest, shown.tobytes(), width, height, [f"Clip As={mode}", "Mix=1"])
            controls += 1
            negative_caught += int(np.count_nonzero(control != results[1.0])) > 0
            rule = "the clip's" if mode == "Surface" else "mix(a, 1, Mix)"
            ok = worst <= 0.5 + 1e-9 and changed == 0
            failures += not ok
            print(f"  {'ok  ' if ok else 'FAIL'}  {path.name}, {mode}: {frames} frames at {width}x{height}, {hidden.mean():.0%} fully "
                  f"transparent; alpha within {worst:.2f} codes of {rule} at Mix 1 and 0.5 (tolerance 0.5, the byte's "
                  f"rounding); white painted under every transparent pixel changes {changed} output bytes (want 0)")
    print(f"  {'ok  ' if negative_caught == controls else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output in {negative_caught} of {controls} runs")
    failures += negative_caught != controls
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def spot_stats(clip, out, room_light):
    """The lit share, the median gain over lit readable pixels, the blown share."""
    cin = clip[..., :3]
    cout = out[..., :3]
    brightest = cin.argmax(axis=-1)[..., None]
    lin_in = linear(np.take_along_axis(cin, brightest, axis=-1)[..., 0])
    lin_out = linear(np.take_along_axis(cout, brightest, axis=-1)[..., 0])
    readable = cin.max(axis=-1) >= 32
    gain = lin_out / np.maximum(lin_in, 1e-6)
    lit = readable & (gain > 2.0 * max(room_light, 1e-3))
    blown = lit & ((cout == 255) & (cin < 250)).any(axis=-1)
    share = float(lit.mean())
    median = float(np.median(gain[lit])) if lit.any() else 0.0
    return share, median, (float(blown.sum()) / float(lit.sum()) if lit.any() else 0.0)


def defaults_from_list(mbtest):
    listing = subprocess.run([str(mbtest), "--list", "--over"], capture_output=True, text=True, check=True).stdout
    values = {}
    for line in listing.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 4 and parts[0].isdigit():
            values[" ".join(parts[1:-2])] = float(parts[-1])
    return values


def defaults(mbtest):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"no demo clips under {MEDIA}")
        return 1
    width, height = 640, 360
    listed = defaults_from_list(mbtest)
    room_light = listed["Room Light"]
    default_stops = listed["Exposure"] * (EXPOSURE_HIGH - EXPOSURE_LOW) + EXPOSURE_LOW
    decoded = []
    for path in clips:
        raw, frames = decode(path, width, height, 30)
        decoded.append((path, raw, np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)))

    def blown_median(stops, report=False):
        blown = []
        for path, raw, clip in decoded:
            out = pipe(mbtest, raw, width, height, [f"Exposure={exposure_param(stops):.7f}"])
            share, gain, b = spot_stats(clip, out, room_light)
            blown.append(b)
            if report:
                print(f"  {path.parent.name + '/' + path.name:38s} lit {share:6.2%} of the frame, median gain {gain:5.2f}, "
                      f"blown {b:6.2%} of the lit")
        return statistics.median(blown)

    print(f"At the Over's defaults (Room Light {room_light:.2f}, Exposure {default_stops:+.2f} stops), {width}x{height}, 30 frames from 1 s:")
    at_default = blown_median(default_stops, report=True)
    # Sixth-stop steps from the bottom of the range: the largest that blows
    # out no more than BLOWN_LIMIT of the median clip's lit pixels.
    grid = [EXPOSURE_LOW + i / 6.0 for i in range(int((EXPOSURE_HIGH - EXPOSURE_LOW) * 6) + 1)]
    above = blown_median(default_stops + 1.0 / 6.0) if default_stops + 1.0 / 6.0 <= EXPOSURE_HIGH + 1e-9 else None
    # --list prints the parameter to four decimals: 8 stops x 5e-5.
    on_grid = min(abs(default_stops - g) for g in grid) <= 8.0 * 5e-5 + 1e-9
    if on_grid and at_default <= BLOWN_LIMIT and (above is None or above > BLOWN_LIMIT):
        chosen = default_stops
        print(f"\n{len(clips)} clips: the median clip has {at_default:.2%} of its spot-lit pixels blown out at the default, "
              f"{above:.2%} a sixth of a stop higher" if above is not None else "")
    else:
        lo, hi = 0, len(grid) - 1
        if blown_median(grid[lo]) > BLOWN_LIMIT:
            print("even the bottom of the range blows out the median clip")
            return 1
        if blown_median(grid[hi]) <= BLOWN_LIMIT:
            chosen = grid[hi]
        else:
            while hi - lo > 1:
                mid = (lo + hi) // 2
                if blown_median(grid[mid]) <= BLOWN_LIMIT:
                    lo = mid
                else:
                    hi = mid
            chosen = grid[lo]
        print(f"\n{len(clips)} clips: at the default the median clip has {at_default:.2%} of its spot-lit pixels blown out; "
              f"the largest Exposure (sixth-stop steps) at which it stays within {BLOWN_LIMIT:.0%} is {chosen:+.3f} stops")
    ok = abs(default_stops - chosen) <= 8.0 * 5e-5 + 1e-9
    print(f"The Over's Exposure default in the binary: {default_stops:+.3f} stops (want {chosen:+.3f}, "
          f"the largest within {BLOWN_LIMIT:.0%})")
    print(f"\n  {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


def sheet(mbtest, out_path, settings):
    # Two bright clips, one of the AV set's black-ground ones, one with alpha.
    names = ["Shop74/IntoTheGlow_02.mov", "Shop74/NeonRoom2_32.mov", "AV/Beat 001.mov", "Shop74/Ethnik2.mov"]
    width, height = 480, 270
    rows = []
    for name in names:
        raw, frames = decode(MEDIA / name, width, height, 30)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)[-1]
        out = pipe(mbtest, raw, width, height, settings)[-1]
        # Alpha shown over a mid grey, so a transparent pixel reads as one.
        def over_grey(img):
            a = img[..., 3:4].astype(np.float64) / 255.0
            return (img[..., :3] * a + 128.0 * (1.0 - a)).astype(np.uint8)
        rows.append(np.concatenate([over_grey(clip), over_grey(out)], axis=1))
    picture = np.concatenate(rows, axis=0)
    args = ["ffmpeg", "-nostdin", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
            f"{picture.shape[1]}x{picture.shape[0]}", "-i", "-", str(out_path)]
    subprocess.run(args, input=picture.tobytes(), check=True)
    print(f"wrote {out_path}: {', '.join(names)}; the clip on the left, the Over on the right "
          f"({', '.join(settings) if settings else 'its defaults'})")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults", "sheet"])
    parser.add_argument("out", nargs="?")
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--set", action="append", default=[], help="sheet: a setting for the Over, repeatable")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    mbtest = build / "mbtest"
    if not mbtest.exists():
        print(f"{mbtest} not found", file=sys.stderr)
        return 1
    if args.what == "alpha":
        print("\n=== alpha on a real clip: Resolume's DXV alpha clips through --pipe, both modes")
        return alpha_check(mbtest)
    if args.what == "sheet":
        if not args.out:
            print("sheet needs an output path", file=sys.stderr)
            return 2
        return sheet(mbtest, args.out, args.set)
    return defaults(mbtest)


if __name__ == "__main__":
    sys.exit(main())
