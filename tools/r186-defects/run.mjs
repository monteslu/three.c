// Four three.js r186 defects, reproduced with three.js alone (no three.c) in
// headless Chromium, each next to the control that shows the correct result:
// the object alone in a fresh renderer. three.c draws what the controls draw.
//
//   node tools/r186-defects/run.mjs [page,...]
//
// r186 comes from build/three-r186 (tools/gen-programs.mjs downloads it);
// THREE_DEV=<a three.js checkout with build/> adds a column for that build.
import { createServer } from 'node:http';
import { readFileSync, existsSync } from 'node:fs';
import { join, extname, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { pkgEntry } from '../deps.mjs';

const HERE = dirname(fileURLToPath(import.meta.url)), ROOT = resolve(HERE, '..', '..');
const { chromium } = await import(pkgEntry('playwright'));
const TREES = { r186: join(ROOT, 'build', 'three-r186', 'package') };
if (process.env.THREE_DEV) TREES.dev = resolve(process.env.THREE_DEV);
const MIME = { '.js': 'text/javascript', '.html': 'text/html' };
const server = createServer((q, s) => {
  const p = decodeURIComponent(q.url.split('?')[0]);
  const v = Object.keys(TREES).find((k) => p.startsWith(`/${k}/`));
  const f = v ? join(TREES[v], p.slice(v.length + 2)) : join(HERE, p);
  if (!existsSync(f)) { s.writeHead(404); return s.end(); }
  s.writeHead(200, { 'content-type': MIME[extname(f)] || 'application/octet-stream' });
  s.end(readFileSync(f));
}).listen(0);
const port = server.address().port;
const CASES = {
  sprite: ['alone', 'withA'],
  phong: ['mixAlone', 'addAlone', 'together'],
  envmap: ['redAlone', 'blueAlone', 'together', 'pmremRedAlone', 'pmremBlueAlone', 'pmremTogether'],
  depthmap: ['greenAlone', 'depthFirst', 'greenFirst'],
};
for (const backend of ['webgl', 'webgpu']) {
  const browser = await chromium.launch({ args: backend === 'webgpu' ? ['--enable-unsafe-webgpu'] : ['--enable-gpu', '--ignore-gpu-blocklist', '--use-angle=gl'] });
  for (const page of (process.argv[2] || Object.keys(CASES).join(',')).split(','))
    for (const v of Object.keys(TREES)) for (const c of CASES[page]) {
      const pg = await browser.newPage();   // a fresh renderer per case: r186 caches programs across scenes
      const errs = [];
      pg.on('pageerror', (e) => errs.push(String(e)));
      pg.on('console', (m) => m.type() === 'error' && errs.push(m.text()));
      await pg.goto(`http://127.0.0.1:${port}/${page}.html?v=${v}&b=${backend}&c=${c}`);
      const r = await pg.waitForFunction(() => window.__result, null, { timeout: 30000 }).then((h) => h.jsonValue()).catch((e) => ({ error: String(e).slice(0, 200) }));
      delete r.ver; delete r.backend;
      if (errs.length) r.errors = [...new Set(errs)].slice(0, 2).map((e) => e.slice(0, 160));
      console.log(page.padEnd(9), backend.padEnd(7), v.padEnd(5), JSON.stringify(r));
      await pg.close();
    }
  await browser.close();
}
server.close();
