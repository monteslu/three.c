// A project's own table of r186 programs: the states it draws, captured from
// three.js (tools/gen-programs.mjs) and emitted as C (tools/emit-programs.mjs)
// under the project's name, for t3_register_program_table. Captures are
// cached, so a rerun only captures the states it has not seen.
//
//   node tools/gen-project-table.mjs --name mygame --out <dir> --states <file> [--missing <log> ...]
//        [--cache <dir>] [--check]
//
//   --states   the project's state list (one per line, # comments); created if absent
//   --missing  logs of states a run could not draw (T3_MISSING_LOG=<file> on a
//              native build, or t3_renderer_generated_missing): merged into
//              --states, so the list only grows
//   --cache    where captures live (default <out>/.t3-captures)
//   --check    capture each new state twice and fail if anything differs
//   --real-gpu capture on webgl-node / webgpu-node devices; by default the
//              capture runs on the mock GPU (tools/mock-gpu.mjs answering from
//              tools/gpu-answers.json), which gives the same bytes with no GPU
//   --minimize rewrite --states without the states another listed state covers
//              (the renderer's rule: same kind, features and shadow casters,
//              at least as many lights of each type; the extra lights draw
//              inert), so adding a bigger light vector folds the smaller ones
//
// Then build the two files it writes (<out>/<name>_gl.c, <name>_wgpu.c) with
// three.c's src/ on the include path, call
//   t3_register_program_table(&mygame_gl, &mygame_wgpu);
// before the first render, and optionally build three.c with T3_TABLE=core.
import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const one = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const all = (k) => argv.flatMap((a, i) => (a === k ? [argv[i + 1]] : []));
const NAME = one('--name'), OUT = one('--out') && resolve(one('--out')), STATES = one('--states') && resolve(one('--states'));
if (!NAME || !OUT || !STATES) {
  console.error('usage: gen-project-table.mjs --name <c identifier> --out <dir> --states <file> [--missing <log> ...] [--cache <dir>] [--check] [--minimize] [--real-gpu]');
  process.exit(2);
}
if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(NAME)) throw new Error(`--name ${NAME}: not a C identifier`);
const CACHE = resolve(one('--cache') ?? join(OUT, '.t3-captures'));
mkdirSync(OUT, { recursive: true });
mkdirSync(CACHE, { recursive: true });

const read = (f) => (existsSync(f) ? readFileSync(f, 'utf8').split('\n').map((l) => l.replace(/#.*/, '').trim()).filter(Boolean) : []);
const listed = read(STATES);
const fromLogs = all('--missing').flatMap((f) => read(resolve(f)));
const added = [...new Set(fromLogs)].filter((s) => !listed.includes(s));
if (added.length) {
  const text = existsSync(STATES) ? readFileSync(STATES, 'utf8').replace(/\n?$/, '\n') : '# r186 program states this project draws (tools/gen-project-table.mjs)\n';
  writeFileSync(STATES, text + added.join('\n') + '\n');
  console.log(`${STATES}: +${added.length} from the missing logs`);
}
let states = [...new Set([...listed, ...added])];
if (!states.length) throw new Error(`${STATES}: no states`);

// a state name: kind+features+lDPSH+sDPS+z<n>+x<ext> (tools/gen-programs.mjs parseState)
function parse(st) {
  const [kind, ...toks] = st.split('+');
  let lights = null, cast = null;
  const rest = [];
  for (const t of toks) {
    if (/^l\d{4}$/.test(t)) lights = [...t.slice(1)].map(Number);
    else if (/^s\d{3}$/.test(t)) cast = [...t.slice(1)].map(Number);
    else rest.push(t);
  }
  return { kind, key: rest.sort().join('+'), lights, cast };
}
function covers(a, b) {   // does state a's program serve state b's draws?
  if (a.kind !== b.kind || a.key !== b.key || !a.lights || !b.lights) return false;
  const shadow = b.key.split('+').includes('shadow');
  if (shadow && String(a.cast ?? [a.lights[0], a.lights[1], a.lights[2]]) !== String(b.cast ?? [b.lights[0], b.lights[1], b.lights[2]])) return false;
  return a.lights.every((n, k) => n >= b.lights[k]);
}
if (argv.includes('--minimize')) {
  const ps = states.map((st) => ({ st, p: parse(st) }));
  const keep = ps.filter(({ st, p }) => !ps.some((o) => o.st !== st && covers(o.p, p) && !(covers(p, o.p) && o.st > st)));
  const dropped = ps.filter((x) => !keep.includes(x)).map((x) => x.st);
  if (dropped.length) {
    const lines = readFileSync(STATES, 'utf8').split('\n').filter((l) => !dropped.includes(l.replace(/#.*/, '').trim()));
    writeFileSync(STATES, lines.join('\n'));
    console.log(`${STATES}: -${dropped.length} covered by other states (${dropped.join(' ')})`);
  }
  states = keep.map((x) => x.st);
}

// capture what the cache lacks (both backends, the same three.js as three.c's own tables)
const have = (s) => existsSync(join(CACHE, `${s}.gl.json`)) && existsSync(join(CACHE, `${s}.wgpu.json`));
const todo = states.filter((s) => !have(s));
const gen = join(ROOT, 'tools', 'gen-programs.mjs');
if (todo.length) {
  console.log(`capturing ${todo.length} state(s) into ${CACHE}`);
  for (const pass of argv.includes('--check') ? ['', '--check'] : ['']) {
    const gpu = argv.includes('--real-gpu') ? [] : ['--mock-gpu', join(ROOT, 'tools', 'gpu-answers.json')];
    const r = spawnSync(process.execPath, [gen, '--states', todo.join(','), '--out', CACHE, ...gpu, ...(pass ? [pass] : [])], { stdio: 'inherit' });
    if (r.status !== 0) process.exit(r.status ?? 1);
  }
}
const r = spawnSync(process.execPath, [join(ROOT, 'tools', 'emit-programs.mjs'), '--in', CACHE, '--states', states.join(','), '--table', NAME, '--out', OUT], { stdio: 'inherit' });
process.exit(r.status ?? 1);
