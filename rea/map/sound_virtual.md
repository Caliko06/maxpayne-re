# WORKLIST #11 — the sound gap: sounds the game asks for and never sends to Play

Read-only RE. Sources: `rea/c/sndmfc.dll/*.c` (Ghidra), `rea/c/MaxPayne.exe/*.c`, `build/data/x_data/3_soundid.h`,
`235_*.txt`, plus the raw image for the two call sites marked `[bytes]` (parsed with python, `build/img/sndmfc.dll`).
FACT = read in code/data; INFERENCE = my reading; UNKNOWN = not proven.

## 1. The rule: how a sound becomes and stays virtual (sndmfc)

Voice = `S_CHANNEL` pointer at `S_Sound+0x143`. `+0x143 == 0` while `+0x18d == 1` (playing) is exactly "requested, not played".

1. **2D request** `S_Sound::play` 0x1030b8d0 → `startPlaying` 0x1030b380. It gives the voice at once
   (`acquireVoice` 0x1030aad0) only if `+0x13e` (audibility) is 1 **and** the device is not the Null device
   (0x1030b380:111-114, `S_Device::getNullDeviceGUID` vs `getGUID`). Otherwise it takes the virtual branch
   (0x1030b380:114-126: position = `+0x139`, loops = `getLoopsLeft`, `+0x143 = 0`), then calls
   `startVoicePlayback` 0x1030b260 which is a no-op with no channel (guard `if (+0x143 != 0)`, 0x1030b260:19).
   Result: playing, no voice. FACT.
2. **3D request** `play` 0x1030b8d0:20-25 does not even start: if not playing/paused it sets `+0x1af = 1`
   (transform pending) and `+0x19d = 1` (start pending). The tick starts it 0.2 s later at most
   (`+0x1f0 + 0.2 < appAge`, constant 0x1030f348 = 0.2f, 0x10302d70:105-109) through `startPlayingVirtual`
   0x1030b6e0. It never starts a stream for a new sound: it sets `+0x143 = 0`, `+0x18d = 1` and only calls
   `startVoicePlayback` in the resume-from-pause branch, where a channel already exists (0x1030b6e0:32-35). FACT.
3. **The tick gives voices**, 0x10302d70:135-183: `qsort` by `+0x193` descending (`snd_compare_priority_desc`
   0x103032a0), then for the first `DAT_10315020` entries of that list, and only when `+0x143 == 0 && +0x13e == 1`
   and (3D) `distance² < falloff²` (`+0x14d`) → `activateVoice` 0x1030ba90 → `acquireVoice` + `startVoicePlayback`.
   Everything past the voice count stays virtual that tick. FACT.
4. **While virtual** the tick calls `updateVirtualPosition` 0x1030bc60 (0x10302d70:110-113): position =
   `+0x1b1 + samplesPerSecond * R_Timer age`, wrapped in `[loopStart, min(loopEnd, length)]`, `setLoopCount` with the
   loops left; when the loops run out it sets position 0 and calls `S_Sound::stop`. FACT. So a virtual sound is not
   frozen: it burns its loops and dies, silently.
5. **Losing a voice** (back to virtual): 3D beyond falloff, every tick, 0x10302d70:100-102 → `releaseVoice` 0x1030bb60;
   `setAudibility(false)` 0x1030bdc0:9-13 → same; steal by `acquireVoice` 0x1030aad0 (lowest priority of the 12).

### Priority, exactly (0x1030a8f0)
`+0x193 = +0x18e - (appAge - +0x1f0) * 0.0f * listener+0x181 + (formula 2 or 3) * (-distance) * listener+0x185`
Listener defaults (0x10301a90:117-119): `+0x181 = 1.0`, `+0x185 = 1.0`, `+0x189 = 2` (formula 2 = distance only).
The age weight is the shipped constant 0x1030f4c8 = **0.0**, so age never counts. 2D "distance" is `volume - 1.0`
(0x1030f368 = 1.0f), so a 2D sound at volume 1.0 has no penalty. INFERENCE: in the shipped build the sort key is
"own priority minus distance", i.e. `Priority = ...` from the sound block, `ID_WEAPON_PRIORITY = 50`,
`ID_PLAYERSHOES_PRIORITY = 8` (3_soundid.h:108, 79).

### The five ways a request stays virtual (and three that do not)
| cause | where | note |
|---|---|---|
| voice pool full, lower priority | 0x10302d70:139-183 | 12 voices max; sorted every tick, so a sound can be virtual for a while and play later |
| 3D beyond falloff | 0x1030b380:51-72, 0x10302d70:152-173 | never gets a voice; if it had one it is released |
| audibility off (`+0x13e == 0`) | 0x1030bdc0, gate at 0x10302d70:146 | the exe turns it off every frame the sound was not requested (see 3) |
| 3D start pending (`+0x1af`) | 0x1030b8d0:20-25 | up to 0.2 s virtual by design |
| Null audio device | 0x1030b380:111-114 | always virtual; `DAT_103159c4`/`0x103159d0` are NULL |
| *not* volume: `v < 0.01` still gets a voice and Play | 0x1030ae20 | host already logs it as `game volume off` |
| *not* missing file: rejected at DB load by `R_File::where` | 0x005341e0:50-52 | it never becomes an S_Sound; throws instead |
| *not* same-group stop in the shipped data | 0x1030b380:84 | every block ships `ID = 0` (3_soundid.h:3,6,8; `235_*.txt`), and the stop needs `0 < +0x13f` |

## 2. Which globals hold "requested" vs "played": none count
There is **no counter** in sndmfc (FACT: the tick 0x10302d70 increments nothing; the only per-tick numbers are the
vector and `DAT_10315020`). State, not counters:
| address | what |
|---|---|
| 0x10315020 | voice slots (8/10/12, seeded 12) |
| 0x10315974 / 0x10315978 | begin/end of the vector of playing sounds built by the tick (`FUN_10304a60`, 0x10302d70:126) |
| 0x103159d0 | IDirectSound (NULL = no voices at all) |
| 0x103159d8 | IDirectSound3DListener (the tick's final `CommitDeferredSettings`) |
| 0x103159e8 | S_CHANNEL list (`S_Listener::getSoundOnChannel` 0x10303850) |
On the exe side the per-sound stamps are `X_Sound+0x301` (last request stamp) and `+0x375` (last audibility),
compared against `DAT_008b3d70` in `FUN_0059d4a0` (0x0059d4a0:8-18, written 0x008b3d70 in 0x0044c1b0:107).
The gate that actually decides is `+0x13e` on the S_Sound. INFERENCE: `DAT_008b3d70` is a per-level sound token
(reset to 1 on level start, 0x00451960:36,59); its exact source `FUN_0065a2f0` returns `+0x18` of a singleton and is
UNKNOWN. Reading `+0x13e` makes the meaning of `+0x301` irrelevant to the host.

## 3. Where host/dsound.c must read (exact addresses)
Hook: `IDirectSound3DListener_CommitDeferredSettings` (host/dsound.c:861). The tick calls it last, after the qsort and
after every voice was assigned: `call [ecx+0x44]` at **0x10303283**, return address **0x10303286** [bytes].
Other callers exist (`activateVoice` 0x1030ba90, `startPlaying` 0x1030b380, `setProgramFocus` 0x103036c0): filter on the
return address, or you read a half-assigned vector. Under the game's mutex, same thread as the vector writes.

```c
static void rn_snd_virtual(Ctx *c) {                     /* from CommitDeferredSettings, ret == 0x10303286 */
    uint32_t b = ld(c, 0x10315974, 4), e = ld(c, 0x10315978, 4);   /* playing-sound vector, this tick */
    uint32_t voices = ld(c, 0x10315020, 4), used = 0;
    for (uint32_t p = b; p + 4 <= e; p += 4) {
        uint32_t s = ld(c, p, 4);
        if (ld(c, s + 0x143, 4)) { used++; continue; }             /* has a voice: it went to Play */
        if (ld(c, s + 0x18d, 1) != 1) continue;                    /* not playing: nothing was asked */
        uint32_t name = ld(c, s + 0x114, 4), len = ld(c, s + 0x118, 4);  /* std::rs at +0x110 (0x1030a840) */
        rn_log("sound virtual: %.*s aud %u 3d %u prio %.2f", (int)len, (char *)G(name),
               ld(c, s + 0x13e, 1), ld(c, s + 0x147, 1), *(float *)G(s + 0x193));
    }
    if (used == voices) rn_log("sound virtual: voice pool full (%u/%u)", used, voices);
}
```
Field map (all verified in the constructors 0x10309410 / 0x10308b20 and the setters):
`+0x110` std::rs name (`+0x114` data ptr, `+0x118` length, `getFileName` 0x1030a840), `+0x124` volume,
`+0x13e` audibility, `+0x13f` group id, `+0x143` channel, `+0x147` is-3D, `+0x148` paused, `+0x14d` falloff,
`+0x18d` playing, `+0x18e` set priority, `+0x193` sort key.
"Requested but not played" per name = one line per name per tick, deduped; that is the missing #11 number.
Cross-check (already in the host): `dsound report ... peak N` tells whether the pool was full at the same time.

## 4. Who requests what, in MaxPayne.exe
| kind | entry | evidence |
|---|---|---|
| any name, random variation | `FUN_00597db0` → `SoundFactory::getSound` 0x00588320 (`rand() % count`, 0x00597db0:84) | name-based request; 3D object via 0x0059d070 → `S_SoundOmni::construct` (0x00588320:143) |
| 2D object | second factory entry 0x0058c020 → 0x0059df00 → `S_Sound::construct` (0x0059df00:19) | INFERENCE: 2D by the missing position args |
| play it | `FUN_0059d480` = `S_Sound::play` + `X_Sound+0x301 = -1` (0x0059d480:6) | only 4 callers: 0x00587980 (load/attach), 0x0058fad0, 0x00590270, itself |
| per-frame audibility | `FUN_0059d4a0` (0x0059d4a0) from the manager update `FUN_00590a60`:209, itself called by the mode audio tick `FUN_00707070`:47 | this is the on/off switch that keeps a loop virtual |
| script `A_PlaySound` | `MaxPayne_GameMode::onMessage_A_PlaySound` 0x0042c1f0 (registered 0x004578f0) | rea/map/names |
| script `A_Play3DSound` | `X_Character` 0x0069c400, `X_WeaponSet` 0x007586a0 | rea/map/names; `A_PlayFloating3DSound` 0x0069c420 / 0x007586e0; X_Projectile and X_SkinItem also register receivers (RTTI, handler not named) |
| music | 0x0058e9d0 (3 S_Sound, play at 0x0058e9d0:51), crossfade 0x0058ed70 (play :22), volume 0x0044c1b0 | always 2D, loop -1 |
| weapons | `235_weapons.txt` (29 blocks, `ID_WEAPON`, `weapons\*.wav`, `3DSound = YES`) → `X_WeaponSet` 0x007586a0 | `ID_WEAPON_PRIORITY 50`, hotspot 10 |
| footsteps | `235_characters.txt`:641+ (`Characters\Footsteps\Default_Left_01.wav`, Carpet/Wood/Gravel) → `X_Character` 0x0069c400 | `ID_PLAYERSHOES_PRIORITY 8`, falloff 8 |
| voices / speech | `235_story.txt`, `179_<character>.txt` (`ID_INGAME_SPEECH`, priority 50, falloff 30) → `A_Play3DSound` / `A_PlaySound` | 3_soundid.h:12-16 |
| ambience | `235_ambient.txt` (19 blocks `ID_LOOPING`, `Looping = YES`, 3D) | loops: the audibility stamp is what kills them |

## 5. What this does not explain (UNKNOWN / open)
- The 38 silent Plays measured 2026-11-03 were all 8-bit and 0.05-0.57 s (rea/map/sound.md, "Measured"): a data
  question, not this path; `census/sounds.tsv` should confirm whether those files are silent.
- Exact meaning of `DAT_008b3d70` (see 2): UNKNOWN, not needed for the log.
- Whether the exe also drops requests before `play` (e.g. `X_AudioImplementation` rate limiting per group): not traced;
  the group-stop path is dead in the shipped data, so it is not that.
- Native vs web: the vector is read on the guest thread, so the count is identical on both builds by construction
  (INFERENCE; the host reads guest memory either way).
