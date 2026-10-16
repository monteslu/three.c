// A texture an extension samples at two places gets two flipY flags (GL),
// and the capture records both: ext.twiceMap.flipY and ext.twiceMap.flipY#1
// at different offsets. Before, the second flag was left out of the property
// map, became a constant 0, and that sample read a render target's texture
// upside down. Runs on the mock GPU (no device).
import { spawnSync } from 'node:child_process';
import { mkdirSync, readFileSync, rmSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const out = join(ROOT, 'build', 'test', 'flip-copies');
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });
const r = spawnSync(process.execPath, [join(ROOT, 'tools', 'gen-programs.mjs'), '--states', 'basic+xtwice', '--backend', 'gl', '--out', out,
  '--mock-gpu', join(ROOT, 'tools', 'gpu-answers.json'), '--extend', join(ROOT, 'test', 'extensions', 'twice.mjs')], { encoding: 'utf8' });
if (r.status !== 0) { console.error(r.stdout + r.stderr); process.exit(1); }
const json = JSON.parse(readFileSync(join(out, 'basic+xtwice.gl.json'), 'utf8'));
const p = json.properties;
const a = p['ext.twiceMap.flipY'], b = p['ext.twiceMap.flipY#1'];
const frag = readFileSync(join(out, 'basic+xtwice.gl.frag'), 'utf8');
const flags = (frag.match(/bool\( nodeUniform\d+ \)/g) || []).length;
let fail = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fail = 1; };
check(flags === 2, `two flipY flags in the fragment shader (${flags})`);
check(!!a && !!b, 'both flags are in the property map');
check(!!a && !!b && (a.group !== b.group || a.byteOffset !== b.byteOffset), 'at different places');
process.exit(fail);
