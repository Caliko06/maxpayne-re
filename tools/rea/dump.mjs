// Decompile every Ghidra procedure of one binary into files, in one REA session.
// usage: node tools/rea/dump.mjs <binary> <outdir>
//   <outdir>/<addr>.c   pseudocode (skipped when it exists: a rerun resumes)
//   <outdir>/index.tsv  addr, name, status, lines  (rewritten at the end)
// Run it on the Pod under nohup; query the files instead of opening Ghidra.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { existsSync, mkdirSync, writeFileSync, readFileSync } from "node:fs";
const here = new URL(".", import.meta.url).pathname;
const [bin, out] = process.argv.slice(2);
const CHUNK = Number(process.env.REA_CHUNK ?? 50);
mkdirSync(out, { recursive: true });
const t = new StdioClientTransport({ command: "node", args: [here + "node_modules/rea-agents/scripts/rea.mjs", "--mcp"],
  env: process.env, stderr: "ignore" });
const c = new Client({ name: "mp-dump", version: "1" });
await c.connect(t);
const opt = { timeout: 3600000 };
const call = async (name, args) => {
  const r = await c.callTool({ name, arguments: args }, undefined, opt);
  const sc = r.structuredContent;
  return { err: r.isError, v: sc?.normalized_result ?? sc?.raw_result ?? sc?.result ?? sc };
};
const log = m => console.log(`${new Date().toISOString().slice(11, 19)} ${bin.split("/").pop()}: ${m}`);
const t0 = Date.now();
let r = await call("open_binary", { path: bin, provider_id: "ghidra" });
if (r.err) { log("open failed " + String(JSON.stringify(r.v ?? r)).slice(0, 400)); process.exit(1); }
log(`opened in ${((Date.now() - t0) / 1000).toFixed(0)} s`);
r = await call("list_procedures", {});
const all = (Array.isArray(r.v) ? r.v : r.v?.items ?? r.v?.value ?? []).filter(p => /^0x/.test(p.address));
log(`${all.length} procedures`);
const index = new Map(all.map(p => [p.address, { name: p.value ?? p.name ?? "", status: "", lines: 0 }]));
const todo = all.map(p => p.address).filter(a => !existsSync(`${out}/${a}.c`));
for (const a of all.map(p => p.address)) if (existsSync(`${out}/${a}.c`)) index.get(a).status = "ok";
let done = 0;
for (let k = 0; k < todo.length; k += CHUNK) {
  const part = todo.slice(k, k + CHUNK);
  r = await call("batch_decompile", { addresses: part });
  for (const it of r.v?.items ?? []) {
    const e = index.get(it.address);
    if (!e) continue;
    e.status = it.status;
    if (it.pseudocode) { writeFileSync(`${out}/${it.address}.c`, it.pseudocode); e.lines = it.pseudocode.split("\n").length; }
    else writeFileSync(`${out}/${it.address}.err`, JSON.stringify(it).slice(0, 4000));
  }
  done += part.length;
  if ((k / CHUNK) % 20 === 0) log(`${done}/${todo.length} (${((Date.now() - t0) / 60000).toFixed(1)} min)`);
}
writeFileSync(`${out}/index.tsv`, [...index].map(([a, e]) => `${a}\t${e.name}\t${e.status}\t${e.lines}`).join("\n") + "\n");
log(`DONE ${all.length} procedures in ${((Date.now() - t0) / 60000).toFixed(1)} min`);
await c.close();
process.exit(0);
