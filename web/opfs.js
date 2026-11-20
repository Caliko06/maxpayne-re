// The OWNER's game folder and saves in the browser's private storage (OPFS), so the page needs no game server:
// the folder is picked once (<input webkitdirectory>, all browsers) and copied to OPFS game/; saves live in OPFS save/.
// host/files.c reads them from its worker with synchronous range requests on blob: URLs of the OPFS files
// (RN_FILES, made here at boot); save writes go behind the game to the worker half of this file (sync access handles).
// Nothing leaves the machine: OPFS is this origin's local storage.
'use strict';
const MARK = '.mp-copied';            // written last by a copy: its absence means no game (or a copy cut short)

if (typeof window === 'undefined') {
  // ---- worker: every write to OPFS, one at a time --------------------------------------------------------------------
  const dirOf = async (root, parts, create) => { let d = root; for (const p of parts) d = await d.getDirectoryHandle(p, { create }); return d; };
  const split = rel => rel.split('/').filter(Boolean);
  const writeFile = async (dir, name, src) => {        // src: a Blob (copied in 8 MB slices) or bytes
    const h = await (await dir.getFileHandle(name, { create: true })).createSyncAccessHandle();
    try {
      h.truncate(0);
      if (src instanceof Blob)
        for (let at = 0; at < src.size; at += 8 << 20) h.write(new Uint8Array(await src.slice(at, at + (8 << 20)).arrayBuffer()), { at });
      else h.write(src, { at: 0 });
      h.flush();
    } finally { h.close(); }
  };
  let q = Promise.resolve();
  onmessage = ({ data: m }) => { q = q.then(() => run(m)).catch(e => postMessage({ id: m.id, error: String(e && e.message || e) })); };
  async function run(m) {
    const root = await navigator.storage.getDirectory();
    if (m.op === 'copy') {           // m.files: [[rel, File]]; the old copy goes first
      await root.removeEntry('game', { recursive: true }).catch(() => {});
      const game = await root.getDirectoryHandle('game', { create: true });
      let done = 0;
      for (const [rel, file] of m.files) {
        const p = split(rel);
        await writeFile(await dirOf(game, p.slice(0, -1), true), p[p.length - 1], file);
        done += file.size;
        postMessage({ id: m.id, progress: done });
      }
      await writeFile(game, MARK, new TextEncoder().encode(JSON.stringify({ files: m.files.length, bytes: done, at: new Date().toISOString() })));
    } else {                         // host/files.c save_send: put / mkdir / del under save/
      const save = await root.getDirectoryHandle('save', { create: true }), p = split(m.rel);
      if (m.op === 'mkdir') await dirOf(save, p, true);
      else if (m.op === 'put') await writeFile(await dirOf(save, p.slice(0, -1), true), p[p.length - 1], new Uint8Array(m.data));
      else if (m.op === 'del') await (async () => (await dirOf(save, p.slice(0, -1), false)).removeEntry(p[p.length - 1], { recursive: true }))()
        .catch(e => { if (e.name !== 'NotFoundError') throw e; });      // deleting what is gone: done
    }
    postMessage({ id: m.id, done: true });
  }
} else {
  // ---- page -----------------------------------------------------------------------------------------------------------
  globalThis.MPFiles = (() => {
    let worker, seq = 0;
    const waits = new Map();
    const failAll = why => { for (const w of waits.values()) w.reject(new Error(why)); waits.clear(); worker = null; };   // a dead worker answers nobody
    const call = (m, onProgress) => {
      worker ||= Object.assign(new Worker('opfs.js'), { onerror: e => failAll('opfs worker: ' + (e.message || 'error')), onmessageerror: () => failAll('opfs worker: bad message'), onmessage: ({ data: r }) => {
        const w = waits.get(r.id);
        if (!w) return;
        if (r.progress !== undefined) return w.onProgress?.(r.progress);
        waits.delete(r.id);
        r.error ? w.reject(new Error(r.error)) : w.resolve();
      } });
      const id = ++seq;
      return new Promise((resolve, reject) => { waits.set(id, { resolve, reject, onProgress }); worker.postMessage({ ...m, id }); });
    };
    // Every file under dir: [path, size, blob URL] (directories [path, -1, '']).
    async function walk(dir, pre, out) {
      for await (const [name, h] of dir.entries()) {
        const p = pre + name;
        if (h.kind === 'directory') { out.push([p, -1, '']); await walk(h, p + '/', out); }
        else { const f = await h.getFile(); out.push([p, f.size, URL.createObjectURL(f)]); }
      }
      return out;
    }
    // Phones and tablets play the original 2001 textures (OWNER 2026-11-20, light version): the install's HD pack, data/database
    // (587 files, 1 GB; Part1_Level1 is 466 Mpx of JPEG against 3.9 in retail, decoded by the game's own lifted code), is left out of
    // the listing, and the game reads each of those files from its retail twin inside x_*.ras (all 587 have one).
    const hdPack = p => window.MP_DEVICE !== 'desktop' && /^data\/database(\/|$)/i.test(p);
    const hasFile = async (dir, name) => { try { await dir.getFileHandle(name); return true; } catch (e) { if (e.name !== 'NotFoundError') console.warn('opfs hasFile', name, e); return false; } };
    return {
      // The value of RN_FILES for host/files.c (a blob: URL of {game, save}), or null when OPFS holds no complete game.
      async files() {
        if (!navigator.storage?.getDirectory) return null;
        const root = await navigator.storage.getDirectory();
        if (globalThis.RN_PUBLIC) {
          const response = await fetch('/game-files.json');
          if (!response.ok) throw new Error('game-files.json: HTTP ' + response.status);
          const game = (await response.json()).filter(e => !hdPack(e[0])), save = await root.getDirectoryHandle('save', { create: true });
          // The small files the boot and the first menu read (web/prefetch.json) come whole, 6 at a time, and the guest reads
          // them from blob: URLs in memory. A Range request is one round trip each, and Safari does not answer it from the
          // HTTP cache the way Chrome does (2026-11-19: menu at 23 s in Safari vs 12 s in Chrome, 50 ms per request).
          const { whole } = await (await fetch('/prefetch.json')).json(), blobs = new Map();
          let at = 0;
          await Promise.all(Array.from({ length: 6 }, async () => {
            while (at < whole.length) {
              const p = whole[at++], r = hdPack(p.slice('/game/'.length)) ? null : await fetch(p).catch(() => null);
              if (r?.ok) blobs.set(p.slice('/game/'.length), URL.createObjectURL(await r.blob()));
            }
          }));
          for (const e of game) if (blobs.has(e[0])) e[2] = blobs.get(e[0]);
          return URL.createObjectURL(new Blob([JSON.stringify({ game, save: await walk(save, '', []) })], { type: 'application/json' }));
        }
        const game = await root.getDirectoryHandle('game').catch(() => null);
        if (!game || !await hasFile(game, MARK)) return null;
        const save = await root.getDirectoryHandle('save', { create: true });
        const g = (await walk(game, '', [])).filter(([p, n]) => n >= 0 && p !== MARK && !hdPack(p));
        const s = await walk(save, '', []);
        // Files tools/serve.py adds to the game folder (a_cheats.ras, tools/ras.py): the game finds them next to its own.
        const extra = await fetch('/extra-files.json').then(r => r.ok ? r.json() : []).catch(() => []);
        for (const [name, size] of extra) {
          const r = await fetch('/game/' + name).catch(() => null);
          if (r?.ok) g.push([name, size, URL.createObjectURL(await r.blob())]);
        }
        // The game takes the first *.RAS the listing names as the first archive searched (rea/map/ras_overlay.md), and OPFS lists in
        // its own order: sort by path as tools/serve.py does, so a_cheats.ras comes before x_data.ras.
        g.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
        return URL.createObjectURL(new Blob([JSON.stringify({ game: g, save: s })], { type: 'application/json' }));
      },
      // Asks for the game folder in el (a button and an <input webkitdirectory>), copies it to OPFS, resolves when done.
      pick(el) {
        return new Promise(resolve => {
          el.innerHTML = 'Pick the folder where Max Payne is installed (the one with <b>MaxPayne.exe</b>).<br>' +
            'It is copied once to this browser (about 2 GB) and never leaves your device.<br><br>' +
            '<button id="mp-pick">Choose folder…</button> <span id="mp-pickmsg"></span>' +
            '<input id="mp-dir" type="file" webkitdirectory multiple hidden>';
          const input = el.querySelector('#mp-dir'), msg = el.querySelector('#mp-pickmsg');
          el.querySelector('#mp-pick').onclick = () => input.click();
          input.onchange = async () => {
            // webkitRelativePath is "<folder>/<path>": the picked folder itself is dropped.
            const files = [...input.files].map(f => [f.webkitRelativePath.split('/').slice(1).join('/'), f]).filter(([p]) => p);
            const top = new Set(files.filter(([p]) => !p.includes('/')).map(([p]) => p.toLowerCase()));
            if (!top.has('maxpayne.exe') || !top.has('x_data.ras')) { msg.textContent = 'that folder does not have MaxPayne.exe and x_data.ras'; return; }
            el.querySelector('#mp-pick').disabled = true;
            const total = files.reduce((a, [, f]) => a + f.size, 0), gb = n => (n / 2 ** 30).toFixed(2);
            await navigator.storage.persist?.().catch(() => {});
            try {
              await call({ op: 'copy', files }, done => { msg.textContent = `copying ${gb(done)} / ${gb(total)} GB`; });
              resolve();
            } catch (e) { msg.textContent = 'could not copy: ' + e.message; el.querySelector('#mp-pick').disabled = false; }
          };
        });
      },
      // host/files.c save_send: method PUT (body: bytes) / PUT of a directory / DELETE, of a path under save/.
      save(method, rel, dir, body) {
        const op = method === 'DELETE' ? 'del' : dir ? 'mkdir' : 'put';
        let data = body;
        if (ArrayBuffer.isView(body)) {   // only this view's bytes, never the whole backing buffer (subarray / SharedArrayBuffer)
          data = new ArrayBuffer(body.byteLength);
          new Uint8Array(data).set(new Uint8Array(body.buffer, body.byteOffset, body.byteLength));
        }
        return call({ op, rel, data });
      },
    };
  })();
}
