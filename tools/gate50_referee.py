# GATE50 referee (orchestrator 2026-11-19): classifies every slow frame of one telemetry.ndjson by cause; the last line is JSON for tools/audit_rails.py.
# Usage: python3 tools/gate50_referee.py <telemetry.ndjson>
import json,sys,collections
P=sys.argv[1]
head=None;rows=[]
for l in open(P):
    o=json.loads(l)
    if o.get("kind")=="session": head=o
    elif "rows" in o: rows+=o["rows"]
C={c:i for i,c in enumerate(head["cols"])}; M=head["modes"]
SUM=["vsync","gpuWait","gilMain","read","idleMain","gpuRead","save","saves","texKb","pipes","shaders","reads","upKb","draws"]
fr=[];acc=None;lt=None
for r in rows:
    if acc is None: acc={k:0 for k in SUM}
    for k in SUM: acc[k]+=r[C[k]]
    if r[C["present"]]:
        t=r[C["t"]]
        if lt is not None:
            acc.update(t=t,dt=t-lt,mode=M[int(r[C["mode"]])] if r[C["mode"]]>=0 else "?",bt=r[C["bt"]],hp=r[C["hp"]])
            acc["game"]=max(0,acc["dt"]-acc["vsync"]-acc["gpuWait"]-acc["gilMain"]-acc["read"]-acc["idleMain"]-acc["gpuRead"]-acc["save"])
            fr.append(acc)
        lt=t;acc=None
def near(i,f,w):
    return any(f(fr[j]) for j in range(max(0,i-w),min(len(fr),i+w+1)))
cnt=collections.Counter();ex=collections.defaultdict(list)
for i,f in enumerate(fr):
    if f["mode"] not in("game","comic") or f["dt"]<=33.4: continue
    # first frames after a mode change are load tail
    if near(i,lambda g:g["mode"] in("load","menu","boot"),3): c="tail of load/mode switch"
    elif near(i,lambda g:g["saves"]>0 or g["save"]>0,30): c="save (autosave/quicksave)"+(" +GPU readback" if near(i,lambda g:g["gpuRead"]>0,30) else "")
    elif f["gpuRead"]>0: c="GPU readback"
    elif f["pipes"]+f["shaders"]>0: c="new shader/pipeline"
    elif f["texKb"]>2000: c="texture upload"
    elif f["read"]>8: c="file read"
    elif i>0 and near(i,lambda g:False,0) or any(fr[j]["bt"]!=fr[j-1]["bt"] for j in range(max(1,i-7),min(len(fr),i+8))): c="bullet time toggle"
    elif f["game"]>33: c="game code (guest CPU)"
    elif f["vsync"]>f["game"]: c="vsync wait (missed beat)"
    else: c="mixed game+vsync"
    k=f["mode"]+": "+c; cnt[k]+=1; ex[k].append((round(f["t"]/1000,1),round(f["dt"]),round(f["game"]),round(f["vsync"])))
for k,v in cnt.most_common(): print(f"{v:4}  {k}   e.g. {ex[k][:3]}")
print("total",sum(cnt.values()))
# 1s windows fps min in game
import math
bins=collections.Counter()
for f in fr:
    if f["mode"]=="game": bins[int(f["t"]//1000)]+=1
lows=[s for s,n in bins.items() if n<50 and bins.get(s-1,0)>=50 and bins.get(s+1,0)>=50]
print("game seconds:",len(bins),"seconds <50 frames:",sum(1 for n in bins.values() if n<50),"isolated:",len(lows))
# low seconds: away from mode changes?
modesec={}
for f in fr: modesec.setdefault(int(f["t"]//1000),set()).add(f["mode"])
pure=[s for s,n in bins.items() if n<50 and modesec[s]=={"game"} and modesec.get(s-1)=={"game"} and modesec.get(s+1)=={"game"}]
print("low seconds in pure game:",len(pure),sorted(pure)[:40])
for s in sorted(pure)[:12]:
    fs=[f for f in fr if int(f["t"]//1000)==s]
    print(s,len(fs),"max",round(max(f["dt"] for f in fs)),"sum dt",round(sum(f["dt"] for f in fs)),"saves",sum(f["saves"] for f in fs),"gpuRead",round(sum(f["gpuRead"] for f in fs)), "bt",{f["bt"] for f in fs})
sl=[f for f in fr if f["mode"]=="game" and f["dt"]>33.4]
bt1=[f for f in sl if f["bt"]==1]
gm=[f for f in fr if f["mode"]=="game"]
print("slow game frames",len(sl),"with bt on",len(bt1),"; bt on share of game frames",round(sum(f['bt']==1 for f in gm)/len(gm)*100,1),"%")
print("vsync-class slow frames during bt:",sum(1 for f in bt1 if f["vsync"]>f["game"]))
print("---- E2E, all modes")
lo=[f for f in fr if f["mode"] in ("load","boot","menu") and f["dt"]>33.4]
print("load/boot/menu frames >33.4:",len(lo),"sum s",round(sum(f["dt"] for f in lo)/1000,1),
 ">250:",sum(f["dt"]>250 for f in lo),"texKb>50MB:",sum(f["texKb"]>51200 for f in lo),"read-dominant:",sum(f["read"]>f["game"] for f in lo))
allslow=[f for f in fr if f["dt"]>33.4]
print("all frames >33.4:",len(allslow),"of",len(fr))
w=collections.deque();low=0;secs=0
# 1s windows over all frames
ts=[f["t"] for f in fr];j=0;bad=set()
for i,t in enumerate(ts):
    while ts[j]<=t-1000:j+=1
    if i-j+1<50: bad.add(int(t//1000))
print("seconds with <50 frames (any mode):",len(bad),"of",int((ts[-1]-ts[0])/1000))
causes={}
for k,v in cnt.items(): causes[k]=v
out={"all_over33":len(allslow),"frames":len(fr),"low_seconds_any":len(bad),"low_seconds_pure_game":len(pure),
 "load_over33":len(lo),"load_over250":sum(f["dt"]>250 for f in lo),"load_seconds":round(sum(f["dt"] for f in fr if f["mode"] in("load","boot"))/1000,1),
 "save_over33":sum(v for k,v in cnt.items() if "save" in k),"bt_over33":sum(v for k,v in cnt.items() if "bullet" in k),
 "bt_share_pct":round(sum(f['bt']==1 for f in gm)/max(1,len(gm))*100,1),"causes":causes}
print("REFEREE "+json.dumps(out))
