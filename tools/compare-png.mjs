// Compare two PNGs of the same size: pixels differing by more than a
// tolerance in any channel, the largest channel difference, and where.
//   node tools/compare-png.mjs a.png b.png [--tol 2]
import { readPng } from './png.mjs';
const args = process.argv.slice(2);
const tolIdx = args.indexOf('--tol');
const tol = tolIdx >= 0 ? +args.splice(tolIdx, 2)[1] : 2;
const [a, b] = args.map(readPng);
if (a.w !== b.w || a.h !== b.h) { console.log(`size differs: ${a.w}x${a.h} vs ${b.w}x${b.h}`); process.exit(2); }
const nch = Math.min(a.nch, b.nch, 3);
let diff = 0, max = 0, first = null, sumA = 0, sumB = 0;
for (let i = 0; i < a.w * a.h; i++) {
  let worst = 0;
  for (let c = 0; c < nch; c++) {
    const va = a.data[i * a.nch + c], vb = b.data[i * b.nch + c];
    sumA += va; sumB += vb;
    worst = Math.max(worst, Math.abs(va - vb));
  }
  if (worst > max) max = worst;
  if (worst > tol) { diff++; first ??= [i % a.w, (i / a.w) | 0]; }
}
console.log(JSON.stringify({ pixels: a.w * a.h, differ: diff, tol, maxChannelDiff: max, first, meanA: +(sumA / (a.w * a.h * nch)).toFixed(2), meanB: +(sumB / (a.w * a.h * nch)).toFixed(2) }));
process.exit(diff ? 1 : 0);
