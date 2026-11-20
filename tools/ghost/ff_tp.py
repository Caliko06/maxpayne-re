#!/usr/bin/env python3
"""Give every autopilot milestone a place to stand: `tp` = the waypoint nearest to its trigger, in its room or the closest room by exits.

A level database stores a waypoint as: name, an empty string, 0x1a, 12 floats (rotation, position in the room's frame). Only names
w<n>/wp<n>/waypoint<n> are used: C_Teleport to a Jumppoint ends the game ("Waypoint not found", rea/map/fights_23.md).
Usage: ff_tp.py <rooms dir from ff_tp_check.mjs dump> [waypoint to avoid ...]. Writes tools/ghost/exits.json; an existing `tp` stays.
"""
import json, math, os, re, struct, sys

EXITS = 'tools/ghost/exits.json'
REC = re.compile(rb'\x14[\x01-\x0f]\r\x14([\x03-\x7f])(::[^\r\x00]+)\r\x14\x00\x1a([\s\S]{48})')
WP = re.compile(r'(w|wp|waypoint)[ _]?\d+$', re.I)


def records(level):
    m = re.match(r'part(\d)_level(\d)(b?)', level)
    b = open(f'build/data/lvl/1_Part{m[1]}_Level{m[2]}{m[3]}.ldb', 'rb').read()
    out = {}
    for r in REC.finditer(b):
        if r[1][0] == len(r[2]):
            f = struct.unpack('<12f', r[3])
            if all(abs(x) < 1e5 for x in f):
                out.setdefault(r[2].decode('latin1'), f[9:12])
    return out


def room(name):
    return name.lstrip(':').split('::')[0].lower()


def through(p, e):
    # p_B = p_A*R + t, 0x646020 (host/gamestate.c, Portal)
    R, t = e['R'], e['t']
    return tuple(p[0] * R[i] + p[1] * R[3 + i] + p[2] * R[6 + i] + t[i] for i in range(3))


def rings(graph, start, at):
    """(room index, trigger position in that room's frame), nearest rooms first."""
    names = [room(n) for n in graph['rooms']]
    if start not in names:
        return []
    first = names.index(start)
    seen, ring, order = {first}, [(first, at)], []
    while ring:
        order.append(ring)
        nxt = []
        for a, p in ring:
            for e in graph['portals']:
                if e['a'] == a and e['b'] not in seen:
                    seen.add(e['b'])
                    nxt.append((e['b'], through(p, e)))
        ring = nxt
    return [[(names[i], p) for i, p in r] for r in order]


def main():
    d = json.load(open(EXITS, encoding='utf8'))
    rooms_dir, avoid = sys.argv[1], set(sys.argv[2:])
    same = near = missing = 0
    for level, route in d.items():
        if not isinstance(route, dict) or not route.get('ff'):
            continue
        rec = records(level)
        graph = json.load(open(os.path.join(rooms_dir, level + '.json')))
        wps = {n: p for n, p in rec.items() if WP.fullmatch(n.split('::')[-1]) and n.count('::') == 2 and n not in avoid}
        for st in route['ff']:
            if st.get('tp'):
                continue
            t = next(iter([st['t']] if isinstance(st.get('t'), str) else st.get('t') or []), None) or next(iter(st.get('kill') or []), None)
            if not t or not t.startswith('::'):
                continue
            if t not in rec:   # a trigger named inside a sub-object (::SCENIC_LIFT_01::x): the record carries its room in front (::horne03::SCENIC_LIFT_01::x)
                full = next((k for k in rec if k.endswith(t.removesuffix('.TRIGGER')) or k.endswith(t)), None)
                t = full or t
            at = rec.get(t, (0.0, 0.0, 0.0))
            for hops, ring in enumerate(rings(graph, room(t), at)):
                here = [(math.dist(wps[n], p), n) for r, p in ring for n in wps if room(n) == r]
                if here:
                    st['tp'] = min(here)[1]
                    same += hops == 0
                    near += hops > 0
                    if hops:
                        print(level, st['name'], t, '->', st['tp'], 'rooms away', hops)
                    break
            else:
                missing += 1
                print('NONE', level, st['name'], t)
    json.dump(d, open(EXITS, 'w', encoding='utf8'), indent=1, ensure_ascii=False)
    print('same room', same, 'nearby', near, 'none', missing)


main()
