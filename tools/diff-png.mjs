// Write a diff image (differing pixels in red over a dimmed copy of A) and
// the bounding box of the differences.  node tools/diff-png.mjs a.png b.png out.png [--tol 2]
import { readPng, writePngRgb } from './png.mjs';
const [pa, pb, po, , tolArg] = process.argv.slice(2);
const tol = tolArg ? +tolArg : 2;
const a = readPng(pa), b = readPng(pb);
const out = new Uint8Array(a.w * a.h * 3);
let x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
for (let i = 0; i < a.w * a.h; i++) {
  let d = 0;
  for (let c = 0; c < 3; c++) d = Math.max(d, Math.abs(a.data[i * a.nch + c] - b.data[i * b.nch + c]));
  const x = i % a.w, y = (i / a.w) | 0;
  if (d > tol) { out.set([255, 0, 0], i * 3); x0 = Math.min(x0, x); y0 = Math.min(y0, y); x1 = Math.max(x1, x); y1 = Math.max(y1, y); }
  else for (let c = 0; c < 3; c++) out[i * 3 + c] = a.data[i * a.nch + c] >> 2;
}
writePngRgb(po, a.w, a.h, out);
console.log(JSON.stringify({ bbox: x1 < 0 ? null : [x0, y0, x1, y1] }));
