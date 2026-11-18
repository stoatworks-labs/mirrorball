/**
 * Mirrorball — browser demo.
 *
 * A mirror ball turning in a dark hall full of its spots (SW Mirrorball, MR01,
 * a source) and the same spots landing on a clip, or the clip as the pin
 * spot's gobo (SW Mirror Over, MR02, an effect). The one idea, from AGENTS.md:
 * nothing is drawn as a spot. Every facet is a flat mirror reflecting one
 * narrow pin spot by the law of reflection, and for each pixel of the room the
 * plugin traces the light back through the few facets that could send it, to
 * the lamp's lens and its gate.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `mirrorball-core.wasm`
 * is the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Mirrorball.cpp` — the plugin class, with
 * its constructor, its clock and seconds-or-milliseconds vote, the gobo and
 * ring-table buffers and every uniform it sets — and everything it calls: the
 * motor (Motor.cpp: Free and Sync, in double, on the CPU), the ball's rings,
 * the room and the lamp's place (Ball.cpp, in double, on the CPU), Controls,
 * PassBuffer, Shaders and Diag, with the FFGL SDK's CFFGLPlugin, its parameter
 * bookkeeping, FFGLShader, FFGLFBO and the screen quad. Its GL calls go to this
 * page's WebGL2 context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what the two
 *     registration files do in a bundle), reads its parameter declarations
 *     back through the SDK's host getters — the panel below is built from
 *     them, not from a list typed here — and forwards SetFloatParameter,
 *     SetTime, SetBeatInfo and ProcessOpenGL, as tools/mbtest's rig does.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()` — the version line and the ES precision
 *     defaults, nothing else. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2.
 *   - **The host row**: the tempo (Resolume's BPM and bar phase, here the
 *     page's), which Sync steps on.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's mb_convert pairs each control with
 *     the function Mirrorball.cpp applies to it); the units printed after it,
 *     the tooltips and the presets are this page's words. The presets are
 *     written in metres, degrees and rpm and go to the host's 0..1 through the
 *     plugin's own inverse functions (mb_param_from).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page**, as fountain's demo does it: the Plugin dropdown
 * is a new instance at that constructor's defaults. `?plugin=over` opens the
 * effect. The two declare the same controls but View (the source's), Clip As
 * and Mix (the Over's), and Exposure has a different default in each.
 *
 * **Seed is a number field.** It is FF_TYPE_INTEGER, which the kit has no
 * control for (pinhole's Open Zones is the precedent): a text row turned into
 * a number field clamped to the declared range.
 *
 * **Each new instance is primed.** The plugin votes on whether its host's
 * clock is seconds or milliseconds and, until four frames agree, turns the
 * ball on the wall clock. A page that opens paused would never settle it, and
 * every control moved would then turn the ball by the wall time since the last
 * redraw — while paused. So each new instance is first handed five frames a
 * millisecond or so apart; the ball turns by those few milliseconds.
 *
 * **Restart is a new instance**, at the panel's settings: the ball back at its
 * starting phase. Sending the plugin a clock that jumps back would leave a
 * Sync move in flight until the clock came round to its end again.
 *
 * **Photosensitivity.** It can flash with a small ball, the lamp in close and
 * a fast turn (index.html says what was measured). The page opens on the
 * defaults, which measured no flashes; no preset is in that region (checked
 * below, at load); a link that opens in it opens paused, with Play to start
 * it; and a caution line under the picture says so whenever the panel is in
 * it.
 *
 * **The About block is not on the panel**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createMirrorball from './mirrorball-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of InitGL() and Assemble() by check_shaders.py), one
// text per stage of each of the two passes.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.filter((s) => s.stage === stage && s.text === text);
  if (!ours.length) {
    shaderCheck.refused += 1;
    return '';
  }
  for (const s of ours) shaderCheck.matched.add(s.name);
  return port(text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createMirrorball({
    mirrorballShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/mirrorball.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

//===========================================================================
// The declarations, read from each plugin's constructor through the SDK.
//===========================================================================

function declarations(effect) {
  const h = core._mb_new(effect ? 1 : 0);
  const list = [];
  const n = core._mb_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._mb_param_type(h, i);
    const d = { index: i, name: text(core._mb_param_name(h, i)), type, group: text(core._mb_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._mb_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._mb_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._mb_param_element_name(h, i, e)));
        d.values.push(core._mb_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_INTEGER) {
      d.min = core._mb_param_range_min(h, i);
      d.max = core._mb_param_range_max(h, i);
    }
    list.push(d);
  }
  const inputs = [core._mb_min_inputs(h), core._mb_max_inputs(h)];
  // Kept, never given GL: the read-outs and the presets ask it for
  // Controls.cpp's conversions, which need only its host-index table.
  return { list, inputs, converter: h };
}

const DECLARED = { source: declarations(false), over: declarations(true) };
if (DECLARED.source.inputs.join() !== '0,0' || DECLARED.over.inputs.join() !== '1,1') {
  throw new Error(`the plugins declare ${DECLARED.source.inputs.join('–')} and ${DECLARED.over.inputs.join('–')} inputs; this page hands the source none and the Over exactly one`);
}

/** Controls.cpp's conversion of a host value, for a declaration of a plugin. */
const convert = (variant, d, v) => core._mb_convert(DECLARED[variant].converter, d.index, Math.fround(v));
/** And its inverse: the host's 0..1 for a value in the plugin's units. */
const paramFrom = (variant, d, units) => core._mb_param_from(DECLARED[variant].converter, d.index, units);

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const signed = (n, d) => `${n > 0 ? '+' : n < 0 ? '−' : ''}${fixed(Math.abs(n), d)}`;
const UNITS = {
  Diameter: (x) => `${fixed(x, 2)} m`,
  'Facet Size': (x) => `${fixed(x * 1000, 1)} mm`,
  Missing: (x) => `${fixed(x * 100, 0)}%`,
  Speed: (x) => `${signed(x, 2)} rpm`,
  'Step Angle': (x) => `${fixed(x, x < 10 ? 1 : 0)}°`,
  Glide: (x) => `${fixed(x, 2)} of a step`,
  Beam: (x) => `${fixed(x, 1)}°`,
  Throw: (x) => `${fixed(x, 2)} m`,
  Elevation: (x) => `${signed(x, 0)}°`,
  Bearing: (x) => `${signed(x, 0)}°`,
  Room: (x) => `${fixed(x, 1)} m`,
  'Room Light': (x) => `${fixed(x * 100, 1)}% of white`,
  Exposure: (x) => `${signed(x, 2)} stops`,
};

function readout(d, variant) {
  return (v) => {
    const x = convert(variant, d, v);
    if (Number.isNaN(x)) return d.name === 'Mix' ? `${fixed(v * 100, 0)}%` : fixed(v, 2);
    return (UNITS[d.name] ?? ((y) => fixed(y, 3)))(x);
  };
}

/** Tooltips: this page's words, from the user guide's description of each control. */
const HINTS = {
  Diameter: 'The ball’s size, 0.2–1 m; the slider is linear, its middle 0.6 m. The ball hangs at three quarters of the room’s height.',
  'Facet Size': 'Each square mirror’s side, 5–20 mm; the slider is geometric, its middle 10 mm. Rings of equal latitude, as many facets as fit, 10% grout between them, each reflecting 86%.',
  Missing: 'Facets taken out, 0–50%, chosen by a hash of Seed. Each gap is a hole in the pattern that turns with the ball.',
  Seed: 'Which facets are missing, 0–999. The same Seed gives the same gaps in every instance and every frame. FF_TYPE_INTEGER in the plugin: a number field here, clamped to its declared range.',
  Motor: 'Free turns at a steady Speed. Sync steps on the host’s beat — here the page’s tempo, in the host row under the picture.',
  Speed: 'Free’s rate, −5 to +5 rpm, either way; the slider’s middle is 0. In Sync only its sign counts: the step’s direction. Over 3 rpm with a small ball and the lamp in close, it can flash (the note at the top).',
  'Step On': 'How often Sync steps: every half beat, beat, two beats or bar of the tempo in the host row.',
  'Step Angle': 'How far each Sync step turns the ball, 1–90°; the slider is geometric, its middle about 9.5°.',
  Glide: 'How much of the step interval Sync’s move takes, on a raised cosine. 0 jumps; 1 moves for the whole interval. With any Glide, Sync can turn the ball far faster than Free’s 5 rpm: keep it at 0 with a small ball and the lamp in close.',
  Beam: 'The pin spot’s beam angle, 3–12°. The brightness stays the same as the beam widens, so the light per facet drops.',
  Throw: 'The lamp’s distance from the ball, 0.5–10 m; the slider is geometric, its middle about 2.2 m. Nearer than about 1.3 m at a 5° beam, each facet sees the whole gate and every spot is a whole, mirrored copy of the gobo; further off, a window onto it. The room’s walls stop the lamp short of them.',
  Elevation: 'How far above (or below) the ball’s level the lamp is, −90° to +90°. −90° is straight below.',
  Bearing: 'How far round the ball the lamp is, −180° to +180°; 0 is toward the camera, positive to the right.',
  Gobo: 'The pattern in the pin spot’s gate: Open, Dots, Ring, Star, Bars. In SW Mirror Over, Clip As Gobo replaces it with the clip.',
  Colour: 'The lamp’s colour.',
  Room: 'How wide the hall is, 6–30 m; it is half as high.',
  View: 'SW Mirrorball only. Room looks into the hall from the front; Wall looks straight at the back wall, the one SW Mirror Over prints the clip on.',
  'Room Light': 'Light on every surface besides the spots, as a fraction of white.',
  Exposure: 'The camera’s exposure, ±4 stops. The spots are lux off a real lamp’s flux; Exposure scales them. 0 in SW Mirrorball, +5/6 stop in SW Mirror Over (set on Resolume’s demo clips).',
  'Clip As': 'SW Mirror Over only. Surface: the clip is printed on the back wall and lit by the room and the spots. Gobo: the clip, cropped square, is the pin spot’s gobo, so every spot carries a piece of it, or all of it with the lamp in close.',
  Mix: 'SW Mirror Over only. 0 hands the clip straight back, untouched.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed). Shared names whose declarations
// agree but for the default are one row (Exposure's default differs); a name
// only one plugin declares carries `only`.
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_INTEGER]: 'text',
};

/** The kit reads these from the query string itself, and this page reads `plugin` and `bpm`. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg', 'plugin', 'bpm']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

const onPanel = (d) => d.group !== 'About';

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_INTEGER: return String(Math.round(d.default));
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

/** The same control in both plugins: everything but its host index and default. */
const sameControl = (a, b) => JSON.stringify({ ...a, index: 0, default: 0 }) === JSON.stringify({ ...b, index: 0, default: 0 });

const PARAMS = [];
{
  const over = DECLARED.over.list.filter(onPanel);
  const source = DECLARED.source.list.filter(onPanel);
  for (const d of [...source, ...over]) {
    if (KIT_TYPES[d.type] === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  }
  // The source's order, then anything only the Over declares, after the last
  // row of its own group (Clip As and Mix, in the Over's own group, last).
  for (const s of source) {
    const d = over.find((x) => x.name === s.name);
    if (d && !sameControl(s, d)) throw new Error(`${s.name}: the two plugins declare it differently, and this page has no row for that`);
    const p = { id: addressOf(s.name), name: s.name, type: KIT_TYPES[s.type], group: s.group, hint: HINTS[s.name], ff: { source: s, over: d ?? null } };
    if (!d) p.only = 'source';
    PARAMS.push(p);
  }
  for (const d of over) {
    if (source.some((x) => x.name === d.name)) continue;
    const p = { id: addressOf(d.name), name: d.name, type: KIT_TYPES[d.type], group: d.group, hint: HINTS[d.name], ff: { source: null, over: d }, only: 'over' };
    let at = PARAMS.length;
    for (let i = PARAMS.length - 1; i >= 0; i -= 1) if (PARAMS[i].group === d.group) { at = i + 1; break; }
    PARAMS.splice(at, 0, p);
  }
  for (const p of PARAMS) {
    const d = p.ff.source ?? p.ff.over;
    if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
    if (d.type === FF_TYPE_STANDARD) p.display = (v) => readout(p.ff[currentVariant()] ?? d, p.only ?? currentVariant())(v);
  }
}

/** Each constructor's defaults, by kit id; a row the plugin lacks keeps the other's. */
const DEFAULTS = { source: {}, over: {} };
for (const p of PARAMS) {
  for (const variant of ['source', 'over']) {
    DEFAULTS[variant][p.id] = kitDefault(p.ff[variant] ?? p.ff.source ?? p.ff.over);
  }
}

const query = new URLSearchParams(window.location.search);
const embed = query.has('embed') && query.get('embed') !== '0';
const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';
for (const p of PARAMS) p.default = DEFAULTS[initialVariant][p.id];

//===========================================================================
// The host's tempo. Resolume sends its BPM and bar phase every frame
// (SetBeatInfo); here the page is the host. The bar phase runs on the page's
// clock at the tempo in the host row, in 4/4, continuous across a tempo
// change, and stands still when paused.
//===========================================================================

const TEMPOS = [60, 90, 100, 120, 128, 140, 150, 174];
const host = {
  bpm: TEMPOS.includes(Number(query.get('bpm'))) ? Number(query.get('bpm')) : 120,
  phase: 0,
  lastTime: null,
};

//===========================================================================
// Where the guide measured flashing, as this page tests it: the ball 0.3 m
// or smaller, Throw 1.5 m or nearer, and the ball turning fast — over 3 rpm
// in Free; in Sync, any Glide or more than six steps a second. In the plugin's
// own units, through its own conversions.
//===========================================================================

const BEATS_PER_STEP = { 'Half Beat': 0.5, Beat: 1, '2 Beats': 2, Bar: 4 };

/** `value(name)` is a kit value; true when those settings are in the region. */
function flashRegion(variant, value, bpm) {
  const units = (name) => {
    const p = PARAMS.find((x) => x.name === name);
    return convert(variant, p.ff[variant], value(p.id));
  };
  const option = (name) => {
    const p = PARAMS.find((x) => x.name === name);
    return p.elements[Math.round(value(p.id))];
  };
  if (!(units('Diameter') <= 0.3 + 1e-9 && units('Throw') <= 1.5 + 1e-9)) return false;
  if (option('Motor') === 'Free') return Math.abs(units('Speed')) > 3;
  const stepsPerSecond = bpm / 60 / BEATS_PER_STEP[option('Step On')];
  return units('Glide') > 0 || stepsPerSecond > 6;
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (metres,
// degrees, rpm, a fraction) and is converted by the plugin's own inverse
// (mb_param_from); an option is its element's name; Seed is its number; a
// bare number is the host's 0..1. They are the page's choices, mostly the
// user guide's recipes, and none is in the region above (checked here, for
// both plugins, at every tempo the host row offers).
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: a 50 cm ball at 1 rpm, a 5° pin spot 5 m away', {}],
  ['A star in every spot: a 20 cm ball, the lamp 0.5 m away (the guide’s recipe)', { Throw: u(0.5), Beam: u(12), Gobo: 'Star', Colour: 'Amber', Diameter: u(0.2), Bearing: u(36) }],
  ['Spots on the beat: Sync, 15° every beat, gliding over half of it', { Motor: 'Sync' }],
  ['Steps that jump: Sync, 30° every two beats, no Glide', { Motor: 'Sync', 'Step On': '2 Beats', 'Step Angle': u(30), Glide: 0 }],
  ['A worn ball: 25% of the facets missing', { Missing: u(0.25), Seed: 7 }],
  ['Dots in the gate, the lamp 1 m behind the ball: a whole gobo in every spot', { Gobo: 'Dots', Throw: u(1), Beam: u(12), Bearing: u(150) }],
  ['A big hall in amber: 24 m, darker, the lamp low on the left', { Room: u(24), Colour: 'Amber', 'Room Light': 0.01, Elevation: u(-20), Bearing: u(-60) }],
  ['The back wall, square on (SW Mirrorball’s View Wall; the Over always looks there)', { View: 'Wall' }],
  ['Your clip in pieces round the room (SW Mirror Over: Clip As Gobo)', { 'Clip As': 'Gobo' }],
  ['Your whole clip in every spot (SW Mirror Over: Clip As Gobo, the lamp 1 m behind the ball)', { 'Clip As': 'Gobo', Throw: u(1), Beam: u(12), Bearing: u(150) }],
];

/**
 * Settings by the plugin's own names as the kit's values: {units} goes
 * through the plugin's ParamFrom*, an option is its element's name, an integer
 * is itself, a bare number the host's 0..1. (demo/tools/crosscheck.py uses it
 * too.)
 */
function kitValues(variant, spec, what = 'settings') {
  const values = {};
  for (const [name, value] of Object.entries(spec)) {
    const p = PARAMS.find((x) => x.name === name);
    if (!p) throw new Error(`${what}: no control called ${name}`);
    const d = p.ff[variant] ?? p.ff.source ?? p.ff.over;
    if (p.type === 'option') {
      const slot = p.elements.indexOf(value);
      if (slot < 0) throw new Error(`${what}: ${name} has no element ${value}`);
      values[p.id] = slot;
    } else if (d.type === FF_TYPE_INTEGER) {
      if (!Number.isInteger(value) || value < d.min || value > d.max) throw new Error(`${what}: ${name} ${value} is outside ${d.min}–${d.max}`);
      values[p.id] = String(value);
    } else {
      const host = typeof value === 'object' ? paramFrom(p.ff[variant] ? variant : (p.only ?? variant), d, value.units) : value;
      if (!Number.isFinite(host)) throw new Error(`${what}: ${name} has no conversion`);
      values[p.id] = host;
    }
  }
  return values;
}

function kitPresets(variant) {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) {
    const values = kitValues(variant, spec, `preset “${title}”`);
    for (const which of ['source', 'over']) {
      for (const bpm of TEMPOS) {
        const all = { ...DEFAULTS[which], ...values };
        if (flashRegion(which, (id) => all[id], bpm)) throw new Error(`preset “${title}” is where the guide measured flashing`);
      }
    }
    out[title] = values;
  }
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;
let mounted = null;
function currentVariant() { return mounted?.state.variant ?? initialVariant; }

/** The value the host would send for a kit control, or null for none. */
function hostValue(d, v) {
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_INTEGER: {
      const n = Number.parseInt(String(v).trim(), 10);
      if (!Number.isFinite(n)) return null;
      return Math.min(d.max, Math.max(d.min, n));
    }
    default: return Math.fround(v);
  }
}

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf. The
  // kit has enabled EXT_color_buffer_float (needFloat) for the plugin's
  // RGBA16F gobo and RGBA32F ring table, or refused to start.
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);
  core._mb_prepare_log();

  let instance = 0;
  let variant = null;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let instances = 0;
  let lastTime = null;

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(which, width, height) {
    if (instance) core._mb_delete(instance);
    instance = core._mb_new(which === 'over' ? 1 : 0);
    variant = which;
    pushed = new Map();
    frames = 0;
    instances += 1;
    host.lastTime = null;
    if (!core._mb_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`${which === 'over' ? 'SW Mirror Over' : 'SW Mirrorball'} could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const d = p.ff[variant];
      if (!d) continue;
      const value = hostValue(d, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._mb_set_float(instance, d.index, value);
    }
  }

  function process(time, input) {
    const clip = variant === 'over' ? glName(input.texture) : 0;
    if (!core._mb_process(instance, time, clip, input.width, input.height)) {
      throw new GLError('The plugin’s ProcessOpenGL failed.');
    }
  }

  /**
   * Five host frames about a millisecond apart, before the first one shown:
   * enough for the plugin's seconds-or-milliseconds vote (four frames that
   * agree against the wall clock) to settle, so a paused page stands still.
   * The clock then steps back to the page's time, which the plugin takes as
   * no time passing.
   */
  function prime(time, input) {
    const t0 = performance.now();
    for (let k = 0; k < 5; k += 1) {
      if (k > 0) {
        // The plugin needs 0.5 ms of wall clock between votes. Date.now() caps
        // the wait where performance.now() stands still (a test harness's
        // stepped clock).
        const until = performance.now() + 1.05;
        const cap = Date.now() + 20;
        while (performance.now() < until && Date.now() < cap) { /* wait */ }
      }
      core._mb_set_beat(instance, host.bpm, host.phase);
      process(time + (performance.now() - t0) / 1000, input);
    }
  }

  return {
    render({ input, params, width, height, time, variant: wanted }) {
      core.GL.makeContextCurrent(handle);
      // A new instance for the Plugin dropdown, and for Restart (the page's
      // clock going back).
      const fresh = !instance || wanted !== variant || (lastTime !== null && time < lastTime);
      lastTime = time;
      if (fresh) start(wanted ?? 'source', width, height);
      push(params);
      if (fresh) prime(time, input);

      // The host's beat, every frame.
      if (host.lastTime !== null && time > host.lastTime) host.phase = (host.phase + (time - host.lastTime) * host.bpm / 240) % 1;
      host.lastTime = time;
      core._mb_set_beat(instance, host.bpm, host.phase);

      process(time, input);

      // A setter WebGL2 refuses (a float sent to an int, say) fails silently
      // but for this: asked on an instance's first frames only, as a sync
      // point is not free. demo/tools/check_shaders.py checks the setters
      // statically.
      frames += 1;
      if (frames <= 3) {
        const error = gl.getError();
        if (error !== gl.NO_ERROR) {
          gl.canvas.dataset.glError = `0x${error.toString(16)}`;
          console.error(`WebGL reported error 0x${error.toString(16)} on frame ${frames} of ${variant === 'over' ? 'SW Mirror Over' : 'SW Mirrorball'}`);
        }
      }
      // For anything checking the page from outside.
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.instances = String(instances);
      gl.canvas.dataset.psi = String(core._mb_psi(instance));
      gl.canvas.dataset.steps = String(core._mb_steps(instance));
    },
    stats() {
      if (!instance) return null;
      return {
        variant,
        psi: core._mb_psi(instance),
        turns: core._mb_turns(instance),
        steps: core._mb_steps(instance),
        now: core._mb_host_now(instance),
        facets: core._mb_facets(instance),
        rings: core._mb_rings(instance),
        throwUsed: core._mb_throw_used(instance),
        frame: core._mb_frame(instance),
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._mb_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. mirrorball-core.wasm is source/Mirrorball.cpp — the plugin class: its constructor, its clock and seconds-or-milliseconds vote, the gobo and ring-table buffers, every uniform and the GL state it saves and puts back — and everything it calls (Motor, Ball, Controls, PassBuffer, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader, FFGLFBO and screen quad, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The motor — Free’s steady turn and Sync’s steps placed at the instant inside the frame where the beat fell, eased on a raised cosine — and the ball’s rings, the room and the lamp’s place are that C++, in double. Only the two registration files are left out; the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order InitGL and Assemble() join the pieces in, and the repository’s verify script fails if a character differs, or if a uniform is set by a call its GLSL type does not take. At start-up the page also requires each of the four stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults. GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points. The gobo is a mipmapped 16-bit float texture and the ring table a 32-bit float one, as in the plugin, which needs EXT_color_buffer_float; a browser without it is refused rather than handed something smaller.',
  'No audio, and none is missing: Mirrorball has no audio input. Sync follows the host’s beat, not the sound.',
  'The tempo and bar phase are the page’s. Resolume sends its BPM and bar phase every frame; here the tempo is chosen in the host row (120 BPM by default, which is also what the plugin assumes when a host sends none) and the bar phase runs on the page’s clock, in 4/4, continuous across a tempo change, standing still when paused. Sync steps on that. At the defaults Sync would step 15° on every beat, twice a second at 120 BPM. The plugin’s own clock for a host whose transport has stopped (it keeps stepping at the last tempo it was sent) is never used here: while the page plays, its bar phase moves.',
  'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, handed to SetTime. The plugin votes against the wall clock on whether those are seconds or milliseconds, and until four frames agree it turns the ball on the wall clock, its own rule. So that a paused page stands still, each new instance — page load, the Plugin dropdown and Restart — is handed five frames about a millisecond apart before the first one shown, which settles the vote; the ball turns by those few milliseconds (at 5 rpm, under a fifth of a degree), and the clock then steps back to the page’s time, which the plugin takes as no time passing. Restart is a new instance at the panel’s settings, the ball back at its starting phase, rather than a clock sent backwards: a Sync move in flight would hang until the clock came round to its end again. Step advances the clock 1/60 s.',
  'Paused, a moved control redraws the frame without turning the ball.',
  'A frame here is one of the display’s refreshes (60 or 120 a second, and whenever the browser gets round to it), not Resolume’s output frame. The motor does not depend on it — Free’s phase is the same at any frame rate and a Sync step starts on the beat inside the frame it falls in — but which instants are drawn does.',
  'WebAssembly is not the Mac’s arm64. It has no fused multiply-add, and its sin, cos, tan and pow are musl’s, not Apple’s, so the motor’s phase and the ring layout’s doubles can differ from the plugin built for a Mac in the last digits. What reaches the GPU is rounded to float, as in the plugin, and then rasterised by this browser’s GPU, not the Mac’s.',
  'SW Mirror Over’s clip is the kit’s generated one, or your own image or video. The kit hands over premultiplied colour, and the plugin treats its input as straight colour with alpha, so over a half-transparent edge it would count the alpha twice. The page’s own clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them; an image of your own with transparency would show it at its edges. In Arena the effect gets the composition’s own frame; here its whole texture is used, unpadded.',
  'Seed is FF_TYPE_INTEGER in the plugin and a number field here, clamped to its declared range (0–999). The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Switching the Plugin dropdown is a new instance at that constructor’s defaults, as in Resolume. The two declare the same controls but View (the source’s), Clip As and Mix (the Over’s); the Over’s Exposure default is +5/6 stop, the source’s 0.',
  'The photosensitivity note at the top, the caution line under the picture and the rule that a link opening with a small ball, the lamp in close and a fast turn opens paused are the page’s, not the plugin’s; in embed mode (?embed=1, the output as a video source, with no Play button to press) neither the note nor that rule applies. The page’s tempos stop at 174 BPM, so Sync’s steps here are at most 5.8 a second; its guard on more than six is there for links.',
  'Nothing here is measured. The plugin has never been loaded into Resolume, on macOS or Windows; its bundles are read and rendered through by a host-shaped tool, and its Windows DLLs are built and loaded only by CI (its README’s Status says what was seen). Its harness, mbtest, drives the same class headlessly on a synthetic clock and reads the light back off the pixels: a facet’s spot leaves at the angle of incidence to 5.6e-5 rad, the near-field spot is the cone–plane ellipse, a facet’s light over a plane is what it intercepted, the circles on the ceiling run at the motor’s rate, every spot carries the right mirrored piece of the gobo, and 18 wrong models and 27 one-character mutants are caught. That harness, not this page, is the reason to believe the ball.',
];

//===========================================================================
// The page.
//===========================================================================

mounted = mountDemo({
  name: 'Mirrorball',
  pluginId: 'MR01 · MR02',
  kind: ['source', 'effect'],
  tagline:
    'A mirror ball for Resolume, turning in a dark hall full of its spots. Nothing is drawn as a spot: every facet is a flat mirror reflecting one narrow pin spot by the law of reflection, and for each pixel the plugin traces the light back through the few facets that could send it, to the lamp’s lens and its gate. So spots stretch where they land at a slant, crowd and dim behind the ball, crawl near the axis and race toward the horizon, and carry a mirrored piece of the gobo with the lamp far off and a whole one with it in close. SW Mirrorball is the hall; SW Mirror Over lands the same spots on your clip, or makes your clip the pin spot’s gobo.',
  repo: 'https://github.com/stoatworks-labs/mirrorball',

  blurb:
    'It is the plugin’s own C++ — the plugin class, its motor and the ball’s layout in double, and its clock — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2. The host is this page: Sync steps on the page’s tempo, chosen under the picture, and SW Mirror Over runs on a generated clip.',

  // The gobo is RGBA16F and mipmapped, the ring table RGBA32F, as in the plugin.
  needFloat: true,
  // Both plugins write alpha 1 over the kit's opaque clips.
  showBackdrop: false,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Mirrorball (source)', hint: 'MR01, FF_SOURCE: the ball in a dark hall full of its spots. Reads no clip.' },
      { id: 'over', name: 'SW Mirror Over (effect)', hint: 'MR02, FF_EFFECT: the same ball and lamp seen on the back wall, with the clip printed on the wall or as the pin spot’s gobo.' },
    ],
  },

  // For SW Mirror Over only, and all opaque: the kit's 'alpha' clip is left
  // out because the kit's premultiplied colour is not the straight colour the
  // plugin expects (see the notes at the foot).
  sources: ['scene', 'bars', 'grid', 'detail', 'spot', 'ramp'],

  params: PARAMS,
  presets: kitPresets(initialVariant),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// Which controls belong to which plugin, Seed, the open-paused rule, the
// host row and the lines under the picture.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule.
//===========================================================================

if (mounted) {
  const params = mounted.params;
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin and the tempo.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (host.bpm !== 120) q.set('bpm', String(host.bpm));
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(which) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === which);
    const effect = which === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const which = mounted.state.variant;
    if (which !== shownVariant) {
      shownVariant = which;
      // A new instance: that constructor's defaults, for Defaults too.
      params.defaults = { ...DEFAULTS[which] };
      params.reset();
    }
    if (!embed) showForVariant(which);
  });

  // A link that opens where the guide measured flashing opens paused.
  if (!embed && flashRegion(mounted.state.variant, (id) => params.get(id), host.bpm)) {
    mounted.state.playing = false;
    const play = document.querySelector('.btn--play');
    if (play) play.textContent = 'Play';
  }

  if (!embed) {
    showForVariant(shownVariant);

    // Seed as a number field.
    for (const p of PARAMS) {
      const d = p.ff.source ?? p.ff.over;
      if (d.type !== FF_TYPE_INTEGER) continue;
      const field = document.getElementById(`p-${p.id}`);
      if (!field) continue;
      field.type = 'number';
      field.min = String(d.min);
      field.max = String(d.max);
      field.step = '1';
      field.inputMode = 'numeric';
    }

    buildHostRow();
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The host row: the stand-in for what Resolume would hand the plugin.
//---------------------------------------------------------------------------
function field(label, select, title) {
  const wrap = document.createElement('label');
  wrap.className = 'transport__field';
  if (title) wrap.title = title;
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = label;
  // Narrow screens: the field may shrink, and the select with it.
  wrap.style.maxWidth = '100%';
  wrap.style.minWidth = '0';
  select.style.maxWidth = '100%';
  select.style.minWidth = '0';
  wrap.append(span, select);
  return wrap;
}

function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport';
  row.id = 'mirrorball-host';
  const tag = document.createElement('span');
  tag.className = 'transport__label';
  tag.textContent = 'The host:';
  tag.title = 'The stand-in for what Resolume would hand the plugin. Not a plugin control.';

  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  tempo.id = 'mirrorball-tempo';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.bpm = Number(tempo.value);
    mounted.redraw();
  });

  row.append(
    tag,
    field('Tempo', tempo, 'Resolume’s BPM, sent with its bar phase every frame (SetBeatInfo). The bar phase runs at it, in 4/4, on the page’s clock; Sync steps on it.'),
  );
  transport.after(row);
}

//---------------------------------------------------------------------------
// The lines under the picture: the plugin's own state, through the accessors
// its harness reads, and the caution when the panel is where the guide
// measured flashing. They report; they measure nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status mb-status';
  line.id = 'mirrorball-status';
  line.setAttribute('aria-live', 'off');
  const caution = document.createElement('p');
  caution.className = 'stage__status mb-caution';
  caution.id = 'mirrorball-caution';
  caution.setAttribute('aria-live', 'polite');
  caution.hidden = true;
  const log = document.createElement('p');
  log.className = 'stage__status';
  log.id = 'mirrorball-log';
  log.setAttribute('aria-live', 'off');
  log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
  log.style.fontSize = '12px';
  log.style.whiteSpace = 'pre-wrap';
  log.style.overflowWrap = 'anywhere';
  stage.append(line, caution, log);

  const params = mounted.params;
  const byName = (name) => PARAMS.find((p) => p.name === name);
  const units = (name) => {
    const p = byName(name);
    const d = p.ff[currentVariant()] ?? p.ff.source ?? p.ff.over;
    return convert(p.ff[currentVariant()] ? currentVariant() : (p.only ?? currentVariant()), d, params.get(p.id));
  };
  const option = (name) => byName(name).elements[Math.round(params.get(byName(name).id))];
  const sig = (n) => (Math.abs(n) >= 100 ? n.toFixed(0) : Math.abs(n) >= 10 ? n.toFixed(1) : n.toFixed(2));

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    let motor;
    if (option('Motor') === 'Free') {
      const rpm = units('Speed');
      motor = Math.abs(rpm) < 1e-6
        ? 'Free at 0 rpm: standing still'
        : `Free at ${sig(Math.abs(rpm))} rpm${rpm < 0 ? ' backwards' : ''}, a turn every ${sig(60 / Math.abs(rpm))} s`;
    } else {
      const every = option('Step On');
      const perSecond = host.bpm / 60 / BEATS_PER_STEP[every];
      motor = `Sync: ${s.steps} step${s.steps === 1 ? '' : 's'} of ${sig(units('Step Angle'))}° so far, one every ${every.toLowerCase()} at ${host.bpm} BPM (${sig(perSecond)} a second), gliding over ${Math.round(units('Glide') * 100)}% of each`;
    }
    const asked = units('Throw');
    const lamp = Math.abs(s.throwUsed - asked) > 1e-6
      ? `the lamp ${s.throwUsed.toFixed(2)} m from the ball (Throw asks for ${asked.toFixed(2)} m; ${s.throwUsed < asked ? 'the room’s walls stop it there' : 'it stays clear of the ball'})`
      : `the lamp ${s.throwUsed.toFixed(2)} m from the ball`;
    const next = `${s.variant === 'over' ? 'SW Mirror Over' : 'SW Mirrorball'}: ${s.facets.toLocaleString('en-GB')} facets in ${s.rings} rings`
      + ` · ${motor} · turned ${(s.turns * 360).toFixed(1)}° since it started`
      + ` · ${lamp} · the plugin’s clock at ${s.now.toFixed(2)} s, frame ${s.frame}`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }

    const flashing = flashRegion(currentVariant(), (id) => params.get(id), host.bpm);
    caution.hidden = !flashing;
    caution.textContent = flashing
      ? 'These settings are where the user guide measured this plugin flashing past three times a second in part of the picture: a ball of 0.3 m or less, the lamp within 1.5 m, and a fast turn (over 3 rpm in Free; in Sync, any Glide or more than six steps a second). Flashing light can trigger seizures in people with photosensitive epilepsy. Pause stops it at once.'
      : '';

    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) mirrorball: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against mbtest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, DEFAULTS, core, flashRegion, host, hostValue, kitValues };
