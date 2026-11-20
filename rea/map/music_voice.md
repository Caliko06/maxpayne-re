# Música y voz: cómo suenan y dónde las perdemos (2026-11-04)

Fuentes: `rea/c/<image>/*.c` (Ghidra), `tools/ras.py` sobre `~/Games/maxpayne/x_music.ras` / `x_english.ras` / `x_data.ras`,
`host/dsound.c`, `host/files.c`. **FACT** = leído en código o datos. **INFERENCE** = mi lectura. **UNKNOWN** = no probado.

## 1. Dónde se pide la música

- El `.ldb` lleva el texto del script compilado. `maxpayne_gamemode->a_playmusic(<bloque>)` aparece 351 veces en los 25 `.ldb`
  (ej. `build/data/lvl/1_Part1_Level1.ldb` off 0x1a30b98, bloque DO `::exit02::cam_dummy`; la de 0x1a53a11 está en `::startroom::start_train`). FACT.
- Nombre validado al parsear: checkSyntax 0x42c200 → 0x592a70, que lanza `"Music '%s' not found from database"` (string 0x885c40).
  Un nombre con typo revienta la carga del nivel; no queda en silencio. FACT.
- Runtime: `onMessage_A_PlayMusic` 0x42c210 (`mov eax,[esp+4]; push eax; add ecx,0x16`) llama a 0x58e8e0 en 0x42c219 [bytes]. FACT.
- 0x58e8e0 arma la petición con el nombre del mensaje (+0x4e, getter 0x59ea30) y un float (+0x6e, 0x58f5f0), y entra al setter. FACT.
- Al empezar nivel, globalconfig.txt `[gamemode] LevelOnInitMessages` manda `a_playmusic( "" )` (parar) y
  `a_playsound( story, empty )`; el script del nivel pone el tema. FACT.

## 2. El selector (S_Sound 0x58e9d0, `ret 8`, this en ecx) [bytes]

- Gestor embebido en el modo: el modo lo actualiza en `mode+0x57` (0x0060de10: `FUN_0058ed70(param_1 + 0x57)`). FACT.
- Campos: +9 actual, +0xd siguiente, +0x11 repuesto (`S_Sound*`), +0x21 `std::string` con el nombre vigente, +0x15/+0x19 fade
  de cada uno, +0x1d progreso. FACT (prologo: `8d 5e 21` = `lea ebx,[this+0x21]`).
- **Si el nombre pedido == el vigente, return inmediato** (compare inicial). Por eso los `.ldb` repiten `a_playmusic(old_building)`
  sin reiniciar el tema: el cambio de música por sala no corta. FACT.
- Nombre `""` → destruye los 3 S_Sound (Stop DS). FACT.
- Sin actual: crea el tema (0x58ee50 → ctor `S_Sound(ruta, tipo, 1.0, 1.0, NULL, 0)`), `setRepeatCount(loop ? -1 : 1)`
  (flag global 0x40e800), `setVolume(1.0)`, `play()`. FACT.
- Con actual y sin siguiente: crea el siguiente a volumen 0 y guarda su fade → arranca el crossfade. FACT.
- Con los 3 ocupados: borra el repuesto y lo sustituye. Cola máxima = actual + 2 pendientes; un 4º cambio seguido se pierde
  (nivel 4 pide 26 temas). FACT (estructura), INFERENCE (efecto audible).

## 3. Crossfade y parada

- 0x58ed70, una vez por frame desde 0x60de10: `p = progreso / fadeTime` clamp 1; `setVolume(actual, 1-p)` +
  `setVolume(siguiente, p)`; si el siguiente no está sonando, `play()`. `progreso += R_Timer::sample (0x7269f0)`. FACT.
- Al llegar a 1: libera el actual, actual=siguiente, siguiente=repuesto, progreso=0. FACT.
- fadeTime sale de 0x441e10 (`*(float*)(this+0x24)`) al crear el tema. Coincide con `FadeInTime` de music.txt (1.0 música,
  2.0 ambiente, 0.5 `max_payne_short`) → INFERENCE sobre qué objeto es `this`.
- No hay fade de DirectSound: el juego reescribe el volumen cada frame sobre un buffer de 0.5 s que sigue sonando. FACT.
- Los fades reales son pistas: `a_playmusic(fadeout4s)` cruza a `empty.wav`, que es silencio de **10.0 s**
  (883 668 B = 8 bit estéreo 44.1 k). `fadeout1s/2s/4s` solo cambian el tiempo de cruce. FACT (music.txt + tamaño).
- Parada brusca = `a_playmusic("")` → destructor de los 3 S_Sound, sin fade. FACT.

## 4. `x_music.ras`: 30 entradas (29 wav + music.txt), 286.8 MB, todas method 3 (stored)

- 8 bit **estéreo 44.1 kHz PCM** (tag 1), no ADPCM: `max_theme.wav` 19 773 676 B, `graphic_novel.wav` 18 173 380 B,
  `byzantine_power_game.wav` 15 355 556 B. El comentario "44 kHz ADPCM" de music.txt está obsoleto. FACT (header leído).
- music.txt define 14 temas (`max_payne`, `just_forget`, `whack_him`, `mr_big`, `men_in_blue`, `fallen_angels`, `byzantine`,
  `ms_valkyr`, `killer_suits`, `graphic_novel`, `nightmare`, ...), 17 ambientes `noise\*` (`docks`, `subway_station`, `garage`,
  `steelmill_ambience`, ...) y los `fadeout*`/`empty` de relleno. `the_godfather` y `closer_to_heaven` apuntan a `graphic_novel.wav`. FACT.
- Los nombres de music.txt traen carpeta (`wavs\`, `noise\`) pero el archivo guarda el nombre pelado (`max_theme.wav`).
  `R_File::searchFile` (rlmfc 0x1000fe60) corta en el último `\` (bucle `while (pcVar14[iVar12] != '\\')`) y busca por hoja. FACT.
- Ningún archivo `.sfl` (playlist Sound Forge) en x_data, x_english ni x_music → `importPlaylist` 0x10307610 nunca encuentra nada.
  Los loop points son el archivo completo. FACT.

## 5. Narración y líneas (x_english.ras: 1323 archivos, 790 wav, 350.9 MB de wav)

- Cómic: cada página trae su VO en `[OnInitMessage]`: `this->A_PlaySound( story, P1L1a_002 );` (graphicnovelpages.txt, 210 páginas,
  166 bloques en story.txt). Es un `S_Sound` 2D normal, `ID_PLAYER_2D_SPEECH` (volumen 0.95), Streamed, Priority 100. FACT.
- La página **no** espera al audio: avanza por su timer (0x4a8b50) o por clic/tecla; pasar página a mano NO dispara OnInit
  (0x49c1f0), solo 0x4a8f40 al dar Play. graphnovel.md 1 y 3. FACT.
- Limpieza 0x58bf30 cada frame: destruye los `S_Sound` del modo con `isSoundPlaying() == false`. Si el host dijera "false" antes
  de tiempo, la voz se corta al instante. FACT.
- Música del cómic: existe el receptor `PlayMusic` 0x49e350, pero quién arranca `graphic_novel.wav`: UNKNOWN.
- En juego: `maxpayne_gamemode->A_PlaySound(<banco>, <bloque>)` (2D) y `->A_Play3DSound(dynamic, <bloque>, "")` (3D en el objeto).
  Banco = txt de x_data d235 (`subway.txt`, `hotel.txt`, `characters.txt`, ...); ej. `A_PlaySound(subway, P1L1a_Max_01)`,
  `A_PlaySound(characters, get_painkillers)`. FACT.
- Bloques con variantes: varios `[Sound]` dentro del bloque (`get_painkillers` tiene 2); se elige con `rand() % count` en
  `SoundFactory::getSound` 0x00588320. FACT.
- Voces 8 bit mono 22 050 (`P1L1a_Max_01.wav` 114 400 B = 5.19 s) o 8 bit estéreo 44.1 k (`P1L0a_001` 586 412 B = 6.65 s). FACT.
- Voces, disparos y pasos compiten por los mismos 12 canales: prioridad 100 (VO cómic) / 50 (habla) contra 8 (pasos) y 0 (clips),
  con el desempate por distancia (0x1030a8f0). FACT.

## 6. `host/dsound.c`: dónde se pierde o se corta música/voz

1. **El anillo es de 0.5 s** (`nAvgBytesPerSec/2`, dsound.c:635-640) y el mixer lleva LEAD 2048 frames (dsound.c:19). Un parón de
   guest > ~0.45 s dispara el **hold** (dsound.c:433): silencio, sin repetir la vuelta vieja. Un parón de 0.5 s en medio de una
   línea de 6 s = un hueco de 0.5 s en la voz. Probado con un stall forzado (sound.md 2.8); medido 0 holds en una partida normal. FACT.
2. El hold exige `unlocks >= 2` (dsound.c:433). Un loop que el juego llena una sola vez nunca se retiene: repetiría la vuelta rancia
   para siempre. No aplica al juego (0x1030b260 rellena entero y luego Play). INFERENCE.
3. `avail` nace 0 y solo sube en Unlock (dsound.c:791) o SetCurrentPosition (dsound.c:834, asume lleno). Un Play sin Unlock
   mezclaría el relleno 0x80 (silencio) sin hold. El juego siempre rellena antes de Play (0x1030b260). FACT.
4. `Lock` rechaza `off >= size` o `n > size` (dsound.c:778) → el refill no escribe y el stream muere. sndmfc pide
   `bytes = (play - tracker) mod size` (0x1030a140), que solo puede pasar de `size` si el cursor de play retrocede; el host lo
   congela (`ahead` clampado en 0, dsound.c:699) pero no lo invierte → UNKNOWN, probablemente imposible.
5. Si el **mixer** (pthread propio, dsound.c:521-540) se atora, `ahead` = 0, el play cursor se congela en `pos`, el juego pide
   `bytes = 0` y no rellena: silencio mientras dure, sin envolver datos. FACT (código). En la web el mixer corre fuera del GIL, así
   que el caso real es el opuesto (punto 1).
6. Loop: `o->looping` viene de `DSBPLAY_LOOPING`; sndmfc siempre pasa 1 (`call [buf+0x30](0,0,1)`, 0x1030b260). El host envuelve
   el anillo completo (dsound.c:456-458) y **no conoce loopStart/loopEnd** (0x1030a5a0/0x1030a5d0). Hoy da igual (sin `.sfl`,
   punto 4.3); con un `.sfl` de mod, el host ignoraría el loop. UNKNOWN el impacto, FACT la carencia.
7. Formatos aceptados: solo tag 1, 8/16 bits, 1-2 canales, 100..200 000 Hz (dsound.c:615). sndmfc ya decodifica ADPCM a 16 bits
   (0x1030dd40). Un buffer tag 2 llegaría como DSERR_BADFORMAT (dsound.c:636) y el sonido no sonaría nunca. FACT.
8. 8 bit: `sample()` hace `(p-128)/128` (dsound.c:379), la convención WAV sin signo. Correcto para los 1360 wavs del juego. FACT.
9. 22 050 Hz se remuestrea con Hermite (dsound.c:390-396): sobrepico al alza en fuentes 8 bit, que satura (dsound.c:342) y se cuenta
   en `ring_hdr[7]`. No corta; el canal de voz sí puede sonar "áspero" por el filtro. INFERENCE.
10. Robo de voz: `acquireVoice` (0x1030aad0) hace Release del buffer si cambia el formato; `release_slot` lo para en seco
    (dsound.c:299) y su bloque vuelve al pool, que el siguiente CreateSoundBuffer reutiliza y **borra** (dsound.c:645). Entre
    Release y el siguiente Unlock hay silencio. En el juego solo pasa con sonidos que aún no tenían voz → inaudible. INFERENCE.
11. `SetVolume` en el buffer primario devuelve CONTROLUNAVAIL porque sus flags 0x11 no traen CTRLVOLUME (dsound.c:731): `master`
    se queda en 0 y el volumen global no existe. sndmfc nunca lo toca (solo 0x1030ae20 por sonido). FACT; si algún día lo tocara,
    la música no bajaría.
12. `SetFrequency` con valor < 100 devuelve INVALIDPARAM y **deja el anterior** (dsound.c:750): un pitch imposible suena al rate
    viejo, no en silencio. sndmfc clampa a 6000..100 000 antes. FACT.
13. Entradas de vtable sin función host matan la corrida con "missing API": todos los `Get*` de las 3D, `SetAllParameters`,
    `SetDistanceFactor/SetDopplerFactor` (dsound.c:35-41 vs tabla 165-184). sndmfc no las llama (sound.md 3); un ASI/DLL ajeno
    abortaría la partida. FACT.
14. 3D: `min_d`/`max_d` valen 1 / 1e9 al nacer el buffer y solo el primer apply los pone (dsound.c:274). `activateVoice` aplica
    parámetros **antes** de Play (0x1030ba90), así que no hay un tick con gain 1 centrado. INFERENCE.
    Pan = proyección `x/d` (dsound.c:406), no la ley seno de DS3D: imagen estéreo distinta, nunca silencio. FACT/INFERENCE.
15. Música y voz son 2D (`3Dsound = NO`): no pasan por gain3d. El mute por `MUTE3DATMAXDISTANCE` (dsound.c:405) solo afecta a
    efectos 3D, y el juego ya descarta por falloff en 0x10302d70. FACT.

## 7. `host/files.c`: dónde se pierde o se corta

1. Los sonidos con `Streamed = YES` (música y toda la voz) abren `R_UnbufferedFileStream` con buffer de 8 KiB (files.md 3).
   Cada lectura < BLOCK (256 KiB) se sirve de una caché LRU de 640 bloques = **160 MB** (files.c:15-16, 357-373). FACT.
2. **Riesgo de thrash**: un tema de 20 MB ocupa 80 bloques; un `.ldb` son 15-32 MB en crudo y los jpg se decodifican por bloques.
   Si el conjunto vivo pasa de 160 MB, un bloque de música se desaloja entre dos lecturas de 8 KiB → un re-fetch de 256 KiB con
   XHR síncrono y el GIL tomado → hueco en el refill → el hold del punto 6.1. UNKNOWN (no medido); el orden de magnitud lo hace
   plausible en nivel 4-5 con voz + 2 temas. Verificar con `RN_FILELOG` y el contador de resyncs.
3. **Ruta OPFS**: con `RN_FILES` (web/index.html:149) los `blob:` ignoran el header `Range` → status 200 → `web_read` corta con
   `new Uint8Array(x.response, off, n)` **sobre el archivo entero** (files.c:141-152). Un bloque de 256 KiB de un tema de 20 MB
   copia 20 MB; por tema son 80 × 20 MB ≈ 1.6 GB de copia. Unos ms por llamada en SSD (INFERENCE, no medido); en disco lento es
   un stall directo a hueco de audio.
4. `tools/serve.py` sí contesta 206 con `Content-Range` (serve.py:129-146), así que la ruta HTTP normal está bien; el 200 solo
   aparece con OPFS u otro servidor.
5. Lectura corta: si el bloque es más chico que el tamaño del índice, `gf_read` devuelve `done ? done : -1` (files.c:395) → la
   lectura del loader falla a mitad del stream → el `S_Sound` termina antes de tiempo (o excepción "not a valid WAVE file" si pasa
   en la cabecera). Solo si el archivo cambió bajo el índice. FACT.
6. `MAXF 256` handles abiertos (files.c:12): 12 voces + nivel + saves quedan muy por debajo. No es límite. FACT.
7. Nativo: `fseeko` + `fread`, sin reintento (files.c:403-404). Un error transitorio termina el stream. FACT.
8. `gf_open` de un save lee el archivo entero en memoria (files.c:321-324); es la única ruta que hace eso y no toca el audio. FACT.

## 8. Lo que no está probado

- Que el thrash de la caché de 160 MB ocurra de verdad en una partida larga (7.2): se mide con `RN_FILELOG` + `dsound ring`.
- Impacto audible del remuestreo Hermite en voces de 22 050 Hz (6.9): comparar un wav extraído contra el WAV grabado con `RN_WAV`.
- Quién arranca `graphic_novel.wav` en el cómic (5.4) y si el `.kf2` de cada página dura al menos lo que su VO (graphnovel.md lo
  deja en NOT VERIFIED; medido en runtime: página y voz terminan juntas, 6.26/30.10/23.83 s).
- La cuenta exacta de "pedidas y nunca sonadas" sigue siendo la deuda de sound_virtual.md §3: el hook ya existe en el host
  (dsound.c:871-897) pero no hay corrida con `RN_SOUNDLOG=1` archivada en `out/`.
