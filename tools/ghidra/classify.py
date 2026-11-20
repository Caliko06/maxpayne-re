#!/usr/bin/env python3
"""Label every function of the six Max Payne images with a subsystem.

Input : rea/map/<image>.tsv (Ghidra export) + rea/c/<image>/<addr>.c (decompiled C)
Output: rea/map/subsystems.tsv  (image, entry, name, subsystem, confidence, basis)

Stages (stop at the first that decides):
  1 FACT       namespace/class of the DLL function, thunk/unwind/catch names, atexit stubs
  2 FACT       string literals (file names, menu text, error messages) in the function
  3 FACT       class methods the function calls (P_*, S_*, G_*, R_*, M_*, CWnd ...)
  4 INFERENCE  propagation over the call graph and over address neighbours (one TU = one range)
  5 INFERENCE  image default for the single-purpose DLLs (D3D8 driver, sound) when still unlabelled
Everything else stays unknown.
"""
import collections, math, os, re, sys

ROOT = os.path.expanduser('~/maxpayne-re')
MAP = f'{ROOT}/rea/map'
CDIR = f'{ROOT}/rea/c'
IMAGES = ['MaxPayne.exe', 'e2mfc.dll', 'rlmfc.dll', 'grphmfc.dll', 'sndmfc.dll', 'e2_d3d8_driver_mfc.dll']
SUBS = ['render', 'scene', 'physics', 'sound', 'input', 'files', 'memory', 'timing', 'ui', 'graphic_novel',
        'level', 'game', 'save', 'video', 'math', 'strings_containers', 'crt_runtime', 'unknown']
# labels that are shared utilities: they are not spread over the call graph (everybody calls them)
UTIL = {'crt_runtime', 'strings_containers', 'memory', 'math'}

# ---- class -> subsystem (class names come from the DLL exports and from the decompiled calls) ----
CLASS = {}
def cls(sub, names):
    for n in names.split():
        CLASS[n] = sub
cls('render', '''P_Driver P_Device P_DisplayMode P_DriverCaps P_DriverMap P_CPUDriver P_ReferenceCPUDriver P_Material
  P_MaterialInfo P_Map P_Bitmap P_BitmapInterface P_VertexFormat P_Buffer P_ReferenceVertexBuffer P_VertexBuffer
  P_TextureFormat P_ZBufferFormat P_VertexShader P_PixelShader P_Primitive P_OutputPrimitive P_LinePrimitive
  P_ParticlePrimitive P_2DLineObject P_3DLineObject P_Statistics P_LayerArray P_ReferenceTriangleBuffer
  P_DriverException_DirectDrawError G_Image G_Color G_FColor''')
cls('scene', '''P_Scene P_BaseObject P_VirtualObject P_ReferenceVirtualObject P_ReferenceBaseObject P_ObjectContainer
  P_Camera P_LODObject P_SpotLight P_PointLight P_DirectionalLight P_VirtualLight P_TriangleMeshBase P_TriangleMesh
  P_SkinMesh P_ReferenceSkinMesh P_ReferenceTriangleMesh P_PrimitiveInstance P_Sprite P_ParticleSystemBase
  P_Particle P_TestParticleSystem P_UserData P_DeviceDependentBaseObject P_DeviceDependentVirtualObject
  P_ParticleSystemBase::Particle''')
cls('physics', 'P_CollisionBuffer')
cls('ui', 'P_Text P_Font CDialog CSliderCtrl CButton CEdit CStatic CComboBox')
cls('sound', 'S_Listener S_Sound S_SoundOmni S_SoundSpot S_Loader S_WaveLoader S_Device')
cls('files', '''R_File R_MemoryFile R_FileStream R_UnbufferedFileStream R_FileDirectoryChange R_Script R_ScriptLock
  R_MemoryFile::Chunk''')
cls('memory', 'R_Memory')
cls('timing', 'R_Timer R_Profile')
cls('math', 'M_ScalarArray M_Vector2Array M_Vector3Array M_Vector4Array M_FPUControl M_Float16 M_MathInit R_CPUReference')
cls('strings_containers', 'std::rs rs std CString string R_STR')
cls('crt_runtime', '''R_Exception R_Mutex R_MutexLock R_RemedyInit R_CPUDriver CException CObject type_info exception
  CWnd CWinApp CCommandLineInfo CArchive MSVCRT.DLL MSVCP60.DLL MFC42.DLL''')
# R_System: timing for the counters, memory for physical memory, rest is system glue
CLASS['R_System'] = 'crt_runtime'
METHOD = {('R_System', 'getPerformanceCounter'): 'timing', ('R_System', 'getPerformanceFrequency'): 'timing',
          ('R_System', 'getTotalPhysicalMemory'): 'memory'}
# D3D8 driver classes
D3D_PREFIX = 'P_D3D'

# classes seen in calls that carry no information about where the caller lives
IGNORE_CALL = {'R_Exception', 'type_info', 'exception', 'DLL', 'struct_std', 'class_std', 'CException', 'CObject',
               'CString', 'R_Memory', 'M_FPUControl', 'CWnd', 'CWinApp', 'CCommandLineInfo'}
STRONG_CLASSES = None  # filled below: classes whose subsystem is a domain label

# ---- string rules (regex on the function's string literals) : (regex, subsystem, weight) ----
S = [
    (r'GraphicNovel|graphicnovel|GRAPHICNOVEL', 'graphic_novel', 3),
    (r'savegame|SaveGame|\.mps|\.mds|MP_SGI_|MP_GM_SAVE|MP_GM_LOAD|SaveHUDText|LoadHUDText|CannotSave|QuickSave|AutoSave|'
     r'Save game|SaveScreen|Max Payne Save|SaveLimited|SaveGameLimit|UnableToSave', 'save', 3),
    (r'video clip|Media player|StartupVideo|\.bik|\.avi|\.mpg|\bmovie|IMedia|DirectShow', 'video', 3),
    (r'X_InputDeviceException|DIERR_|Joystick|Keyboard|mouse acquire|DirectInput|Button0 type|input\.txt|'
     r'Input Settings|\bMouse\b|Artificial player input|KeyPress', 'input', 3),
    (r'^Level (init|deinit)|LevelOnInitMessages|MP_GM_LEVEL_INIT|DumpFactories from deiniting level', 'level', 12),
    (r'X_SharedDB|X_LevelDB|LevelDB|LDB|Level init|Level deinit|Level items|Level Runtime|Level Database|ldb of level|'
     r'\.ldb|missing AI movement network|Level doesn.t contain|Game Level|Room creation|Preload geometry|'
     r'Room %s|room "%s"|in room|Room name|Room axes|Room \'%s\'', 'level', 2),
    (r'MaxPayne_HUDMode|HUDFadeLayer|hud\.txt|X_ScrollingTextWindow|X_MenuModeBase|X_MenuLine|MaxPayne_MenuMode|menu\.txt|'
     r'DeveloperMenu|X_ConsoleMode|console\.txt|MenuLoadingScreen|loadingscreen|GameExitScreen|Crosshair|ProgressBar|'
     r'Console line|Commands must end|Unknown command|MainDialog|AdvancedDialog|OKButton|CancelButton|'
     r'Video Settings|Audio Settings|Input Settings|Advanced Settings|Other Settings|Task Switching|Display Width|'
     r'Display Height|Display Bits|Password|DialogTexts|dialogtexts|X System|X_Message::parse|Legend:|Framerate Smoothness|'
     r'Texture Color Depth|Antialiasing|Filtering Mode|Particle Detail|Character Detail|Level Texture Detail|'
     r'Other Texture Detail|Character Shadows|WideScreenMultiplier|%d Stand|%d Movement|%d Use|%d Turn|%d Get damage|'
     r'%d Weapon specific|\bFont\b|font', 'ui', 2),
    (r'Sound|Music|\.wav|Audio|Volume|SOUNDS|3DSound|2DSound|Modeswitch sound', 'sound', 2),
    (r'collision|Collision|capsule|Capsule|collid|Debris|ricoch|Grav\.vel|Anim\.vel', 'physics', 2),
    (r'KF_|KF2|KF Memory|skeleton|Skeleton|Skin mesh|SKD|\bBone\b|Bone "|camera|Camera|textureanimation|AnimationSet|'
     r'Animation|animation|Dynamic object|Absolute camera|LookAt|FOV|Frame %d out of bounds', 'scene', 2),
    (r'Tris |Prim rend|Prim proc|Tex Used|VidMem|Mipmap|LOD Bias|D3D8|3D engine drivers|Fogging|getSceneToRender|'
     r'Screen Buffers|CombinationMaps|Decal|Tesselation|texture|Texture|PS_ParticleSystem|PSys|PSCombination|'
     r'Particle|particle|DirectX allows|Total %i maps|Tex %d|Quadtree|Objects %d, Lights|Scene: %i|Modeswitch render|'
     r'Shadows|Surface Material|Detail Texture|Invalid image file|\.tga', 'render', 1),
    (r'FPS|Framerate|%6\.2fms|%5\.1f%%|Performance measure|Unclassified|statistics|Statistics|Profile|%4\.2fx|Time %',
     'timing', 2),
    (r'Memory dump|debugmem|AllocationGranularity|enableloadingstats', 'memory', 2),
    (r'X_Message|X_MessageQueue|this->C_|FSM|X_Projectile|X_Weapon|X_Character|X_GlobalAI|AI |AI network|AI path|'
     r'Combat|Dodge|Shoot|Mobster|Merc|Enemy|enemy|Waypoint|waypoint|path|Path|Projectile|projectile|Weapon|weapon|'
     r'Character|Player|player|Health|health|Bullet|bullet|Painkiller|painkiller|Startpoint|startpoint|Trigger|TRIGGER|'
     r'T_Activate|DO_|MaxPayne_GameMode|MaxPayne_Player|MP_GM|MP_GAO|MP_MS_|MaxPayne_ModeSwitch|X_LRRC|Hit %s|'
     r'activator|Message|message|jumppoint|difficulty|Difficulty|nightmare|BulletTime|SlowMotion|slowmotion|'
     r'Hardcore|Nonreactive|IdleUntil|StandAnd|WalkAnd|RunWalk|Retreat|GoTo|Goto|Crouch|Single|Jump|Visual scan|'
     r'Randomized shooting|Movement target|Ignoring setMovement|X_AnimationMessages|X_CharacterProperties|'
     r'Skin %d|SkinItem|Inventory|inventory|Ammo|Pickup|Upper|Lower Body|Char\.|Weapons|Receiver|receiver', 'game', 1),
    (r'\.txt|\.scr|\.ras|\.kfx|file |File |Invalid File', 'files', 0.3),
    (r'Max Payne - |Remedy Entertainment|Exception: |already running|DirectX 8|Demo version|minimum installation',
     'crt_runtime', 0.5),
]
S = [(re.compile(r), sub, w) for r, sub, w in S]
STR_IGNORE = {'Out of system memory resources', 'class ', 'class', 'X System', 'default', 'invalid rv<T> subscript',
              'String', 'Total', 'message', 'general', 'General', 'level', 'modifiers', 'Modifiers', 'properties',
              'Properties', 'window', 'unknown', 'false', 'normal', 'minimum', 'maximum', 'linear', 'Default'}

FN_RE = re.compile(r'\bFUN_([0-9a-f]{8})\b')
CALL_RE = re.compile(r'\b((?:std::)?[A-Za-z_][A-Za-z0-9_]*)::(~?[A-Za-z_][A-Za-z0-9_]*|operator[^\s(]*)\s*\(')
STRLIT_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def read_tsv(img):
    rows = []
    with open(f'{MAP}/{img}.tsv') as f:
        next(f)
        for line in f:
            p = line.rstrip('\n').split('\t')
            p += [''] * (9 - len(p))
            rows.append(dict(entry=p[0], size=int(p[1] or 0), name=p[2], ns=p[3], ncallers=int(p[4] or 0),
                             callees=[x for x in p[5].split(',') if x], imports=p[6], strings=p[7],
                             callers=[x for x in p[8].split(',') if x]))
    return rows


def short_ns(ns):
    return ns.split('::')[-1] if ns else ns


def class_sub(c, method=None):
    if (c, method) in METHOD:
        return METHOD[(c, method)]
    if c in CLASS:
        return CLASS[c]
    c2 = c.split('::')[-1]
    if c2.startswith(D3D_PREFIX):
        return 'render'
    return CLASS.get(c2)


def seed_rows(img, rows):
    """stage 1-3: returns dict entry -> (sub, conf, basis)"""
    lab = {}
    dll = img.endswith('.dll')
    for r in rows:
        e, n, ns = r['entry'], r['name'], r['ns']
        if n.startswith('Unwind@') or n.startswith('Catch@') or n.startswith('thunk_'):
            kind = n.split('@')[0].split('_')[0]
            lab[e] = ('crt_runtime', 'FACT', f'{kind} funclet/thunk by name')
            continue
        if n in ('entry', 'DllMain', '_DllMainCRTStartup') or n.startswith('__') or n in ('xtoa', '_atexit'):
            lab[e] = ('crt_runtime', 'FACT', f'CRT entry/helper {n}')
            continue
        if ns != 'Global':
            c = short_ns(ns)
            if ns.startswith(('MSVCRT.DLL', 'MSVCP60.DLL', 'MFC42.DLL', 'KERNEL32', 'DSOUND', 'DINPUT')):
                sub = 'input' if ns.startswith('DINPUT') else 'sound' if ns.startswith('DSOUND') else 'crt_runtime'
                if ns.startswith('MFC42.DLL::CString'):
                    sub = 'strings_containers'
                if ns.startswith('MFC42.DLL::C') and c in ('CDialog', 'CSliderCtrl', 'CButton', 'CEdit', 'CStatic', 'CComboBox'):
                    sub = 'ui'
                lab[e] = (sub, 'FACT', f'import/namespace {ns}')
                continue
            if ns.startswith('RLMFC.DLL::') and not dll:
                c = short_ns(ns)
            sub = class_sub(c, n)
            if sub is None and ns.startswith('std'):
                sub = 'strings_containers'
            if sub:
                lab[e] = (sub, 'FACT', f'class {c}')
                continue
            if ns in ('namespace',):
                continue
        else:
            if n in ('M_RAND_01', 'M_RAND', 'M_funcMakeInverseSqrtLUT') or n.startswith('M_'):
                lab[e] = ('math', 'FACT', f'name {n}')
                continue
            if n in ('R_STR',) or n.startswith('R_STR'):
                lab[e] = ('strings_containers', 'FACT', f'name {n}')
                continue
            if n.startswith('R_REAL'):
                lab[e] = ('memory', 'FACT', f'name {n}')
                continue
            if n.startswith('operator') and dll and img == 'rlmfc.dll':
                # operator<< / >> on R_ streams: file/stream serialisation; operator new/delete: memory
                if 'new' in n or 'delete' in n:
                    lab[e] = ('memory', 'FACT', f'name {n}')
                else:
                    lab[e] = ('files', 'FACT', f'rlmfc stream operator {n}')
                continue
            if n == 'swap':
                lab[e] = ('strings_containers', 'FACT', 'name swap')
                continue
    return lab


def load_c(img, rows):
    """feature extraction from the decompiled C"""
    feats = {}
    d = f'{CDIR}/{img}'
    for r in rows:
        e = r['entry']
        try:
            txt = open(f'{d}/{e}.c', errors='replace').read()
        except FileNotFoundError:
            feats[e] = None
            continue
        calls = collections.Counter()
        for c, m in CALL_RE.findall(txt):
            calls[(c, m)] += 1
        strs = [s.strip() for s in r['strings'].split(' | ') if s.strip() and s.strip() not in STR_IGNORE and len(s.strip()) > 1]
        fn = set(FN_RE.findall(txt))
        feats[e] = dict(sentinel=('0x7fffffff' in txt), calls=calls, strs=strs, fn=fn, unwind=('unaff_EBP' in txt and r['size'] < 80),
                        atexit=bool(re.search(r'FUN_0076f952\(&LAB_', txt)) and r['size'] < 40,
                        ncall=len(re.findall(r'\w+\(', txt)), txt_len=len(txt))
    return feats


def string_vote(strs):
    v = collections.Counter()
    why = collections.defaultdict(list)
    for s in strs:
        for rx, sub, w in S:
            if rx.search(s):
                v[sub] += w
                if len(why[sub]) < 2:
                    why[sub].append(s[:30])
                break  # first matching rule per string
    return v, why


def call_vote(calls):
    """votes from called class methods; returns (strong, weak, basis)"""
    v = collections.Counter()
    why = {}
    for (c, m), n in calls.items():
        if c in IGNORE_CALL:
            continue
        sub = class_sub(c, m)
        if not sub:
            continue
        w = 1.0
        if sub in UTIL or sub in ('files', 'timing'):
            w = 0.35
        v[sub] += w * min(n, 3)
        why.setdefault(sub, f'{c}::{m}')
    return v, why


def classify_exe_like(img, rows, lab, feats):
    for r in rows:
        e = r['entry']
        if e in lab:
            continue
        f = feats.get(e)
        if not f:
            continue
        if f['atexit']:
            lab[e] = ('crt_runtime', 'FACT', 'atexit registration stub')
            continue
        if f['unwind']:
            lab[e] = ('crt_runtime', 'FACT', 'SEH unwind funclet (unaff_EBP, tiny)')
            continue
        sv, swhy = string_vote(f['strs'])
        cv, cwhy = call_vote(f['calls'])
        tot = collections.Counter()
        for k, x in sv.items():
            tot[k] += x
        for k, x in cv.items():
            if k in UTIL or k in ('files', 'timing'):
                continue  # incidental helpers: only used when nothing else speaks
            tot[k] += x
        if not tot:
            for k, x in cv.items():
                tot[k] += x
        if not tot:
            if f['sentinel'] and r['size'] < 1000:
                lab[e] = ('strings_containers', 'INFERENCE', 'intrusive list/container template (0x7fffffff cache sentinel)')
            continue
        top, topv = tot.most_common(1)[0]
        rest = sum(tot.values()) - topv
        # a utility vote only counts when nothing domain-like is present and the function is small
        if top in UTIL and f['txt_len'] > 6000:
            continue
        if topv < 0.9 and not (sv.get(top, 0) > 0 and sv[top] >= 0.3 and len(tot) == 1):
            if not (top in UTIL and topv >= 0.35 and f['txt_len'] < 2500 and len(tot) == 1):
                continue
        src = []
        if sv.get(top):
            src.append('string "%s"' % swhy[top][0] if top in swhy else 'string')
        if cv.get(top):
            src.append('calls ' + cwhy[top])
        if rest == 0:
            lab[e] = (top, 'FACT', ' + '.join(src))
        elif topv >= 1.5 * rest:
            lab[e] = (top, 'INFERENCE', ' + '.join(src) + f' (majority, other={rest:.1f})')
        # else: ambiguous, leave for propagation


def build_graph(img, rows, feats):
    idx = {r['entry']: i for i, r in enumerate(rows)}
    byname = collections.defaultdict(list)
    for r in rows:
        byname[r['name']].append(r['entry'])
    edges = collections.defaultdict(set)
    exe = (img == 'MaxPayne.exe')
    for r in rows:
        e = r['entry']
        targets = set()
        if exe and feats.get(e):
            for a in feats[e]['fn']:
                if a in idx and a != e:
                    targets.add(a)
        for nm in r['callees']:
            if nm.startswith('FUN_') and exe:
                continue
            for t in byname.get(nm, [])[:3]:
                if t != e:
                    targets.add(t)
        for t in targets:
            edges[e].add(t)
            edges[t].add(e)
        for nm in r['callers']:
            for t in byname.get(nm, [])[:3]:
                if t != e:
                    edges[e].add(t)
                    edges[t].add(e)
    return idx, edges


def propagate(img, rows, lab, edges, idx):
    n = len(rows)
    ent = [r['entry'] for r in rows]
    sizes = [r['size'] for r in rows]
    # round schedule: (threshold, minimum share)
    sched = [(1.2, 0.7), (0.9, 0.65), (0.7, 0.6), (0.5, 0.55), (0.4, 0.5), (0.3, 0.5), (0.2, 0.45), (0.15, 0.4)][:5]
    for rnd, (theta, share) in enumerate(sched):
        new = {}
        changed = True
        cur = {e: l[0] for e, l in lab.items()}
        for i, e in enumerate(ent):
            if e in lab:
                continue
            sc = collections.Counter()
            who = collections.defaultdict(list)
            nb = edges.get(e, ())
            for m in nb:
                lm = cur.get(m)
                if lm is None or lm == 'unknown' or lm in UTIL:
                    continue
                deg = len(edges.get(m, ()))
                w = 1.0 / math.sqrt(max(deg, 1))
                if len(nb) > 40:
                    w *= 0.3  # a shared helper: its callers tell little
                sc[lm] += w
                if len(who[lm]) < 2:
                    who[lm].append(m)
            for off, w in ((1, 0.7), (2, 0.45), (3, 0.3), (4, 0.2)):
                for j in (i - off, i + off):
                    if 0 <= j < n:
                        lm = cur.get(ent[j])
                        if lm is None or lm == 'unknown' or lm in UTIL:
                            continue
                        sc[lm] += w
            if not sc:
                continue
            top, tv = sc.most_common(1)[0]
            tot = sum(sc.values())
            if tv >= theta and tv / tot >= share:
                ncall = sum(1 for m in nb if cur.get(m) == top)
                new[e] = (top, 'INFERENCE', f'neighbours: {ncall} linked + address-adjacent {top} (round {rnd + 1})')
        lab.update(new)
        print(f'  {img} round {rnd + 1}: +{len(new)}', file=sys.stderr)
        if not new and rnd > 4:
            pass
    return lab


def util_pass(img, rows, lab, edges):
    """unlabelled helpers whose linked neighbours are all utilities (STL/CRT/exception glue)"""
    add = {}
    for r in rows:
        e = r['entry']
        if e in lab:
            continue
        v = collections.Counter(lab[m][0] for m in edges.get(e, ()) if m in lab)
        if not v:
            continue
        if r['size'] <= 60 and len(edges.get(e, ())) <= 3:
            callees = [m for m in edges.get(e, ()) if m in lab]
            if len(callees) == 1:
                add[e] = (lab[callees[0]][0], 'INFERENCE', 'tiny wrapper of ' + callees[0])
                continue
        top, tv = v.most_common(1)[0]
        if top in UTIL and tv >= 2 and tv / sum(v.values()) >= 0.6:
            add[e] = (top, 'INFERENCE', f'linked to {tv} {top} functions')
    lab.update(add)
    # a function between two utility functions of the same kind (template blocks)
    ent = [r['entry'] for r in rows]
    add = {}
    for i in range(1, len(ent) - 1):
        if ent[i] in lab:
            continue
        a, b = lab.get(ent[i - 1]), lab.get(ent[i + 1])
        if a and b and a[0] == b[0] and a[0] in UTIL:
            add[ent[i]] = (a[0], 'INFERENCE', f'between two {a[0]} functions')
    lab.update(add)


def default_fill(img, rows, lab):
    defs = {'e2_d3d8_driver_mfc.dll': ('render', 'D3D8 driver image: every non-runtime function is driver code'),
            'sndmfc.dll': ('sound', 'sound DLL: every non-runtime function is sound code'),
            'grphmfc.dll': ('render', 'grphmfc image library (G_Image/G_Color), decoders for the textures')}
    if img not in defs:
        return
    sub, why = defs[img]
    for r in rows:
        if r['entry'] not in lab:
            lab[r['entry']] = (sub, 'INFERENCE', 'image default: ' + why)


def main():
    out = []
    only = sys.argv[1:] or IMAGES
    for img in only:
        rows = read_tsv(img)
        print(img, len(rows), file=sys.stderr)
        lab = seed_rows(img, rows)
        feats = load_c(img, rows)
        classify_exe_like(img, rows, lab, feats)
        n_seed = len(lab)
        idx, edges = build_graph(img, rows, feats)
        held = {}
        if os.environ.get('EVAL'):
            import random
            random.seed(1)
            for e in [e for e, l in lab.items() if l[1] == 'FACT' and l[0] not in UTIL and random.random() < 0.3]:
                held[e] = lab.pop(e)
        propagate(img, rows, lab, edges, idx)
        util_pass(img, rows, lab, edges)
        if held:
            ok = sum(1 for e, l in held.items() if e in lab and lab[e][0] == l[0])
            got = sum(1 for e in held if e in lab)
            print(f'  EVAL held {len(held)} assigned {got} correct {ok}', file=sys.stderr)
            byr = collections.defaultdict(lambda: [0, 0])
            for e, l in held.items():
                if e in lab:
                    m = re.search(r'round (\d+)', lab[e][2]); k = m.group(1) if m else 'other'
                    byr[k][0] += 1; byr[k][1] += lab[e][0] == l[0]
            print('  by round', dict(byr), file=sys.stderr)
            conf = collections.Counter((l[0], lab[e][0]) for e, l in held.items() if e in lab and lab[e][0] != l[0])
            print('  confusions', conf.most_common(12), file=sys.stderr)
            return
        if os.environ.get('NO_DEFAULT') != '1':
            default_fill(img, rows, lab)
        print(f'  seeds {n_seed}, labelled {len(lab)} / {len(rows)}', file=sys.stderr)
        for r in rows:
            l = lab.get(r['entry'], ('unknown', 'UNKNOWN', ''))
            out.append((img, r['entry'], r['name'], *l))
    path = os.environ.get('OUT', f'{MAP}/subsystems.tsv')
    with open(path, 'w') as f:
        f.write('image\tentry\tname\tsubsystem\tconfidence\tbasis\n')
        for o in out:
            f.write('\t'.join(x.replace('\t', ' ') for x in o) + '\n')


if __name__ == '__main__':
    main()
