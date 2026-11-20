# Sound: how Max Payne uses DirectSound (RE of sndmfc.dll + MaxPayne.exe)

Sources: Ghidra decompiles rea/c/sndmfc.dll and rea/c/MaxPayne.exe, plus a disassembly of the thread proc.
Addresses are image addresses (sndmfc is rebased: 0x1030xxxx). "Fact" = read in code; "inference" = my reading.
Only sndmfc.dll talks to DirectSound. MaxPayne.exe, e2mfc and rlmfc never import dsound or msacm.
sndmfc imports from DSOUND.DLL only Ordinal_2 = DirectSoundEnumerateA (thunk 0x1030e4b0). It never calls DirectSoundCreate.
rlmfc imports only WINMM!timeGetTime. No mp3, no ogg, no ACM, no EAX/IKsPropertySet.

## 1. What host/dsound.c gives sndmfc, and where sound can still be lost (current to 2026-11-03)
Everything the 2026-11-02 version of this section listed as missing is done in host/dsound.c: the device enumeration answers twice (NULL
"Primary Sound Driver", then "Built-in Output" with a persistent non-NULL GUID in guest memory; the callback returns TRUE to go on, 0x10301290);
QueryInterface answers IID_IDirectSound3DListener on the primary buffer and IID_IDirectSound3DBuffer on every CTRL3D (0x10) secondary
(the 3D state lives on the buffer the mixer plays, not on the interface object: that was the "no gunshots" bug); objects and data blocks are
freed on the last Release (slot free-list, 64-entry block pool by exact size, MAXO 512 live); GetCaps/GetFormat return the real size, flags
(CTRL3D kept) and WAVEFORMATEX that acquireVoice 0x1030aad0 compares; SetFrequency takes 100..200000 Hz; 12 voices are seeded in host/win32.c.
The 3D model is min/(min+rolloff*(d-min)), silence at the max distance (flag 0x20000), pan from x/d; the listener stays at the origin.
Game-dir dsound.dll + dsound.ini (Ultimate ASI Loader for the widescreen fix, inference) are never loaded as the sound DLL.

### 1.1 The COM methods the game really calls, all implemented (two sources agree)
Source A: a Capstone pass over the whole .text of build/img/sndmfc.dll (every `call [reg+disp]`, grouped by function). Source B: the Ghidra
decompiles (rea/c/sndmfc.dll) of the same functions, and Hopper (REA, provider hopper) pseudo-code of 0x1030bb60, 0x1030b9a0, 0x1030b6e0 and
0x1030aad0. The DirectSound calls sit in only these functions: 0x10301590, 0x10301950, 0x10301a30, 0x10301f50, 0x10302830, 0x10302c90,
0x10302d70, 0x103036c0, 0x10303a50, 0x1030a140, 0x1030a390, 0x1030a460, 0x1030aad0, 0x1030ae20, 0x1030b260, 0x1030b380, 0x1030b9a0,
0x1030ba90, 0x1030bb60. The other `call [reg+0x14/0x18/0x1c/0x64]` sites (0x1030c0d0, 0x1030d050, 0x1030db50, ...) are the sound's own
virtuals and the loader stream, not DirectSound. Method lists are in section 3; the host table is the METHODS list in host/dsound.c and has
all of them. Only IIDs referenced from code: 0x1030f66c (3DBuffer), 0x1030f67c (3DListener), 0x1030f68c/0x1030f69c (CLSID/IID DirectSound).
Methods that exist in the host but do nothing (accepted and ignored; the game calls them): IDirectSound SetCooperativeLevel, Compact,
Initialize; buffer Restore (a buffer is never lost, so BUFFERLOST never comes back); listener SetVelocity, CommitDeferredSettings; 3D buffer
SetConeAngles, SetConeOrientation, SetConeOutsideVolume, SetVelocity. The apply argument (DS3D_IMMEDIATE/DEFERRED) is ignored: values count at once.
A vtable entry without a host function (a Get* of the 3D interfaces, say) stops the run with "missing API", it is never silently ignored.
Errors a method can return and the game ignores (so the sound is silently skipped): Lock -> INVALIDPARAM when the range is bad (the refill
0x1030a140 then writes nothing), SetVolume(>0 or <-10000) and SetFrequency(<100 or >200000) -> INVALIDPARAM (the old value stays), 3D
SetMin/MaxDistance(<=0) -> INVALIDPARAM. Real DirectSound refuses the same values.

### 1.2 Where a Play can give no sound: the accounting (RN_SOUNDLOG, host/dsound.c "Sound accounting")
Run with RN_SOUNDLOG=1 (web: `?env=RN_SOUNDLOG`). Every 3 s, if anything happened, four lines (five on the web):
1. `dsound report`: Play calls (secondary buffers; started = those that were not playing already), audible (an output sample above
   1 LSB of 16 bits came out of the mixer for it), silent, split by cause. Game causes: `volume off` (SetVolume -10000, 0x1030ae20: v < 0.01
   or listener muted). Host causes: `not mixed` (Stop before the 5 ms mixer pass ran), `held` (stream held because the game did not refill:
   2.8), `DS3D max distance` (the mixer's 3D gain was 0 the whole Play), `mixed zero` (the data was all zero), `under 1 LSB`.
   Also buffers playing now and the peak number playing at once (compare with the 12 voices).
2. `dsound ends`: how every Play ended, with how many of each were silent. The kind comes from the return address of Stop plus the nearest
   known caller frame above it on the guest stack: releaseVoice 0x1030bb60 called from acquireVoice 0x1030ac1b (STEAL: the game's own
   voice steal, counted apart), from the update tick 0x10302ff9, pause 0x1030b970, setAudibility 0x1030bdd5; S_Sound::stop 0x1030b9a0 called
   from the tick 0x10303065 (sound ended), S_Listener::stop, destruct, saveRaw, startPlaying 0x1030b585, startPlayingVirtual 0x1030b7b8,
   updateVirtualPosition 0x1030bd91. Host-made ends: `non-looping data ran out` (never for the game: its buffers always loop),
   `buffer Released while playing`. A sound that finishes is the game's Stop through the tick; the host cannot see the end of the data.
3. `dsound COM errors`: every method that returned something other than DS_OK, with counts and the last code (first 3 of each also logged).
4. `dsound COM calls accepted and ignored`: counts of the no-op methods listed in 1.1.
5. (web) `dsound ring`: frames mixed/taken, silence frames the page played (underruns), resyncs and the frames they skipped (the worklet jumps
   when its fill passes 6144 frames: audio dropped), frames the mixer overwrote before the page took them (page not started or AudioContext
   suspended until a click), clipped samples, and the stream holds. web/audio.js prints the same counters every 5 s.
Silent Plays are also logged one by one (first 30). Counting is always on; only the printing is behind RN_SOUNDLOG.
Not visible to the host: sounds that never got a voice ("virtual", 0x1030bc60; the game has 12 voices and sorts by priority 0x10302d70) and
sounds the game never started; those are game decisions. The peak of Plays playing at once shows whether the voice pool is the limit.

## 2. sndmfc organisation
### 2.1 Classes (all static state is in .data 0x103159xx)
- S_Listener (0x10301a90 ctor, construct 0x10301f50, destruct 0x10302830). The sound system: one static device, one
  listener object in the game (MaxPayne.exe creates it at 0x00586e10 / 0x00610e20, setActive 0x00610fc0).
  Statics: DAT_103159d0 = IDirectSound, d4 = primary buffer, d8 = IDirectSound3DListener, e0 = mutex, e4 = event,
  dc = update thread (CWinThread*), b8/e8 = lists of listeners / voice slots, 10315020 = voice count, 10315024 = rate.
- S_Device (0x10304270): 0x88-byte record: description string, GUID pointer [+0x24], DSCAPS (0x60 bytes) at +0x28.
  getFirstDevice 0x103018c0, getNextDevice 0x103018e0, getNullDeviceGUID 0x10304510 (= &DAT_10315040).
- S_Sound (ctor 0x10308b20.., construct 0x10309410, size 0x1f4+): one playing sound (2D, no position).
  Fields: [0x120] type bits, [0x124] volume, [0x147] is-3D, [0x143] -> voice slot, [0x1d2] loader,
  [0x1d6..0x1e6] WAVEFORMATEX, [0x1e8] buffer size, [0x1ec] write tracker, [0x1a2] frequency in Hz.
  Type constants (exports): STREAM = 1 (0x1030f414), MUSIC = 2 (0x1030f418), DESTROY = 8 (0x1030f41c).
- S_SoundOmni (0x1030be80 ctor, construct 0x1030c050): 3D sound; sets [0x147]=1, position, hotspot (min distance),
  falloff (max distance). Defaults in construct 0x10309410: hotspot=falloff=100.0 (0x42c80000).
- S_SoundSpot (0x1030c4b0): cone sound. MaxPayne.exe never constructs it (not in its import list).
- S_Loader (0x10306ad0 ctor, construct 0x10307120): file stream + loop/playlist state, vtable calls: +4 seek, +8 tell,
  +0xc read-into-buffer, +0x14 length. S_WaveLoader (0x1030c860, 0x1030d050 header parse, getData 0x1030dd40).
- S_Listener::setProperties (0x10303500): channels, rate, speakers, device, hwnd. Speakers value is stored and never sent to DS.

### 2.2 Startup (MaxPayne.exe)
- 0x0040b7a0: setUpdateMechanism(0 = thread), setUpdateInterval(0x3d088889 = 1/30 s), setThreadPriority(2).
- 0x0040c7b0: setProperties(voices = 8/10/12 from option "Sound Channels" 0x00404190 (values 0/1/2; .data default 1 at
  0x865f64 = 10 voices; host/win32.c seeds 2 = 12 like the startup dialog at its best, 2026-11-03), rate 0xac44 = 44100, speakers 3,
  device, hwnd = main window). Device = Null device if the sound option 0x00404400 is 0, else the default found in 1.1.
- Device test 0x10301950: CoCreateInstance(CLSID_DirectSound {47D4D946-62E8-11CF-93BC-444553540000}, NULL, 1,
  IID_IDirectSound {279AFA83-...}), Initialize(guid), SetCooperativeLevel(hwnd, 3 = DSSCL_EXCLUSIVE).
  0x10301590 does CoInitialize, enumerates, and for each device CoCreateInstance + Initialize + GetCaps(DSCAPS size 0x60).
  Real window handle is used in the final init; GetDesktopWindow only during the test.

### 2.3 Construct S_Listener (0x10301f50, lines 147-190)
1. IDirectSound::Compact.
2. CreateSoundBuffer(DSBUFFERDESC size 0x24, flags 0x11 = PRIMARYBUFFER|CTRL3D, no format) -> primary.
3. primary->SetFormat(PCM, 2 ch, 44100 Hz, avg 176400, align 4, 16 bit, cbSize 0). The primary is never Play()ed.
4. primary->QueryInterface(IID_IDirectSound3DListener) -> d8.
5. d8: SetPosition(0,0,0,defer), SetOrientation(front 0,0,1, up 0,1,0, defer), SetVelocity(0,0,0, defer).
   Later setScaleFactor(1.0) at 0x10303a50 -> SetRolloffFactor(1.0, IMMEDIATE). The listener never moves in DS.
6. Voice pool: N slots (N = voice count 8/10/12). Each slot = {owner S_Sound*, IDirectSoundBuffer*, IDirectSound3DBuffer*},
   buffer made by 0x10302c90(3D=1, the primary's format).
7. CreateMutexA(NULL, FALSE, "Remedy sound mutex") -> e0; CreateEventA(NULL, auto, nonsignaled, NULL) -> e4;
   AfxBeginThread(0x10303360) (0x10303300) and SetThreadPriority.

### 2.4 3D positioning: DS3D hardware path, with the math done by the game
- The game transforms every source position into listener space itself (matrix [listener+0x111..0x13d], 0x1030ae20) and
  sends it with IDirectSound3DBuffer::SetPosition. The DS3D listener stays at the origin facing +Z, up +Y.
- Per active 3D sound, every update, only if its dirty flag [+0x13d] is set (0x10302d70 -> 0x1030ae20):
  buffer SetVolume(vol), buffer SetFrequency(f), 3D SetPosition, SetConeOrientation, SetVelocity, SetMinDistance(hotspot),
  SetMaxDistance(falloff), SetConeAngles(a, a), SetConeOutsideVolume(0). Then listener CommitDeferredSettings (0x44).
  Doppler is off: velocity is the sound's own velocity (0 in this exe) because setDoppler is never called by the exe
  (MaxPayne.exe import list; inference for "never called").
- Volume value (2D and 3D): v = (music ? globalMusic*musicVol : globalEffects*effectsVol) * soundVolume.
  If v < 0.01 or listener muted: -10000. Else SetVolume = round(log10(v) * 10000/3) hundredths of dB (constants
  0x1030f4d4 = 10000.0, 0x1030f4d8 = 1/3, 0x1030f4dc = 0.01). So amplitude gain = v^1.667, not v. Host gain() is right.
- Frequency value: [sound+0x1a2] * pitch factors, clamped to 6000..100000 (the factors come from 0x0044ea00:
  set2DSoundFrequency / set3DSoundFrequency / setMusicFrequency; limits from "2DSoundMinimumPitchMultiplier" etc. 0x0050c3c0).
- SetPan and SetCurrentPosition-on-3D are never used. The game also drops 3D sounds beyond falloff by itself
  (distance compare in 0x10302d70 and 0x1030b380), so the DS3D max-distance mute rarely matters.
- DS3D model the host must give: distance gain = min / d for d > min (rolloff 1.0, distance factor 1.0 default), silence
  at d >= max (flag 0x20000 MUTE3DATMAXDISTANCE is set on all 3D buffers), pan from x/d, no HRTF (DS3DALG_DEFAULT, guid zero).

### 2.5 Voices and buffers (0x10302c90, 0x1030aad0)
- Buffer create 0x10302c90: DSBUFFERDESC size 0x24. dwFlags 3D = 0x300F0 (CTRL3D|FREQ|PAN|VOL|GETCURRENTPOSITION2|
  MUTE3DATMAXDISTANCE), 2D = 0x100E0 (FREQ|PAN|VOL|GETCURRENTPOSITION2). dwBufferBytes = nAvgBytesPerSec / 2, rounded up
  to a multiple of 4 (half a second). lpwfxFormat = the sound's PCM format (always tag 1, cbSize 0; ADPCM is decoded to 16-bit).
  3D buffers then QueryInterface(IID_IDirectSound3DBuffer); 2D slots store NULL.
- Giving a sound a voice (0x1030aad0): picks a free slot, else steals the lowest priority one (Stop). Then GetFormat + GetCaps on
  the slot buffer; if channels, rate, bits or the CTRL3D bit differ from the sound's, it Releases buffer and 3D iface and creates a
  new buffer. So mixed formats (22 kHz mono vs 44.1 kHz stereo, 2D vs 3D) cause constant create/release. (Finding 4.)
- A sound with no voice is "virtual": its position is estimated with a timer (0x1030bc60) and it gets a voice when it is
  audible (0x10302d70 sorts by priority and gives voices to the first N). 2D sounds start at once in play() (0x1030b8d0 ->
  0x1030b380); 3D sounds are flagged and started by the next update (<= 33 ms).

### 2.6 Streaming (every sound, music included, is a looping stream buffer)
- Start (0x1030b260): buffer GetCaps(size 0x14) -> dwBufferBytes; fill the WHOLE buffer (0x1030a140); SetCurrentPosition(0);
  Play(0, 0, DSBPLAY_LOOPING = 1). On DSERR_BUFFERLOST (0x88780096): Restore, refill, Play again.
- Refill (0x1030a140), run by the update (every <= 33 ms): GetCurrentPosition(&play, NULL); bytes = play - tracker (wrap at buffer
  size); Lock(offset = tracker [+0x1ec], bytes, &p1,&n1,&p2,&n2, 0); loader->read into p1 then p2; Unlock; tracker =
  (tracker + n1 + n2) % size. If Lock returns BUFFERLOST: Restore and Lock the whole buffer.
  The game relies on the PLAY cursor being exact enough; it never reads the write cursor in the refill. The write cursor is read
  only by setPlayingPosition 0x1030a390 (GetCurrentPosition(&play, &write)). getPlayingPosition 0x1030a460 uses (&play, NULL).
- So the host buffer is a ring of 0.5 s that the game refills behind the play cursor: 0.5 s of data is always queued
  ahead; an update gap above ~0.45 s makes an underrun.
- Stop = IDirectSoundBuffer::Stop (0x1030b9a0 stop, 0x1030bb60 releaseVoice for a steal; 0x1030b6e0 startPlayingVirtual has no Stop). End of a non-looping sound:
  the game counts bytes after the loader ran out ([sound+0x197/0x198], 0x10302d70 end) and Stops (inference on exact rule).
- Who refills: a dedicated thread. Proc 0x10303360 loop: WaitForSingleObject(event e4, timeout = ftol(interval * 1000) ms,
  interval = 1/30 s set by the exe = 33 ms; default 0.05 s at 0x10315030); on timeout (return != 0) and flag 0x103159f4 == 0
  it runs the update 0x10302d70; return == 0 (event set by 0x10303330) ends the thread. Priority = 2 (HIGHEST).
  MaxPayne.exe ALSO calls S_Listener::update (0x10304230) every frame (0x00601250 line 36 and the main loop 0x0040c020 line 143);
  it runs the same update on the game thread when the interval passed and sets 0x103159f4 so the thread skips one round.
  All DS calls are serialized by "Remedy sound mutex" (WaitForSingleObject INFINITE + ReleaseMutex), so any thread model works.
- Update (0x10302d70) also calls CommitDeferredSettings at its end and sorts voices with qsort.

### 2.7 Loops and playlists
- Repeat count: S_Sound::setRepeatCount (0x1030a6e0), 0xFFFFFFFF = forever. The exe sets it for looping sounds (0x00588320 line
  203) and for music (0x0058e9d0: loop flag 0x0040e800 ? -1 : 1). Loop start/end 0x1030a5a0 / 0x1030a5d0 and S_Loader loop count
  (0x10308300). Looping is done by the loader when it fills the buffer, not by DS (the DS buffer always loops over its 0.5 s).
- Playlist file: next to the wav, "<name>.sfl" (RIFF 'SFPL' with 'cue ', LIST 'adtl', 'ltxt', 'labl' chunks) read by
  importPlaylist 0x10307610 through R_File::where/size/load (so it comes from the archives too); exportPlaylist 0x10307b40.
  This is Sound Forge region data: regions with loop counts. If a wav has no .sfl, there is no playlist.

## 3. COM calls the game makes (vtable offset -> method)
IDirectSound (CLSID/IID above): only via CoCreateInstance (the host's dsound_cocreate handles it; DirectSoundCreate is unused).
| off | method | caller -> args |
|---|---|---|
| 0x08 | Release | 0x10301950, 0x10301a30 (test), destruct 0x10302830 |
| 0x0c | CreateSoundBuffer | 0x10301f50 primary (desc 0x24, flags 0x11); 0x10302c90 secondary (flags 0x300F0 / 0x100E0) |
| 0x10 | GetCaps | 0x10301590, DSCAPS dwSize 0x60 stored at S_Device+0x28 |
| 0x18 | SetCooperativeLevel | 0x10301950 (hwnd, 3) |
| 0x1c | Compact | 0x10301f50 |
| 0x28 | Initialize | 0x10301590, 0x10301950 (guid, NULL for Primary Sound Driver) |
Never used: QueryInterface, AddRef, DuplicateSoundBuffer, GetSpeakerConfig, SetSpeakerConfig.
IDirectSoundBuffer (primary and secondary):
| 0x00 | QueryInterface | 0x10301f50 primary -> 3DListener; 0x10302c90 secondary -> 3DBuffer |
| 0x08 | Release | 0x1030aad0 (buffer and 3D iface when replacing), destruct 0x10302830 |
| 0x0c | GetCaps | 0x1030b260, 0x1030aad0 (DSBCAPS dwSize 0x14) |
| 0x10 | GetCurrentPosition | 0x10302d70, 0x1030a460 (play only), 0x1030a390 (play + write) |
| 0x14 | GetFormat | 0x1030aad0 (size 0x12, WAVEFORMATEX) |
| 0x2c | Lock | 0x1030a140 (offset, bytes, 4 out ptrs, flags 0) |
| 0x30 | Play | 0x1030b260 (0, 0, DSBPLAY_LOOPING) |
| 0x34 | SetCurrentPosition | 0x1030b260 (0) |
| 0x38 | SetFormat | 0x10301f50 primary only |
| 0x3c | SetVolume | 0x1030ae20 (-10000..0) |
| 0x44 | SetFrequency | 0x1030ae20 (6000..100000) |
| 0x48 | Stop | 0x1030b9a0 (S_Sound::stop, call at 0x1030b9dd), 0x1030bb60 (releaseVoice, call at 0x1030bc08); 0x1030b6e0 does NOT call it (Capstone + Hopper) |
| 0x4c | Unlock | 0x1030a140 |
| 0x50 | Restore | 0x1030a140, 0x1030b260 (after BUFFERLOST 0x88780096) |
Never used: GetVolume, GetPan, GetFrequency, GetStatus, Initialize, SetPan.
IDirectSound3DListener (new, from the primary): 0x08 Release (0x10302830); 0x34 SetOrientation, 0x38 SetPosition, 0x40 SetVelocity
(0x10301f50, once); 0x3c SetRolloffFactor (0x10303a50, 1.0, apply 1); 0x44 CommitDeferredSettings (0x10302d70, 0x1030b380,
0x1030ba90, 0x103036c0 setProgramFocus). Never used: all Get*, SetAllParameters, Distance/DopplerFactor.
IDirectSound3DBuffer (new, per 3D buffer): 0x08 Release; 0x34 SetConeAngles; 0x38 SetConeOrientation; 0x3c SetConeOutsideVolume;
0x40 SetMaxDistance; 0x44 SetMinDistance; 0x4c SetPosition; 0x50 SetVelocity (all in 0x1030ae20, apply flag 1 = DS3D_IMMEDIATE).
Never used: SetMode (default NORMAL), SetAllParameters, Get*.

## 4. Files, formats, decoders (MaxPayne.exe side)
- Only RIFF WAVE. S_Sound::construct (0x10309410) throws "Unsupported audio format in %s" unless the name ends in "wav"
  (stricmp of the last 3 chars, string at 0x10315730). Header parse 0x1030d050: needs "RIFF....WAVEfmt ", format tag 1 (PCM) or 2
  (Microsoft ADPCM, 7 coefficient pairs, 'fact' chunk required); anything else: "Only PCM and ADPCM formats are supported".
- ADPCM is decoded inside sndmfc.dll (getData 0x1030dd40, coefficient tables at loader+0x1fe/0x21a, bits and bytes/s x4 -> 16-bit).
  No acm* / msacm32. The DS buffer always receives 16-bit PCM (mono or stereo, any rate in the file).
- Opening (0x1030cac0): S_Loader type STREAM (1) -> R_FileStream(path, 64 KiB buffer, rlmfc); else R_UnbufferedFileStream.
  Both go through rlmfc R_File, which also reads the x_*.ras archives. File names are relative: "wavs\max_theme.wav" (music,
  x_music.ras 287 MB), "ingame\part3\P3L6a_Max_01.wav" (speech, x_english.ras 413 MB). Names appear in data text blocks
  ("Filename = ...;" and `= "wavs\x.wav";`) in the .ras files (seen with strings; the .ras index is scrambled, not parsed here).
- Database load (SharedDB): 0x00528dc0 loads "Sounds" and "Music.txt"; sound blocks 0x00533c00 (streaming flag must be the same for
  all variations of a sound: this is the STREAM bit), 0x005341e0 (file exists, 3D sounds need Falloff and Hotspot, 2D must not),
  categories 0x00532a90, effects 0x00532fa0. Lookup: effect 0x00592300, music 0x00592a70, factory cache 0x00587dc0/0x005937d0.
- Create sounds (inference from callers): SoundFactory::getSound 0x00588320 and 0x0058c020; 2D = wrapper 0x0059d070 -> S_Sound::construct
  (type 0 or STREAM, hotspot 500.0, falloff 1000.0 defaults); 3D = wrapper 0x0059cfd0 -> S_SoundOmni::construct. After creation:
  setID, setVolume, setFrequency(sampleRate * random variation * pitch), setRepeatCount, setListenerFrequencyAffects, setPriority,
  setPlayStart(0), setAudibility(true). Random variation: rand() in 0x00588320 line 195.
- Music: 0x0058e9d0 keeps up to 3 S_Sound objects (current, next, spare) made by 0x0058ee50 (S_Sound(file, type, 1.0, 1.0, NULL, 0));
  0x0058ed70 is the per-frame cross-fade (setVolume a/b, play next). Music volume 0x0044c1b0 / UI 0x005876d0, 0x00587840.
- Voice-overs and graphic novel panels: no special path. The panel script runs `this->A_PlaySound( story, P1L1a_004 );` (OnInitMessage
  in the level text) and that goes through the same sound database and S_Sound streaming (inference: no sound call exists in the GN
  functions 0x0049cab0 / 0x004a8420 themselves). Panels are jpg (data\graphicnovel\bitmaps), not video.
- Save/load of playing sounds: 0x00587980 / 0x0058ba30 (attach, setPlayStart(position), play), 0x00587e30 / 0x0058bd90 (getPlayingPosition,
  getFrequency into a chunk). Called from 0x00454230 / 0x00454fa0.
- Master volumes: setGlobalEffectsVolume/setGlobalMusicVolume 0x0060dce0, 0x0060dd40 (from 0x006102c0 / 0x006102e0, mode changes).
- The intro movie is movies/intro.mpg (not sndmfc; audio path not checked: NOT VERIFIED).

## 5. Win32 the sound path needs (besides dsound)
CoInitialize/CoUninitialize/CoCreateInstance (ole32), GetDesktopWindow (user32), CreateMutexA (named "Remedy sound mutex"), CreateEventA (unnamed,
auto-reset), SetEvent, WaitForSingleObject (with finite timeout AND INFINITE), ReleaseMutex, CloseHandle, SetThreadPriority, MFC42 AfxBeginThread
(MFC42 is lifted guest code, so it needs CreateThread/ResumeThread). rlmfc timeGetTime for ages. No timeSetEvent in the sound path.
Host/threads.c already has these (CreateEventA rejects names, which sndmfc does not use).
NOT VERIFIED: actual sample rates/channels of the shipped wavs (archives are scrambled); the exact end-of-sound rule in 2.6; whether the sound
thread really runs concurrently with the game thread in the web build (depends on host/threads.c).

### 2.8 Long frames and streams on the web (2026-11-02)
Who refills (RE above, 2.6): the sndmfc thread (event wait 33 ms, then the update) AND the exe's per-frame S_Listener::update. On the web both are
guest threads under the GIL (host/threads.c), so while the game thread holds the GIL without an API call (host fast functions, the
sync XHR of a file read, thread creation waiting for the page) the refill cannot run. GPU waits (Present, readbacks) already release it.
The buffer is a 0.5 s ring (22050 frames at 44100 Hz), the host's play cursor lags the mix by LEAD 46 ms, so a gap above about 0.4 s means
DirectSound would loop the stale lap: audible as the music repeating (render's "5 laps in a long frame").
Fix (host/dsound.c mix, Unlock): the host tracks avail = frames of valid data ahead of the play position per buffer (Unlock adds the bytes the game
wrote, the mixer subtracts what it plays; SetCurrentPosition assumes full so a hold is never early). A looping buffer with 2+ Unlocks whose
avail drops under 2 frames is HELD: silent, play position frozen, no stale lap. The game's own refill (bytes = play - tracker) then writes
nearly the whole ring from where it stopped and the stream resumes exactly there: nothing repeats, nothing is skipped, there is a gap instead.
Telemetry: a hold counts as a starve (rn_telem.starves, "stream laps with no refill") and logs "dsound stream N: held".
Proof: forced stall (a temporary 2 s sleep inside Unlock at 60 s, not committed) -> "held" on the music stream at once, "RESUME ... avail 1" at
the next Unlock, no exception; New Game to level 1 without the stall: 0 holds, 0 whole laps (330 s, keys, port 5183).

### 2.9 What holds the GIL on the web (2026-11-02, RN_HOLD=80 + RN_FILELOG, New Game to level 1, 330 s, port 5183)
Blocking host calls (read in the code and timed): synchronous XHR in files.c (read ranges, lookup, list, save lookup, save put) and
pthread_create. Measured: block fetches 2 ms typical, 11 ms max; lookup 26 ms once (the first call fetches the file index); save
lookups 5-13 ms with one outlier of 117 ms; save put 11 ms; pthread_create never took 2 ms or more (the pool of 8 workers is pre-made,
one CreateThread in 200 s). None of them reaches the 0.4 s the stream ring can bear, and each is short next to the quantum, so the GIL is NOT
released around them (a release would let another thread enter the files table and the block cache mid-call, for no gain).
The long holds are guest compute with no API call, where gil_slice (a switch at an API call after 20 ms) never runs: boot (1005 and 653 ms
before the first two Presents, no waiter yet, harmless) and the level load. 21 holds of 80 ms or more, 10 of them in libjpeg (grphmfc
0x10204263..0x102097b7, 100-165 ms): fixed by gil_slice() at the entry of the host stand-ins called per block/row (rn_jpeg_idct, rn_jpeg_ycc,
rn_jpeg_row, rn_d3dx_convert in host/fast.c): same run now 8-9 holds, 0 in libjpeg. What is left (86-460 ms, varies run to run with the load of
this shared Mac): lifted msvcrt/e2mfc/d3d8 loops (R_FileStream ctor 0x10013ca0 chain, P_Map 0x1011f650, D3DX 0x1043xxxx). A hold of that
size is now covered by the stream hold (2.8); preempting them needs a switch point in lifted code (lift.py, flow's file).

## Open
- Clipping (RESOLVED 2026-11-02, host/dsound.c emit): render's telemetry saw a peak of 35538/32767 (|x| = 1.085) in New Game -> Fugitive at ~191 s;
  the Prologue run before it peaked at 32511 with 0 clipped. Cause: the web ring is float and had no clamp, while the native WAV path
  clamped. Every term of the mix is <= 1: the game's SetVolume is round(log10(v)*10000/3) with v <= 1 (section 4, so <= 0 hundredths of dB;
  host gain() = 10^(v/2000) is the DirectSound definition), pan only attenuates the far channel, DS3D distance gain is min/d <= 1, an 8-bit
  sample is <= 0.992. So the overshoot is the SUM of several loud voices (music + fx + voice) plus the Hermite upsample overshoot on
  22 kHz sources (1:1 buffers at 44.1 kHz are exact at t = 0). Real DirectSound sums in 32 bits and saturates to 16 bits, so retail clips
  the same way. emit() now saturates (same as the native path) and counts saturated samples in ring_hdr[7] (read by web/telemetry.js); the
  peak in the 5 s line is the post-clamp one. A soft limiter instead of the hard clip would be an improvement for the OWNER to approve.
- Mouse look on a real browser (pointer lock, unadjustedMovement) is only checked by code, not played.

## Coverage (2026-11-02, names in rea/map/names/): sndmfc.dll (whole image)
Names come from Ghidra-corpus + disassembly reading by a Sonnet worker, spot-checked by the platform slice; one source = weak, two = strong.
'understood' = body read and role stated; 'named' = name justified by class/shape only. Full table: rea/map/names/<image>.tsv (addr, name, level, confidence, summary, evidence).

| set | functions in table | named before | named now | unknown | understood (strong) | named outside the table |
|---|---|---|---|---|---|---|
| sndmfc.dll | 466 | 334 | 466 | 0 | 431 (378); 129 of them are unwind/catch funclets and 28 CRT/import thunks | 48 |

Image: build/img/sndmfc.dll, rebased to 0x10300000. Table rows: 466 (132 were FUN_*). The JSON has 514 entries:
the 466 rows plus 48 functions that are not in the table (thread proc, two qsort comparators, atexit thunks,
secondary-vtable thunks, P_BaseObject/R_Exception/R_FileStream import thunks, type_info dtor, _purecall thunk).
Levels: 479 understood, 35 named only (weak, name from shape or class; body only skimmed). No function is left without a name.

#### Big picture

sndmfc.dll is Remedy's DirectSound 8 wrapper. One S_Listener (the "device" side, also a P_BaseObject) owns a pool of
voices (S_CHANNEL). Each S_Sound is a looping 0.5 s DirectSound stream buffer that the engine refills from an
S_Loader (S_WaveLoader reads PCM or MS ADPCM). 3D math is done by the game code (listener-space matrix at listener+0x111)
and DS3D values are sent as IMMEDIATE. A tick (0x10302d70) runs on a thread (0x10303360) or from S_Listener::update.

#### Classes

##### S_Listener (RTTI .?AVS_Listener@@)
- Vtables: main 0x1030f214 (26 slots), P_UserData sub-object 0x1030f204 (at +0x10, 3 slots: dtor thunk 0x10306a80, dump, duplicate).
- Derives P_BaseObject + P_UserData. Object holds: matrix block +0x2c.., listener-space copy +0x111.., velocity +0x159,
  scale factor +0x179, mute +0x17d, doppler +0x17e, update flag +0x17f, priority formula +0x189, S_SoundList* +0x195.
- Slots: 0 scalar_deleting_dtor 0x10301ce0, 1 0x10306a90 (returns +0xe8), 2 addTo(P_BaseObject import), 3 remove, 4 enable,
  5 setPosition(xyz), 6 S_Listener::setPosition 0x10303ad0, 7..15 P_BaseObject imports (getPosition ... setCollisionMask),
  16 getObjectType 0x103011b0, 17 duplicate, 18 executeAlways 0x103033c0, 19..22 P_BaseObject (executeVisible, linkToScene, visualization,
  vertexNormalVisualization), 23 S_Listener::addTo 0x103034b0, 24 setMatrix 0x10303af0, 25 setRotation 0x10303b80.
- Static parts: setProperties 0x10303500, setUpdateMechanism 0x103035e0 (0 = thread, 1 = manual update), setProgramFocus/killProgramFocus,
  getFirstDevice/getNextDevice (device list), testOutputDeviceFunctionality, global music/effects volume.
- ctor 0x10301a90 and 0x10301d00 (copy) call construct 0x10301f50 / 0x10302640 / destruct 0x10302830.

##### S_Sound / S_SoundOmni / S_SoundSpot
- S_Sound vtable 0x1030f434 (25 slots) + sub-object vtable 0x1030f424. Differences from the listener: slot0 0x10308d40, slot6 P_BaseObject::setPosition,
  16 getObjectType 0x103011d0, 18 executeAlways 0x1030a0b0, 23 addTo 0x1030a320, 24 destroy 0x1030a070.
- S_SoundOmni vtable 0x1030f4f4 (sub 0x1030f4e4): slot0 0x1030bee0, 5/6 setPosition 0x1030c320/0x1030c300, 16 getObjectType 0x1030be50; rest as S_Sound.
- S_SoundSpot vtable 0x1030f56c (sub 0x1030f55c): slot0 0x1030c4d0, 16 getObjectType 0x1030c480; rest as Omni.
- Key fields of S_Sound: type +0x120, volume +0x124, frequency +0x128, frequency-affects flag +0x12c, id/group +0x13f, channel (S_CHANNEL*) +0x143,
  paused +0x148, playing +0x18d, internal priority float +0x193 (qsort key), listener +0x19e, loader +0x1d2, DS write size +0x1e8, last write pos +0x1ec,
  virtual position +0x1b1/+0x1b5, R_Timer +0x1b9, saved loops left +0x1ce. Omni: hotspot +0x149, falloff +0x14d, position copy +0x151, velocity +0x15d, flag +0x1f4.
  Spot: orientation +0x175, cone +0x181, ambient +0x185, flag +0x1f5.
- Voice life cycle (all under mutex 0x103159e0): startPlayingVirtual 0x1030b6e0 / startPlaying 0x1030b380 -> tick -> activateVoice 0x1030ba90
  (acquireVoice 0x1030aad0 steals the lowest priority S_CHANNEL; applyVoiceParameters 0x1030ae20; startVoicePlayback 0x1030b260 uses fillStreamBuffer 0x1030a140)
  -> releaseVoice 0x1030bb60 (turns the sound virtual) -> updateVirtualPosition 0x1030bc60 (timer driven position, loops, stop).
  computeInternalPriority 0x1030a8f0 writes the sort key.

##### S_Loader (abstract, RTTI vtable 0x1030f3bc, 6 slots) and S_WaveLoader (vtable 0x1030f5d4)
- S_Loader slots 1..5 are _purecall (setDataOffset, getDataOffset, getData, getFormatString, getWaveLength). Slot0 0x10306bc0.
- S_WaveLoader slots: 0 0x1030c880 (deleting dtor), 1 setDataOffset 0x1030db50, 2 getDataOffset 0x1030db20, 3 getData 0x1030dd40,
  4 getFormatString 0x1030e320, 5 getWaveLength 0x1030e2a0.
- Loader fields: loop start/end/count +0x144/+0x148/+0x14c, channels WORD +0x162, samples/s +0x164, avg bytes/s +0x168, bits WORD +0x16e,
  tag count +0x2c, TAG list +0x0c. Wave part: loaded flag +0x1c0, shared data (refcounted) +0x1c4, stream +0x1f0, ADPCM coefficient tables +0x1fe/+0x21a.
- Header parsing 0x1030d050 (strings "%s it not a valid WAVE file", "Only PCM and ADPCM formats are supported in %s", "%s it not in valid ADPCM format").
  ADPCM block decoder 0x1030d710. Streams: R_UnbufferedFileStream 0x1030f60c (new at 0x1030e330), R_FileStream 0x1030f5f0.
- Playlist: importPlaylist 0x10307610 reads <wave>.sfl (RIFF SFPL, cue, adtl, ltxt, labl); exportPlaylist 0x10307b40 writes it. S_Loader::TAG is 62 bytes.

##### S_Device (vtable 0x1030f370, 1 slot)
- Fields: description std::rs +4, driver name std::rs +0x14, GUID +0x24, DSCAPS 0x18 dwords at +0x28; sizeof 0x88.
- Global list: std::vector<S_Device> at 0x103159a0 (begin a4, end a8, cap ac). Filled by the DirectSoundEnumerate callback 0x10301290 from snd_enumerate_devices 0x10301590
  (adds "Null Audio Device"). The IDirectSound is created by 0x10301950.

##### Exceptions
- R_MemoryException 0x1030f378 / R_MemoryException_Allocation 0x1030f280 ("Out of system memory resources", throw helper 0x10305210, static copy 0x10315a80).
- S_SoundException 0x1030f328, +InitializationFailed 0x1030f2a4, +InvalidFormat 0x1030f49c. S_LoaderException 0x1030f64c, +InvalidFile 0x1030f628.
- Slot layout (8): GetRuntimeClass, scalar deleting dtor 0x103051f0, 0x103032e0 (ret 4), 0x10304fe0 (ret), 0x103032e0, GetErrorMessage, ReportError, getErrorMessage.

##### R_DLList template family (intrusive doubly linked list)
- Node = {prev, next, payload}; sentinels at +4 (head) and +0xc (tail), count +0x14, cached index +0x18 and node +0x1c (0x7fffffff = invalid), buffers +0x20/+0x24.
- Vtable per instantiation (3 slots: dtor, allocNode, freeNode). RTTI instantiations: S_CHANNEL 0x1030f2c8/d8, S_SoundList 0x1030f2e8/f8, S_Listener* 0x1030f308/18,
  S_Sound* 0x1030f39c/ac, char* 0x1030f3d8/0x1030f408, S_Loader::TAG 0x1030f3e8/f8. Node alloc sizes: 0x14 (S_CHANNEL), 0x18 (S_SoundList), 0xc (pointers), 0x46 (TAG).
- Generic helpers (one copy of code shared by all T): first 0x10305150, next 0x10308a80, unlinkNode 0x10306980, eraseNode 0x10306950, linkNodeBetween 0x103069b0,
  linkAtFront 0x103069e0, linkAtTail 0x103062f0, popFront 0x10306a10, getAt 0x103089a0, initSentinels 0x10306a50.
- S_CHANNEL (RTTI S_Listener::S_CHANNEL) = {owner S_Sound*, IDirectSoundBuffer*, IDirectSound3DBuffer*}, 12 bytes.

#### Globals (data section)
- 0x103159b0 listener count; 0x103159b4 R_DLList<S_Listener*>; 0x103159bc current S_Device*; 0x103159c0 HWND; 0x103159c4 default device;
  0x103159cc device enumeration index; 0x103159d0 IDirectSound*; 0x103159d8 IDirectSound3DListener*; 0x103159dc CWinThread*; 0x103159e0 mutex "Remedy sound mutex";
  0x103159e4 update event; 0x103159e8 channel list; 0x103159ec update mechanism; 0x103159f0 thread priority; 0x103159f4 manual-update flag.
- 0x10315020 channel count; 0x10315024 second property of setProperties; 0x10315028 speaker mode; 0x1031502c thread-finished flag; 0x10315030 update interval (s);
  0x10315034 global music volume; 0x10315038 global effects volume; 0x10315988 R_Timer pacing S_Listener::update.
- 0x10315a30 static pointer vector used to sort sounds; 0x10315a44 wave loader shared-data counter; 0x10315a80 static allocation exception.
- CRT: onexit table 0x10315b2c/0x10315b30, initterm range 0x10315000..0x10315014.

#### Functions that stay uncertain (named, but not fully understood)
- 0x10306a90 (vtable slot 1 returns +0xe8): the meaning of that P_BaseObject field is unknown.
- 0x1030a8f0: formula body read only for its inputs (priority fields, distance, age); exact weights not written down.
- 0x1030ae20 and 0x1030b380: long parameter code, only the structure (volume, frequency, 3D, distance test) is described.
- 0x10302d70: the tick; role clear, per-step order not documented.
- 35 entries marked "named" (weak): S_Listener pause/resume/stop family, simple S_Sound getters/setters (priority, loop start/end, id), enumTagsPassed/enumAllTags,
  S_Sound::attach/destruct/saveRaw, S_WaveLoader::getData/setDataOffset (ADPCM paths), testOutputDeviceFunctionality, setDoppler.
- Template internals named weakly (shape only, class instance not proven): snd_*vector*, snd_copy_*_devices, snd_dllist_pair_*, M_Matrix4x3Template ctor, identity matrix getters.

#### Contradictions with rea/map/sound.md
None found. Additions: update-thread proc 0x10303360, the two qsort comparators, device enumeration callback 0x10301290, DS buffer creation 0x10302c90
(flags 0x300f0, size = avg bytes/s / 2), start playback 0x1030b260, S_Loader being abstract (slots 1..5 pure virtual).

## Measured 2026-11-03 (build/snd1, web, part1_level1, 420 s of real combat, machine load about 17)
Play 856: audible 813, playing at the end 5, silent 38 (mixed zero 33, stopped before the mixer ran 5). Peak 10 voices of 12; no steal in the ends list.
Web ring: 3 resyncs skipped 40559 frames (0.2%, at 120, 145, 265 s), 30477 overwritten before the page started, clipped 6320. No COM errors, streams held 0.
So voice count and host drops do NOT explain "incomplete sound". All silent Plays are 8-bit, 0.05-0.57 s (are those files silent? census/sounds.tsv). Not measured: sounds the game
keeps virtual and never sends to Play (0x1030bc60), so #11 is not done.
