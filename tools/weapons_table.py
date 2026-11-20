#!/usr/bin/env python3
"""Tabla de armas desde los datos del juego (WORKLIST #10, lado datos).

Fuentes (solo lectura, x_data extraído de x_data.ras):
  build/data/x_data/310_<weapon>.txt   definición de cada arma (17 archivos)
  build/data/x_data/3_shooting.h       macros de clip/pocket/frecuencia/daño
  build/data/x_data/3_weaponid.h       WEAPONID_* (solo para ordenar)
  build/data/x_data/63_<projectile>.txt  daño directo y P_Explosion (blast)
  build/data/x_data/161_default_skeleton.txt  animaciones del personaje: sus
      proyectiles (golpes cuerpo a cuerpo, p_createprojectiletobone), sus
      sonidos y el frame del C_Reload() de cada recarga
  ~/Games/maxpayne/x_data.ras          fps y frame count de las animaciones .kf2

Salida: census/weapons_data.tsv, una fila por arma, columnas separadas por tab:
  weapon, clip, pocket, frequency, damage_direct, blast, projectile,
  reload_time, sound_keys, model, source_file

Reglas (cada una con su porqué; un valor que no está en los datos va NA):
  - clip/pocket = Clipsize/Pocketsize del 310 con las macros resueltas.
  - frequency = MaximumShootingFrequency (la que usa el motor: intervalo =
    1 / (mult * Max)); el valor Default no entra en esta tabla.
  - projectile = lo que dispara UNA vez el arma: los P_CreateProjectile de su
    animación SHOOT/SHOOT_NOEFFECTS (sin case_* casquillos, clip_* cargador ni
    enemy_* drops) MÁS lo que crean esos proyectiles al explotar (un nivel:
    molotov -> frag_molotov x5). En cuerpo a cuerpo manda la animación del
    personaje: p_createprojectiletobone en CHARANIM_SHOOT<ARMA>, que es la que
    de verdad golpea (el pipe trae un P_CreateProjectile en su WEAPONANIM_SHOOT
    pero su animación de personaje nunca hace WS_Animate(SHOOT), así que ese
    proyectil no sale nunca).
  - damage_direct = Damage del archivo 63_<proyectil>.txt, una entrada por
    entrada de projectile y en el mismo orden.
  - blast = los P_Explosion de esos mismos 63_<proyectil>.txt, como
    "<proyectil> <daño_max>..<daño_min> @ r<radio_min>..<radio_max> x<n>"
    (varias explosiones en el mismo proyectil se resumen en su mínimo y máximo).
  - reload_time = segundos hasta el C_Reload() de la recarga del personaje:
    frame / fps, con el frame de 161_default_skeleton.txt y los fps del .kf2
    (registro de animación: +0 fps, +4 frame count; gameplay.md:414). Los
    [Revert] mandan a la animación real cuando el arma no tiene propia
    (sniper -> MP5, molotov/painkiller -> granada). Validación de que el orden
    (fps, frames) es el correcto: todo frame de C_Reload cae antes del frame
    count; al revés, 10 de 11 lo violarían. NA en pipe y bate (munición
    infinita, no hay recarga) y en empty (el arma vacía no recarga: su Revert
    presta la animación de la granada como relleno).
  - sound_keys = nombres de A_Play3DSound del script del arma y de su animación
    de personaje (SHOOT<ARMA> y la recarga), sin repetir.
"""

import glob
import importlib.util
import os
import re
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
XDATA = os.path.join(REPO, "build", "data", "x_data")
OUT = os.path.join(REPO, "census", "weapons_data.tsv")
ARCHIVE = os.path.expanduser("~/Games/maxpayne/x_data.ras")

NA = "NA"
COLS = [
    "weapon",
    "clip",
    "pocket",
    "frequency",
    "damage_direct",
    "blast",
    "projectile",
    "reload_time",
    "sound_keys",
    "model",
    "source_file",
]

DEFINE = re.compile(r"^\s*#define\s+(\w+)\s+(.*?)\s*$")
ATTR = re.compile(r"^\s*(\w+)\s*=\s*(.*?)\s*$")
CALL_PROJ = re.compile(r"[Pp]_[Cc]reate[Pp]rojectile(?:[Tt]o[Bb]one)?\s*\(\s*([\w\\]+)\s*,\s*([\w\s+*-]+?)\s*[,)]")
CALL_SND = re.compile(r"A_Play3DSound\s*\(\s*(\w+)\s*,\s*(\w+)")
CALL_BOOM = re.compile(r"P_Explosion\s*\(([^)]*)\)")
LOD = re.compile(r"ExportData\s*=\s*([^;\n]+)", re.IGNORECASE)
EFFECT_PREFIX = ("case_", "clip_", "enemy_")
MELEE_WEAPONS = ("leadpipe", "baseballbat")
NO_RELOAD = ("leadpipe", "baseballbat", "empty")


def read(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read()


def load_macros():
    """Todos los #define numéricos de los headers de x_data, sin resolver."""
    macros = {}
    for path in sorted(glob.glob(os.path.join(XDATA, "*.h"))):
        for line in read(path).split("\n"):
            m = DEFINE.match(line)
            if not m:
                continue
            value = m.group(2).split("//")[0].strip()
            if value:
                macros.setdefault(m.group(1), value)
    return macros


def resolve(value, macros, depth=0):
    """Resuelve macros numéricas hasta un literal. Devuelve None si no llega."""
    if value is None or depth > 8:
        return None
    value = value.strip().rstrip(";").strip()
    if not value:
        return None
    if re.fullmatch(r"-?\d+(\.\d+)?%?", value):
        return value
    if value in macros:
        return resolve(macros[value], macros, depth + 1)
    return None


def num(value, macros):
    """Literal numérico o NA."""
    got = resolve(value, macros)
    return got.rstrip("%") if got else NA


def attributes(text):
    """Pares clave = valor del bloque [Attributes]."""
    start = text.find("[Attributes]")
    if start < 0:
        return {}
    end = len(text)
    m = re.search(r"\n\[", text[start + len("[Attributes]"):])
    if m:
        end = start + len("[Attributes]") + m.start()
    out = {}
    for line in text[start:end].split("\n")[1:]:
        m = ATTR.match(line)
        if not m:
            continue
        key, value = m.group(1), m.group(2).split("//")[0].strip()
        if key not in out:
            out[key] = value
    return out


def anim_sections(text):
    """{Index: trozo} de cada [Animation], aunque el Index vaya en otra línea."""
    out = {}
    marks = [(m.start(), m.group(1)) for m in
             re.finditer(r"\[Animation\]\s*(?:\n\s*)?Index\s*=\s*(\w+)", text)]
    for i, (pos, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        out.setdefault(name, "")
        out[name] += text[pos:end]
    return out


def messages(body):
    """Solo los [Message] de un bloque: deja fuera [CharacterHit], [Decal], etc."""
    return "\n".join(re.findall(r"\[Message\][^\n]*", body))


def counted(calls):
    """[(nombre, cuenta)] agregando por nombre, en orden de aparición."""
    out = []
    for name, count in calls:
        name = name.split("\\")[-1]
        if name.lower().startswith(EFFECT_PREFIX):
            continue
        count = resolve(count, MACROS) or count.strip()
        for i, (n, total) in enumerate(out):
            if n.lower() == name.lower():
                out[i] = (n, str(int(float(total)) + int(float(count))))
                break
        else:
            out.append((name, count))
    return out


def script_projectiles(text):
    """Proyectiles de la animación de disparo del propio script del arma."""
    calls = []
    for anim, body in anim_sections(text).items():
        if not (anim.endswith("_SHOOT") or anim.endswith("_SHOOT_NOEFFECTS")):
            continue
        for m in CALL_PROJ.finditer(messages(body)):
            calls.append((m.group(1), m.group(2)))
    return counted(calls)


def sound_keys(text):
    keys = []
    for m in CALL_SND.finditer(text):
        if m.group(2).lower() not in [k.lower() for k in keys]:
            keys.append(m.group(2))
    return keys


def projectile_file(name):
    """Ruta del 63_<proyectil>.txt sin depender de mayúsculas del sistema."""
    want = name.lower()
    for path in glob.glob(os.path.join(XDATA, "63_*.txt")):
        stem = os.path.basename(path)[len("63_"):-len(".txt")]
        if stem.lower() == want:
            return path
    return None


def projectile_damage(name):
    path = projectile_file(name)
    if not path:
        return NA
    return num(attributes(read(path)).get("Damage"), MACROS)


def projectile_children(name):
    """Proyectiles que crea este proyectil al explotar (un nivel)."""
    path = projectile_file(name)
    if not path:
        return []
    calls = [(m.group(1), m.group(2))
             for m in CALL_PROJ.finditer(messages(read(path)))]
    return counted(calls)


def projectile_blast(name):
    """Resumen de los P_Explosion: daño máx..mín y radio mín..máx."""
    path = projectile_file(name)
    if not path:
        return None
    booms = []
    for m in CALL_BOOM.finditer(messages(read(path))):
        args = [a.strip() for a in m.group(1).split(",")]
        if len(args) < 4:
            continue
        try:
            r1, r2, d1, d2 = (float(a) for a in args[:4])
        except ValueError:
            continue
        booms.append((r1, r2, d1, d2))
    if not booms:
        return None
    rmin = min(min(b[0], b[1]) for b in booms)
    rmax = max(max(b[0], b[1]) for b in booms)
    dmax = max(max(b[2], b[3]) for b in booms)
    dmin = min(min(b[2], b[3]) for b in booms)
    tail = " x%d" % len(booms) if len(booms) > 1 else ""
    return "%s %g..%g @ r%g..%g%s" % (name, dmax, dmin, rmin, rmax, tail)


def model(text):
    """Primer ExportData del primer bloque [LOD] (el mesh LOD0)."""
    start = text.find("[LOD]")
    if start < 0:
        return NA
    m = LOD.search(text, start)
    return m.group(1).strip() if m else NA


def weapon_chain(name):
    """(anims del personaje, [Revert]) del arma."""
    anims = ANIMS
    up = name.upper()
    out = []
    for cand in ("CHARANIM_SHOOT" + up, "CHARANIM_RELOAD" + up):
        anim = cand
        for _ in range(4):
            if anim in anims:
                out.append((anim, anims[anim]))
                break
            if anim not in REVERTS:
                break
            anim = REVERTS[anim]
    return out


def char_projectiles(name):
    """Golpes cuerpo a cuerpo: p_createprojectiletobone de CHARANIM_SHOOT<ARMA>."""
    block = ANIMS.get("CHARANIM_SHOOT" + name.upper())
    if not block:
        return []
    calls = [(m.group(1), m.group(2)) for m in CALL_PROJ.finditer(messages(block))]
    return counted(calls)


def reload_time(name):
    """Segundos hasta C_Reload(): frame del personaje / fps del kf2."""
    if name in NO_RELOAD:
        return NA
    block = ANIMS.get("CHARANIM_RELOAD" + name.upper())
    anim = "CHARANIM_RELOAD" + name.upper()
    for _ in range(4):
        if anim in ANIMS:
            block = ANIMS[anim]
            break
        if anim not in REVERTS:
            return NA
        anim = REVERTS[anim]
    if not block:
        return NA
    m = re.search(r"\[Message\]\s*Frame\s*=\s*(\d+);\s*String\s*=\s*\"this->C_Reload\(\)", block)
    if not m:
        return NA
    fps, frames = KF2.get(anim, (None, None))
    if not fps:
        return NA
    frame = int(m.group(1))
    if frames and frame >= frames:
        return NA  # violaría AnimationFrameOutOfLimits: dato raro, no se adivina
    return "%.3f" % (frame / fps)


def load_character_anims():
    """{CHARANIM_*: bloque} y {From: To} de los [Revert]."""
    text = read(os.path.join(XDATA, "161_default_skeleton.txt"))
    anims = {}
    for m in re.finditer(r"\[Animation\]\s*(?:\n\s*)?Index\s*=\s*(CHARANIM_\w+);(.*?)(?=\n\s*\[Animation\]|\n\s*\[Revert\]|\Z)", text, re.S):
        anims[m.group(1)] = m.group(2)
    reverts = dict(re.findall(r"\[Revert\]\s*From\s*=\s*(CHARANIM_\w+);\s*To\s*=\s*(CHARANIM_\w+);", text))
    return anims, reverts


def load_kf2():
    """{anim: (fps, frames)} leyendo el .kf2 del archivo x_data.ras en memoria."""
    spec = importlib.util.spec_from_file_location("ras", os.path.join(REPO, "tools", "ras.py"))
    ras = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ras)
    f, _seed, _ver, files, _dt = ras.read(ARCHIVE)
    by_name = {}
    for entry in files:
        base = entry["name"].split("\\")[-1]
        base = re.sub(r"^\d+_", "", base)
        if base.lower().endswith(".kf2"):
            by_name[base.lower()] = entry
    out = {}
    for anim, block in ANIMS.items():
        m = re.search(r"Filename\s*=\s*anim\\([\w.]+\.kf2)", block)
        if not m:
            continue
        entry = by_name.get(m.group(1).lower())
        if not entry:
            continue
        blob = ras.extract(f, entry)
        hit = re.search(rb"\x0d\x14(.)", blob, re.S)
        if not hit:
            continue
        tail = blob[hit.end() + hit.group(1)[0]:]
        out[anim] = (tail[1], tail[10])  # +0 fps, +4 frame count (gameplay.md:414)
    return out


def row_for(path):
    text = read(path)
    attrs = attributes(text)
    name = os.path.basename(path)[len("310_"):-len(".txt")]

    projs = char_projectiles(name) if name in MELEE_WEAPONS else script_projectiles(text)
    grown = list(projs)
    for proj, count in projs:
        for child, ccount in projectile_children(proj):
            grown.append((child, str(int(float(count)) * int(float(ccount)))))
    damages = [projectile_damage(p) for p, _ in grown]
    blasts = [b for b in (projectile_blast(p) for p, _ in grown) if b]

    keys = sound_keys(text)
    for _anim, block in weapon_chain(name):
        for key in sound_keys(block):
            if key.lower() not in [k.lower() for k in keys]:
                keys.append(key)

    row = {
        "weapon": name,
        "clip": num(attrs.get("Clipsize"), MACROS),
        "pocket": num(attrs.get("Pocketsize"), MACROS),
        "frequency": num(attrs.get("MaximumShootingFrequency"), MACROS),
        "damage_direct": ";".join(damages) if damages else NA,
        "blast": ";".join(blasts) if blasts else NA,
        "projectile": ";".join("%s x%s" % (p, c) for p, c in grown) if grown else NA,
        "reload_time": reload_time(name),
        "sound_keys": ";".join(keys) if keys else NA,
        "model": model(text),
        "source_file": os.path.relpath(path, REPO),
    }
    wid = resolve(attrs.get("WeaponID"), MACROS)
    return row, (int(wid) if wid and wid.lstrip("-").isdigit() else 10 ** 6)


def main():
    global MACROS, ANIMS, REVERTS, KF2
    MACROS = load_macros()
    ANIMS, REVERTS = load_character_anims()
    KF2 = load_kf2()
    paths = sorted(glob.glob(os.path.join(XDATA, "310_*.txt")))
    if not paths:
        print("no hay 310_*.txt en %s" % XDATA, file=sys.stderr)
        return 1
    rows = []
    for path in paths:
        row, key = row_for(path)
        rows.append((key, row))
    rows.sort(key=lambda pair: (pair[0], pair[1]["weapon"]))

    lines = ["\t".join(COLS)]
    for _, row in rows:
        lines.append("\t".join(row[c] for c in COLS))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")

    for _, row in rows:
        print(
            "%-13s clip %-3s pocket %-4s freq %-6s dmg %-8s reload %-6s proj %s"
            % (row["weapon"], row["clip"], row["pocket"], row["frequency"],
               row["damage_direct"], row["reload_time"], row["projectile"])
        )
    print("%d filas + header -> %s" % (len(rows), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
