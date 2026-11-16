"""Render every parameter at both ends of its range and fail if any made no
difference -- for both plugins.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 180 frames (3 s at
60 fps) at 120 BPM, 320x180 (CI's raster), on the defaults: a 0.5 m ball
turning at 1 rpm, the pin spot 5 m off at 75 degrees, the room seen from the
back. Seed acts only on missing facets (Missing 0.6 set); Step On, Step
Angle and Glide only in Sync (Glide with Step On Bar, so the step is mid-glide
at 3 s). An option is set here by its name. A control whose ends differ by
less than `--floor` (mean 8-bit difference per channel) is barely alive:
vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context (keeping only the base), the
addendum's honesty check: what goes dead there is the list of controls that
only act with another one set, which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

SYNC = ["Motor=Sync"]

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Diameter": (["Diameter=0.1"], ["Diameter=0.9"], []),
    "Facet Size": (["Facet Size=0.1"], ["Facet Size=0.9"], []),
    "Missing": (["Missing=0"], ["Missing=0.8"], []),
    "Seed": (["Seed=1"], ["Seed=7"], ["Missing=0.6"]),
    "Motor": (["Motor=Free"], ["Motor=Sync"], []),
    "Speed": (["Speed=0.2"], ["Speed=0.8"], []),
    "Step On": (["Step On=Half Beat"], ["Step On=Bar"], SYNC),
    "Step Angle": (["Step Angle=0.2"], ["Step Angle=0.8"], SYNC),
    "Glide": (["Glide=0"], ["Glide=1"], SYNC + ["Step On=Bar"]),
    "Beam": (["Beam=0.1"], ["Beam=0.9"], []),
    "Throw": (["Throw=0.3"], ["Throw=0.9"], []),
    "Elevation": (["Elevation=0.3"], ["Elevation=0.7"], []),
    "Bearing": (["Bearing=0.3"], ["Bearing=0.7"], []),
    "Gobo": (["Gobo=Open"], ["Gobo=Star"], []),
    "Colour": (["Colour=White"], ["Colour=Red"], []),
    "Room": (["Room=0.1"], ["Room=0.9"], []),
    "View": (["View=Room"], ["View=Wall"], []),
    "Room Light": (["Room Light=0"], ["Room Light=0.5"], []),
    "Exposure": (["Exposure=0.3"], ["Exposure=0.7"], []),
    "Clip As": (["Clip As=Surface"], ["Clip As=Gobo"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The Over's own contexts: none. Its Gobo wheel acts in Surface (the
# default); in Gobo the clip is the gobo.
OVER_CONTEXT = {}

# The About block: a text line and browser buttons, with no pixel to sweep.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide"}


def read_png(path):
    """Enough of PNG for mbtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(mbtest, out, settings, over):
    args = [str(mbtest), "--out", str(out), "--size", "320x180", "--frames", "180"] + (["--over"] if over else [])
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"mbtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(mbtest, over):
    result = subprocess.run([str(mbtest), "--list"] + (["--over"] if over else []), capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"mbtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(mbtest, scratch, name, bare, over):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context + (OVER_CONTEXT.get(name, []) if over else []))
    tag = f"p{abs(hash(name))}{'o' if over else 's'}"
    before = render(mbtest, scratch / f"{tag}-a.png", context + low, over)
    after = render(mbtest, scratch / f"{tag}-b.png", context + high, over)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    mbtest = build / "mbtest"
    if not mbtest.exists():
        print(f"{mbtest} not found", file=sys.stderr)
        return 1

    failed = False
    for over in (False, True):
        plugin = "SW Mirror Over" if over else "SW Mirrorball"
        declared = parameters(mbtest, over)
        unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
        if unknown:
            # A new parameter with no sweep is a hole, not a pass.
            print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
            return 1
        dead, weak = [], []
        with tempfile.TemporaryDirectory() as scratch:
            scratch = pathlib.Path(scratch)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                names = [n for n in declared if n in SWEEP]
                results = list(pool.map(lambda n: sweep_one(mbtest, scratch, n, args.bare, over), names))
        for name, delta in results:
            if delta == 0.0:
                dead.append(name)
                print(f"  DEAD {plugin}: {name:16s} both ends identical")
            elif delta < args.floor:
                weak.append(name)
                print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
            elif args.verbose:
                print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
        print(f"{plugin}: {len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
              f"{len(dead)} dead, {len(weak)} barely alive")
        failed = failed or bool(dead or weak)
    if failed:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
