"""The browser demo against the plugin's harness: the same input frame, the same
raster, the same settings, two renderers.

    python3 demo/tools/crosscheck.py --mbtest build/mbtest [--keep DIR]

Needs a GPU, Google Chrome, numpy, a built mbtest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal), at ?size=WxH&speed=0.5 (Speed
    0 rpm from the first frame, so the ball's phase is 0 on both sides
    whatever either clock does), with &plugin=over&clip=C for the Over. The
    page is paused (the clip stands still). For the Over, Mix is set to 0
    first: the plugin then hands the clip through untouched, so the canvas IS
    the input frame, as 8-bit, and is read back. Then the case's settings are
    applied (Mix back to what it was) and the page's output is read back.
    preserveDrawingBuffer is forced on from outside so the canvas can be read
    after its frame; nothing in the page is changed for this.
  - by the PLUGIN: `mbtest --pipe --size WxH [--over] --set ...` (the real
    MirrorballPlugin in a GL 4.1 context), fed the same input bytes (zeros for
    the source, which takes none), with the same host values: every control
    as the page's own hostValue() sends it, the float32 printed to 9 digits.

The two outputs are compared byte for byte (RGB, and alpha separately). A case
passes at max 1 code and at most 1e-4 of its values off by any amount. A
negative control -- mbtest with Diameter one slider step (0.001) from the
page's -- must differ from the page by more than the defaults case does, so
the comparison is shown to see a slip that small.

Exit 1 when a case fails, the negative control passes, or the page logs an
error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

SIZE = "640x360"

# name, plugin, clip, settings by the plugin's own names (the page's
# kitValues(): {'units': x} is in the plugin's units and goes through its
# compiled ParamFrom*, an option is its element's name, an integer itself, a
# bare number the host's 0..1). Speed is 0 rpm throughout (the URL), and
# Motor stays Free: what is compared is the picture, not the clock.
CASES = [
    ("source, defaults", "source", None, {}),
    ("source, View Wall", "source", None, {"View": "Wall"}),
    ("source, star: 20 cm ball, lamp 0.5 m", "source", None,
     {"Diameter": {"units": 0.2}, "Throw": {"units": 0.5}, "Beam": {"units": 12}, "Gobo": "Star", "Colour": "Amber", "Bearing": {"units": 36}}),
    ("source, worn: 60% missing, Seed 7", "source", None,
     {"Missing": {"units": 0.6}, "Seed": 7, "Facet Size": 0.8, "Exposure": 0.6}),
    ("source, Dots, lamp 1 m behind", "source", None,
     {"Gobo": "Dots", "Throw": {"units": 1}, "Beam": {"units": 12}, "Bearing": {"units": 150}, "Room Light": 0.1}),
    ("source, big amber hall", "source", None,
     {"Room": {"units": 24}, "Colour": "Amber", "Room Light": 0.01, "Elevation": {"units": -20}, "Bearing": {"units": -60}}),
    ("over, defaults", "over", "scene", {}),
    ("over, Clip As Gobo, lamp 1 m behind", "over", "bars",
     {"Clip As": "Gobo", "Throw": {"units": 1}, "Beam": {"units": 12}, "Bearing": {"units": 150}}),
    ("over, Mix 50%, Bars gobo, Blue", "over", "grid", {"Mix": 0.5, "Gobo": "Bars", "Colour": "Blue"}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""

# Every control of the plugin on show, as the page sends it to the instance.
HOST = """import('./plugin.js').then(m => {
  const v = m.mounted.state.variant, out = [];
  for (const p of m.PARAMS) {
    const d = p.ff[v];
    if (!d) continue;
    const h = m.hostValue(d, m.mounted.params.get(p.id));
    if (h !== null) out.push([d.name, h]);
  }
  return out;
})"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.frames()
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.frames() > n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def mbtest(binary, over, host, frame, override=None):
    sets = []
    for name, value in host:
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    r = subprocess.run([binary, "--pipe", "--size", SIZE, *(["--over"] if over else []), *sets],
                       input=frame, capture_output=True, timeout=300)
    if r.returncode != 0:
        raise RuntimeError(f"mbtest {' '.join(sets)}: {r.stderr.decode()[:400]}")
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--mbtest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    args = ap.parse_args()

    w, h = map(int, SIZE.split("x"))
    server, base = serve()
    page = Page()
    failures = 0
    try:
        for name, variant, clip, settings in CASES:
            over = variant == "over"
            page.load(f"{base}?size={SIZE}&speed=0.5" + (f"&plugin=over&clip={clip}" if over else ""))
            page.after("document.querySelector('.btn--play').click()")
            if page.run("import('./plugin.js').then(m => m.mounted.state.playing)"):
                raise RuntimeError("the page did not pause")
            if over:
                mix = page.run("import('./plugin.js').then(m => m.mounted.params.get('mix'))")
                page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
                cw, ch, frame = page.canvas()
                if (cw, ch) != (w, h):
                    raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
                kit = {"mix": mix}
            else:
                frame = bytes(w * h * 4)
                kit = {}
            kit.update(page.run(f"import('./plugin.js').then(m => m.kitValues('{variant}', {json.dumps(settings)}))"))
            page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            cw, ch, ours = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {SIZE}")
            host = page.run(HOST)
            theirs = mbtest(args.mbtest, over, host, frame)

            a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
            b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
            diff = np.abs(a - b)
            rgb = diff[..., :3]
            off = int((rgb > 0).sum())
            ok = rgb.max() <= 1 and off <= 1e-4 * rgb.size and diff[..., 3].max() <= 1
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'}  {name:<38} {clip or '':<6} max {rgb.max()} code, {off} of {rgb.size} values off,"
                  f" alpha max {diff[..., 3].max()}  (picture mean {a[..., :3].mean():.1f} codes)")
            if name == "source, defaults":
                defaults_mean = rgb.mean()
                step = dict(host)["Diameter"] + 0.001
                slip = np.frombuffer(mbtest(args.mbtest, over, host, frame, {"Diameter": step}), np.uint8).reshape(h, w, 4).astype(np.int16)
                slip_rgb = np.abs(a - slip)[..., :3]
                caught = slip_rgb.mean() > defaults_mean
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {'negative: Diameter one step off':<38} {'':<6} max {slip_rgb.max()} codes, mean {slip_rgb.mean():.2e}"
                      f" against the defaults' {defaults_mean:.2e}, {int((slip_rgb > 0).sum())} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, name.replace(" ", "-").replace(",", "").replace(":", "").replace("%", ""))
                Image.fromarray(a[..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                Image.fromarray(b[..., :3].astype(np.uint8)).save(f"{stem}-mbtest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases agree' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
