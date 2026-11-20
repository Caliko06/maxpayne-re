# Enemigos de la Parte I (datos de archivo)

8 tipos de personaje aparecen como enemigos armados en los 9 niveles `1_Part1_*.ldb`: 256 instancias.
Todo lo de abajo sale de `build/data/x_data` y `build/data/lvl`. Tabla completa con fuente por valor: `out/bec/enemies.tsv` (68 columnas).

## Método
- Tipo de cada instancia: cada registro del `.ldb` es `nombre_instancia` + transformación + `nombre_del_tipo` + mensajes. Se leyó hacia adelante desde cada nombre `::sala::eN` hasta el siguiente token que es un tipo (`C*_*`). Fuente: el propio `.ldb`.
- Números de IA y vida: bloque `[AI]` y `[Properties]` de `179_<tipo>.txt`, con las macros resueltas en `3_ai_ranges.h`, `3_characters.h`, `3_object_lods.h`.
- Armas: `C_PickupWeapon(w)` / `C_PickupAmmo(w,n)` del script del nivel; `C_RemoveAllWeapons(x)` = quita todo **menos** `x` (`rea/map/gameplay.md:237`), `empty` = se queda sin armas.
- Los `.ldb` son binarios: la fuente de esos valores es `archivo@0xOFFSET`, no `archivo:línea`.

## Tabla 1 — un renglón por tipo

| tipo (`179_*.txt`) | descripción | modelo LOD0 | vida / herido | mult. cadencia | IA | armas (inicial → keep / pickup) | voz |
|---|---|---|---|---|---|---|---|
| C1_All_Johnny | Doe Johnny (mobster flaco) | doe_johnny_l0.kfs/.skd | 15 / 10 (clase 1) | 0.10 | mobster | beretta (OnInit) → keep beretta, deagle, berettadual, pump, sawed, ingram, ingramdual, leadpipe, empty · pick pump, sawed, deagle, grenade, ingram, molotov, ingramdual | mobster + `whistle`; muerte por defecto del esqueleto |
| C1_All_Mickey | Doe Mickey | doe_mickey_l0.kfs/.skd | 15 / 10 (clase 1) | 0.10 | mobster | beretta → keep deagle, pump, berettadual, beretta, sawed, ingram, empty · pick grenade, pump, deagle, sawed | mobster (death propio) |
| C2_All_Buck | DiCola Buck | dicola_buck_l0.kfs/.skd | 20 / 9 (clase 2) | 0.20 | mobster | beretta → keep beretta, berettadual, deagle, pump, ingram, sawed, ingramdual, empty · pick pump(18), deagle, ingram, sawed, berettadual, grenade | mobster; muerte por defecto |
| C2_Out_Dyson | DiCola Dyson (encapuchado) | dicola_dyson_l0.kfs/.skd | 20 / 9 (clase 2) | 0.20 | mobster | beretta → keep deagle, berettadual, beretta, pump, sawed, ingram, ingramdual, empty · pick pump, deagle, grenade | mobster (death propio) |
| C3_In_Junkie | Valhalla Spud (junkie) | valhalla_spud_l0.kfs/.skd | 25 / 8 (clase 3) | 0.30 | junkie | beretta → keep beretta, empty · pick beretta (+ `c_setinfiniteammo(beretta,true)` en L2, L3, L3b); en L1 los junkie1-3 van sin armas e inmortales (intro) | junkie (alert, noise, talk, gethit, death) |
| C3_In_Luciano | DiCola Luciano (ítalo) | dicola_luciano_l0.kfs/.skd | 25 / 8 (clase 3) | 0.30 | mobster | beretta → keep pump, beretta, empty · pick pump, beretta (+ammo leadpipe) | mobster; muerte por defecto |
| C4_In_Ray | DiCola Ray (gordo) | dicola_ray_l0.kfs/.skd | 30 / 7 (clase 4) | 0.40 | mobster | beretta → keep pump(1) · pick pump(1); el resto se queda con la beretta | mobster; muerte por defecto |
| C3_All_Martin | DiCola Martin (gabardina) | dicola_martin_l0.kfs/.skd | 25 / 8 (clase 3) | 0.30 | mobster | beretta → keep ingram, beretta, sawed, berettadual · pick pump(7), sawed(5), ingram(5), molotov(2), deagle, grenade | mobster; muerte por defecto |

Instancias por nivel (`L1` … `L6`): Johnny 57 (1:4 1b:7 2:9 2b:6 3:7 3b:7 5:13 6:4) · Mickey 33 (1:5 1b:7 3:3 3b:8 4:10) · Buck 82 (1:7 1b:10 2:7 2b:5 3:10 3b:8 4:13 5:20 6:2) · Dyson 19 (1:3 1b:5 4:11) · Junkie 15 (1:1 2:3 3:4 3b:7) · Luciano 19 (2:9 2b:10) · Ray 11 (2:6 2b:5) · Martin 20 (5:6 6:14).

## Tabla 2 — parámetros de IA (idénticos por clase)

| parámetro | mobster (7 tipos) | junkie (1 tipo) | fuente |
|---|---|---|---|
| AimingSpeed | 10 | 8 | 3_ai_ranges.h:62 / 43 |
| AimingSpeedCone | 30 | 25 | :65 / :44 |
| AimUpDownSpeed | 30.0 | 30.0 | 179_*.txt:15 (literal) |
| TurnLeftRightSpeed | 520 | 80 | :92 / :53 |
| VisualPerceivingRadius | 50 | 20 | :83 / :50 |
| GeneralPerceivingRadius | 2 | 2 | :80 / :49 |
| ActivationReactionTime | 0.0 | 0.0 | :86 / :51 |
| ShootingCone | 120 | 170 | :89 / :52 |
| PerceivingGroupOneRadius | 15 | 15 | :68 / :45 |
| PerceivingGroupTwoRadius | 4 | 4 | :71 / :46 |
| PerceivingGroupThreeRadius | 0 | 0 | :74 / :47 |
| EnemyInterestTime | 30 | 10 | :77 / :48 |
| ActivateOtherCharactersRadius | 0 | 0 | :95 / :25 (literal) |

Grupos = el jugador disparando / corriendo o en strafe / asomándose (`3_ai_ranges.h:63-74`). Cada tipo apunta a su macro en `179_<tipo>.txt:13-25` (columna `*_src` del TSV).

## Qué campos hay y qué no
Existe: vida, vida herida, multiplicador de cadencia, radio visual, radio general, cono de disparo, tres radios de percepción por grupo, tiempo de interés, tiempo de reacción, velocidad de giro, velocidad y cono de apuntado, armas, texturas (5 sets; Ray y Martin 3), sonidos.

No existe en los archivos:
- **accuracy**: no hay campo de precisión de IA. La dispersión es del arma (`rea/map/weapons.md:30`, beretta Accuracy 100; `weapons.md:51` la fórmula). Ningún `[AI]` la declara.
- **cover**: no hay sistema de cobertura ni puntos de cobertura; el AI solo se agacha en el sitio (`rea/map/ai_behaviour.md:201`).
- **reaction**: 0.0 en las dos clases, o sea sin retardo de activación.
- **overrides por nivel**: los `.ldb` no traen números de IA (`strings -n 4 1_Part1_Level1.ldb | grep -icE "MaximumHealth|AimingSpeed|VisualPerceivingRadius"` = 0); el `179_*.txt` es la única fuente.
- La máquina de estados de cada instancia no está en el personaje: la pone el script del nivel (`C_SetStateMachine`); la lista por tipo está en el TSV (`state_machines`).

Escala por dificultad (`1_difficulty.txt`): `EnemyHealthMultiplier` Fugitive 0.25/0.8/1.2 (:29,:39,:49), Hard-Boiled 1.3 (:81,:91,:101), DOA 1.4 (:135,:145,:155), NYM 1.0 (:189,:199,:209). La vida de la tabla es la base.

## Sonidos
- Mobster: `mobster_alert`, `mobster_group_alert`, `mobster_alert_known`, `mobster_group_alert_known`, `mobster_noise`, `mobster_group_noise`, `mobster_gethit`, `mobster_death` (`235_enemy.txt:995,1198,1373,1437,1500,1577,1710,1871`), wavs en `enemy\mobster\*.wav`. Johnny además usa `whistle` (:950).
- Junkie: `junkie_alert`, `junkie_noise`, `junkie_talk`, `junkie_gethit`, `junkie_death` (`235_enemy.txt:9,213,304,507,556`), wavs en `enemy\junkie\*.wav`.
- Todos comparten `characters/death_medium` en el frame del impacto al suelo, pero ese grupo **no está definido** en el `x_data` extraído (no aparece en `235_characters.txt` ni en `235_enemy.txt`). Sin resolver.

## Otros personajes con instancias en la Parte I (no son tipos de enemigo)
| tipo | instancias | papel según el script |
|---|---|---|
| transit_cop | 5 (L1) | cadáveres y el cop de `::r01::helper` (aliado, `C_SetHealth(0.01)`, noqueado) |
| nypd_cop | 4 (L3) | policías de escena: `C_SetInvulnerable(true)`, `C_SetIdle(2)`, sin armas |
| dawn_candy | 1 (L2b) | `::b02::enemy02`, recibe beretta y `C_SetStateMachine(crouch)` |
| gognitti_vinnie | 6 (3b:1, L4:5) | guion: `C_SetInvulnerable(true)`, huellas y `C_GoTo`, no combate |

## Huecos
- `::b02::enemy02` (dawn_candy) sí recibe arma y máquina; no lo clasifiqué como tipo de enemigo porque aparece una sola vez y con `C_SetStateMachine(crouch)`. Si el OWNER lo quiere dentro, es una fila más.
- No revisé `x_level*.ras` (scripts fuera del `.ldb`): si algo cambia armas en runtime, no está aquí.
- El grupo de sonido `characters` (death_medium) no está en los `.txt` extraídos.
