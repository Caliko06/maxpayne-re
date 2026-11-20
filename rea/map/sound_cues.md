# Short cues and bullet-time pitch (#11, 2026-11-09)

## Correction to the earlier conclusion

The earlier "game-side culling" explanation was not established. A Stop 50-80 ms after Play does not explain why a
DirectSound buffer never produced audible data. The original host can report a false lap before the sound advances.

FACT (original DLL, Ghidra corpus plus Capstone in out/sound_1109/tick_asm.txt):
- Start 0x1030b260 fills the whole half-second ring via 0x1030a140, sets position zero, then Play(LOOPING).
- Tick 0x10302e78 reads the PLAY cursor; 0x10302e7b..0x10302e9b computes `(play - tracker) mod size` and refills it.
- EOF refills increment sound+0x198; 0x10303057..0x10303065 stops when that count exceeds the ring size.
- releaseVoice 0x1030bb60 queries the playing position then Stops; S_Sound::stop 0x1030b9a0 calls Stop directly.
  GetStatus is not the EOF decision in this tick. The 8-bit conversion `(byte-128)/128` is correct.

FACT (host regression): the original cursor subtracts `min(ahead,pframes)*current_step` from an accumulated ring
position, then wraps any negative result. At 32000 Hz, 788 output frames can leave -1.02e-12, reported as byte 15999
of 16000. Changing frequency also moves the reported position backward (5165 -> 4214 in the fixture). The DLL
interprets either backward move as nearly a full lap and replaces still-pending PCM with the loader's tail/EOF.
INFERENCE: this explains the prior silent Plays with roughly two rings written in 50-80 ms; the failed browser
runs below did not observe these three cues through the original caller, so that particular runtime link remains open.

Patch in host/dsound.c: track unwrapped mixed source progress and the last reported play position; clamp the latter
monotonically until SetCurrentPosition explicitly resets it. Wrap only at the API boundary. Publish `mixed` and buffer
positions under the same mutex and sample the clock once. Keep the existing PCM conversion, ring mixer and game code.
RN_SOUNDLOG now gives bounded start-data, initial-cursor and per-Play peak/frame evidence for the three cues.
Prior art consulted: [Wine's committed-buffer discussion](https://www.winehq.org/pipermail/wine-devel/2021-September/195929.html).

## Evidence and limits

`out/sound_1109/mixer_check.c` includes the actual host/dsound.c, uses the three original WAVs, invokes its mixer and
GetCurrentPosition, and emulates the DLL's EOF refill behind that cursor. No game code runs in this fixture.
The same fixture against HEAD's original dsound.c fails for all three cues with a backward cursor:
`out/sound_1109/mixer_check_before.log`. The patch passes: `out/sound_1109/mixer_check.log`.

| Cue | Mixed frames | Input/output peak |
|---|---:|---:|
| default_hit_01 | 15435 | 0.773438 |
| metal_hit_01 | 15435 | 1.050975 (Hermite overshoot, before output clipping) |
| shell_ricochet_01 | 15435 | 0.242188 |

Web build passed (`build_final.log`, RN_BR=0 RN_JOBS=2). Two headless muted runs used only port 5192:
1. `before/page.log`: setup deadlock, pending player commands prevented the menu-load mailbox from running; terminated.
2. `after/page.log`, `after/report.json`: loaded part1_level1; the first scripted A_Play3DSound failed to parse,
   followed by STOP / DirectDrawError at 64.1 s. `end_1000.png` inspected. This is not a successful gameplay/audio check.
UNKNOWN: actual three-cue output during browser gameplay, listening quality, Safari, real scale-0.2 frequency output.
No third game run was attempted. Assets and fixtures remain private, ignored under out/sound_1109/.

## Pitch: no game patch

FACT: S_Sound::setFrequency 0x1030a7c0 stores +0x128, clamps negative values to zero, and sets the dirty byte +0x13d.
0x1030af24..0x1030af51 selects listener +0x171 for 3D and multiplies source Hz, sound +0x128 and listener factor;
0x1030af5c..0x1030af98 clamps to 6000..100000 Hz and calls DirectSound SetFrequency. See pitch_asm.txt and pitch_push_asm.txt.
The exe's 0x44ea73..0x44ea9b computes `0.15 + 0.85*scale`: scale 0.2 gives 0.32, scale 0.5 gives 0.575.
FACT in after/page.log: 37.5 s SetFrequency 12678/22050 = 0.574966, with S snapshots at scale 0.500 (37.6..42.2 s).
The isolated mixer accepts 14112/44100 and 10240/32000 = 0.320000. This is host coverage, not a game-produced 0.2-scale measurement.
