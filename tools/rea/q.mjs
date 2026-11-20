// Run a batch of REA MCP calls in ONE Ghidra session. usage: node q.mjs calls.json  ([{"tool":..,"args":{..},"max":N}])
// The batch opens its binary itself (first call open_binary). Runs on the Pod: tools/rea/setup.sh.
// On the Mac (Hopper: provider_id "hopper"), once per checkout: (cd tools/rea && npm i --silent); node_modules stays out of git.
import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { readFileSync } from "node:fs";
const here = new URL(".", import.meta.url).pathname;
const calls = JSON.parse(readFileSync(process.argv[2], "utf8"));
const t = new StdioClientTransport({ command: "node", args: [here + "node_modules/rea-agents/scripts/rea.mjs", "--mcp"],
  env: process.env, stderr: "ignore" });
const c = new Client({ name: "mp-rea", version: "1" });
await c.connect(t);
const opt = { timeout: 1800000 };
for (const k of calls) {
  if (k.tool === "list") { const l = await c.listTools(); for (const x of l.tools) console.log(x.name, JSON.stringify(x.inputSchema?.properties ?? {}).slice(0, k.width ?? 160)); continue; }
  const r = await c.callTool({ name: k.tool, arguments: k.args ?? {} }, undefined, opt);
  const sc = r.structuredContent; const out = sc?.raw_result ?? sc?.result ?? sc ?? r.content?.map(x => x.text).join("\n");
  console.log(`=== ${k.tool} ${JSON.stringify(k.args ?? {})}`);
  console.log(typeof out === "string" ? out : JSON.stringify(out, null, k.pretty ? 1 : 0).slice(0, k.max ?? 20000));
}
await c.close();
