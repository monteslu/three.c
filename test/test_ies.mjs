// t3_ies_texture_parse (src/loaders.c) against three.js r187's IESLoader, texel for texel.
//   node test/test_ies.mjs build/test/test_ies
// Three profiles made for the test: one horizontal angle (one row), 0-180 degrees (mirrored to the circle)
// and 0-270 (the loader adds the 360 entry the file leaves out), the last with commas and TILT=INCLUDE.
// The control compares one profile's C texture with another's three.js texture: it must differ.
import { writeFileSync, readFileSync, mkdirSync, existsSync } from 'node:fs';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { spawnSync } from 'node:child_process';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const bin = resolve(process.argv[2] ?? join(ROOT, 'build', 'test', 'test_ies'));
const PKG = join(ROOT, 'build', 'three-r187', 'package');
if (!existsSync(join(PKG, 'build', 'three.webgpu.js'))) { console.log('skip  test_ies (three.js r187 not fetched: node tools/gen-programs.mjs --states basic)'); process.exit(0); }
globalThis.self ??= globalThis;
const { IESLoader } = await import(pathToFileURL(join(PKG, 'examples', 'jsm', 'loaders', 'IESLoader.js')).href);
const FILES = {
  ring: 'IESNA:LM-63-2002\n[TEST] ring\nTILT=NONE\n1 1000 1 10 1 1 2 0 0 0\n1 1 100\n0 5 10 15 20 25 30 40 60 90\n0\n300 320 500 900 1000 700 260 80 10 0\n',
  lobes: 'IESNA:LM-63-2002\n[TEST] lobes\nTILT=NONE\n1 1000 2 7 5 1 2 0 0 0\n1 0.5 100\n0 10 20 30 45 60 90\n0 45 90 135 180\n1000 950 800 500 200 40 0\n700 700 650 600 420 200 0\n300 320 380 420 380 160 0\n120 110 100 80 40 10 0\n60 50 30 10 0 0 0\n',
  wide: 'IESNA91\n[TEST] wide\nTILT=INCLUDE\n1\n3\n0 45 90\n1 0.9 0.8\n1, 1200, 1.5, 6, 4, 1, 1, 0.1, 0.1, 0\n1.0 1.0 55\n0 30 60 90 120 180\n0 90 180 270\n10 400 900 300 50 5\n20 500 700 200 40 5\n900 100 50 20 10 5\n30 300 600 500 100 50\n',
};
const dir = join(ROOT, 'build', 'test', 'ies');
mkdirSync(dir, { recursive: true });
const loader = new IESLoader();
const out = {};
let fails = 0;
for (const [name, text] of Object.entries(FILES)) {
  const src = join(dir, name + '.ies'), dst = join(dir, name + '.bin');
  writeFileSync(src, text);
  const r = spawnSync(bin, [src, dst], { encoding: 'utf8' });
  if (r.status !== 0) { console.log(`FAIL ${name}: ${r.stderr}`); fails++; continue; }
  const b = readFileSync(dst);
  const tex = loader.parse(text);
  out[name] = { c: new Uint16Array(b.buffer, b.byteOffset + 8, (b.length - 8) / 2), w: b.readUInt32LE(0), h: b.readUInt32LE(4), js: tex.image.data, jw: tex.image.width, jh: tex.image.height };
}
const differ = (a, b) => { if (a.length !== b.length) return a.length + b.length; let n = 0; for (let i = 0; i < a.length; i++) n += a[i] !== b[i]; return n; };
for (const [name, o] of Object.entries(out)) {
  const n = o.w !== o.jw || o.h !== o.jh ? -1 : differ(o.c, o.js);
  if (n) { console.log(`FAIL ${name}: ${n < 0 ? `size ${o.w}x${o.h}, three.js ${o.jw}x${o.jh}` : `${n} of ${o.c.length} texels differ from IESLoader`}`); fails++; }
  else console.log(`ok   ${name}: ${o.w}x${o.h}, every texel equals IESLoader's`);
}
if (out.lobes && out.wide) {
  if (!differ(out.lobes.c, out.wide.js)) { console.log('FAIL control: two different profiles compare equal'); fails++; }
  else console.log('ok   control: another profile differs');
}
if (out.ring && out.ring.h !== 1) { console.log('FAIL ring: a single horizontal angle should give one row'); fails++; }
process.exit(fails ? 1 : 0);
