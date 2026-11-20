// The level's walkable graph as the ghost uses it (rea/map/ai.md: navigation, rooms). Positions of characters, triggers and
// AI movement nodes are in the frame of their room. Rooms have no placement of their own; each exit (portal) maps its owner
// room A's frame to its destination B's: p_B = p_A*R + t (0x646020; R is identity in all but 2 exits of the game). A BFS
// over the exits gives every room an offset into one frame: world = local + off[room], off[B] = off[A] - t.
// The AI network alone is split where the AI does not walk (doors, the start room), so each portal becomes a node of its
// own, linked to the nearest AI nodes on both sides, and routes are shortest by distance (Dijkstra).

export function buildGraph(nodes, rooms = [], portals = [], links = []) {
  const idxOf = new Map(rooms.map(r => [r.ptr, r.idx]));
  const ptrOf = new Map(rooms.map(r => [r.idx, r.ptr]));
  const off = new Map();
  const out = new Map();
  for (const p of portals) if (p.a >= 0 && p.b >= 0) { if (!out.has(p.a)) out.set(p.a, []); out.get(p.a).push(p); }
  for (const root of [...ptrOf.keys()]) {
    if (off.has(root)) continue;
    off.set(root, [0, 0, 0]);
    const q = [root];
    while (q.length) { const a = q.shift();
      for (const p of out.get(a) || []) if (!off.has(p.b)) { const o = off.get(a); off.set(p.b, o.map((v, i) => v - p.t[i])); q.push(p.b); } }
  }
  const T = new Map([...off].map(([idx, o]) => [ptrOf.get(idx), o]));
  // Without room data (older dumps): offsets from the AI links that cross rooms, as before.
  if (!rooms.length) linkOffsets(nodes, T);
  const world = (room, p) => { const t = T.get(room) || [0, 0, 0]; return p.map((v, i) => v + t[i]); };
  const byId = new Map(), byRoom = new Map(), adj = new Map();
  const put = n => { byId.set(n.id, n); for (const r of n.rooms) { if (!byRoom.has(r)) byRoom.set(r, []); byRoom.get(r).push(n); } adj.set(n.id, new Set()); };
  const link = (a, b) => { adj.get(a).add(b); adj.get(b).add(a); };
  for (const n of nodes) { n.w = world(n.room, n.pos); n.rooms = [n.room]; put(n); }
  for (const n of nodes) for (const id of n.nb) if (id > 0 && byId.has(id)) adj.get(n.id).add(id);
  // Portal nodes: one per exit, at its polygon centre; linked to the 2 nearest AI nodes of each side and to the other
  // portals of a room with no AI nodes (the player's start room in part1_level1 has none).
  const pnodes = [];
  portals.forEach((p, i) => {
    const ra = ptrOf.get(p.a), rb = ptrOf.get(p.b);
    if (ra === undefined || rb === undefined || !T.has(ra)) return;
    const n = { id: 1e6 + i, kind: 'portal', room: ra, rooms: [ra, rb], pos: p.c, w: world(ra, p.c), nb: [] };
    put(n); pnodes.push(n);
  });
  for (const n of pnodes) for (const r of n.rooms) {
    const ai = (byRoom.get(r) || []).filter(m => m.kind !== 'portal').sort((x, y) => dist2(x.w, n.w) - dist2(y.w, n.w));
    for (const m of ai.slice(0, 2)) link(n.id, m.id);
    if (!ai.length) for (const m of byRoom.get(r)) if (m !== n) link(n.id, m.id);
  }
  // Drops: the AI network has no link where only the player goes, e.g. from the platform down onto the tracks
  // (part1_level1: the service train trigger sits on a 379-node piece of ::p2/::p2_tunnel). Inside one room, the closest
  // pair of nodes of two different pieces within 2.5 units across and 4 up/down is linked; a link Max cannot walk ends in
  // the driver's stuck -> blocked rule.
  const und = new Map([...adj.keys()].map(id => [id, new Set(adj.get(id))]));
  for (const [id, s] of adj) for (const nb of s) und.get(nb).add(id);
  const comp = new Map();
  for (const n of byId.values()) { if (comp.has(n.id)) continue; comp.set(n.id, n.id); const q = [n.id];
    while (q.length) for (const nb of und.get(q.pop())) if (!comp.has(nb)) { comp.set(nb, n.id); q.push(nb); } }
  let drops = 0;
  const dropSet = new Set();
  for (const list of byRoom.values()) {
    const best = new Map();
    for (let i = 0; i < list.length; i++) for (let j = i + 1; j < list.length; j++) {
      const a = list[i], b = list[j], ca = comp.get(a.id), cb = comp.get(b.id);
      if (ca === cb) continue;
      const dx = (a.w[0] - b.w[0]) ** 2 + (a.w[2] - b.w[2]) ** 2, dy = Math.abs(a.w[1] - b.w[1]);
      if (dx > 2.5 ** 2 || dy > 4) continue;
      const k = ca < cb ? `${ca}:${cb}` : `${cb}:${ca}`, d = dx + dy * dy;
      if (!best.has(k) || d < best.get(k).d) best.set(k, { a, b, d });
    }
    // A step of 0.9 or more is a drop one way and a climb back the other (part1_level1 p2: from the tracks at y -1.3 the
    // platform is only reachable by the climb; ::p5 2706/2707 is a ramp Max walks both ways). Both edges cost 500 (path()),
    // so the router uses them only when nothing else leads there, and can still return.
    for (const { a, b } of best.values()) {
      link(a.id, b.id);
      dropSet.add(`${a.id}:${b.id}`); dropSet.add(`${b.id}:${a.id}`); drops++; }
  }
  // Extra links from the level's table (exits.json `links`: pairs of AI node ids across a hole in the network, each with its source).
  for (const [a, b] of links) if (byId.has(a) && byId.has(b)) link(a, b);
  return { nodes: [...byId.values()], byId, byRoom, adj, T, world, rooms: rooms.length, portals: pnodes.length, drops, dropSet, blocked: new Set(), blockedEdges: new Set() };
}

function linkOffsets(nodes, T) {
  const byId = new Map(nodes.map(n => [n.id, n])), pair = new Map(), rooms = new Set(nodes.map(n => n.room));
  for (const a of nodes) for (const id of a.nb) { const b = byId.get(id);
    if (!b || b.room === a.room) continue;
    const k = `${a.room}>${b.room}`, d = pair.get(k) || { ra: a.room, rb: b.room, s: [0, 0, 0], n: 0 };
    for (let i = 0; i < 3; i++) d.s[i] += a.pos[i] - b.pos[i];
    d.n++; pair.set(k, d); }
  const adj = new Map();
  for (const d of pair.values()) { if (!adj.has(d.ra)) adj.set(d.ra, []); adj.get(d.ra).push(d); }
  for (const root of rooms) { if (T.has(root)) continue; T.set(root, [0, 0, 0]); const q = [root];
    while (q.length) { const r = q.shift();
      for (const d of adj.get(r) || []) if (!T.has(d.rb)) { const t = T.get(r); T.set(d.rb, t.map((v, i) => v + d.s[i] / d.n)); q.push(d.rb); } } }
}

const dist2 = (a, b) => (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2;

// Nearest node to a world point: within one room when it is known (as 0x438100 -> 0x444f50 does), else among all nodes.
export function nearest(g, w, room) {
  let best = null, bd = Infinity;
  for (const n of (room && g.byRoom.get(room)) || g.nodes) { const d = dist2(n.w, w); if (d < bd) { bd = d; best = n; } }
  return best;
}

// Shortest path by distance over the AI links and the portal links, around the nodes the player could not reach
// (g.blocked: closed gates and doors the AI network passes through).
export function path(g, from, to) {
  const dist = new Map([[from.id, 0]]), prev = new Map([[from.id, null]]), done = new Set();
  const heap = [[0, from.id]];
  const up = i => { while (i > 0) { const p = (i - 1) >> 1; if (heap[p][0] <= heap[i][0]) break; [heap[p], heap[i]] = [heap[i], heap[p]]; i = p; } };
  const pop = () => { const top = heap[0], last = heap.pop();
    if (heap.length) { heap[0] = last; let i = 0;
      for (;;) { const l = 2 * i + 1, r = l + 1; let m = i;
        if (l < heap.length && heap[l][0] < heap[m][0]) m = l; if (r < heap.length && heap[r][0] < heap[m][0]) m = r;
        if (m === i) break; [heap[m], heap[i]] = [heap[i], heap[m]]; i = m; } }
    return top; };
  while (heap.length) {
    const [d, id] = pop();
    if (done.has(id)) continue; done.add(id);
    if (id === to.id) break;
    const a = g.byId.get(id);
    for (const nb of g.adj.get(id)) { if (done.has(nb) || (g.blocked.has(nb) && nb !== to.id) || g.blockedEdges?.has(`${id}:${nb}`)) continue;
      // A drop costs 500 units: taken only when nothing else leads there (part1_level1: the start platform's drop onto
      // the tracks of the start train killed Max four times in a row).
      const nd = d + Math.sqrt(dist2(a.w, g.byId.get(nb).w)) + (g.dropSet?.has(`${id}:${nb}`) ? 500 : 0);
      if (nd < (dist.get(nb) ?? Infinity)) { dist.set(nb, nd); prev.set(nb, id); heap.push([nd, nb]); up(heap.length - 1); } }
  }
  if (!prev.has(to.id)) return null;
  const out = [];
  for (let id = to.id; id !== null; id = prev.get(id)) out.push(g.byId.get(id));
  return out.reverse();
}

// A route that begins by dropping >= 0.9 while the goal sits above the landing: the way back would need the climb edge, and the
// first drop is the one a player cannot undo (part1_level1: the platform above the tracks). The caller treats it as no route.
export function startsWithDrop(g, route, goal) {
  if (!route || route.length < 2) return false;
  const [a, b] = route;
  return g.dropSet.has(`${a.id}:${b.id}`) && a.w[1] - b.w[1] >= 0.9 && goal.w[1] > b.w[1] + 0.9;
}
