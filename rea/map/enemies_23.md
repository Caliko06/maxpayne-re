# Enemigos de las Partes II y III (datos de archivo)

15 niveles `1_Part2_*` / `1_Part3_*.ldb`: **424 instancias de enemigo armado** en 19 tipos (13 nuevos de II/III + 6 reusados de la Parte I) más 57 instancias de otros personajes. Todo sale de `build/data/lvl/*.ldb` y `build/data/x_data`; tabla completa (68 columnas, fuente por valor): `out/bec/enemies_23.tsv`.

## Método
- Tipo de cada instancia: el `.ldb` guarda un registro por objeto: `\r\x14<len>` + nombre `::sala::eN`, `\r\x14\x00`, `\x1a` + 48 bytes + `\x1a` + 48 bytes (dos copias de la transformación, 12 floats cada una), `\x14<byte>\r\x14\x00\r\x14<len>` + **nombre del tipo**. Se exige esa firma completa, así que el texto de script que también menciona `::sala::eN->...` no se cuenta. Fuente: el propio `.ldb`, dirección `archivo@0xOFFSET`.
- Vida, vida herida, cadencia y IA: bloque `[AI]` y `[Properties]` de `179_<tipo>.txt`, macros resueltas en `3_ai_ranges.h` y `3_characters.h`.
- Armas: `[OnInit]` del `179_` (arma inicial), y del script del nivel `C_PickupWeapon(w)` (recoge), `C_PickupAmmo(w,n)` (munición) y `C_RemoveAllWeapons(x)` (quita todo **menos** `x`; `rea/map/gameplay.md:237`).
- Los `.ldb` son binarios: las fuentes son `archivo@0xOFFSET`, no líneas.

## Tabla 1 — un renglón por tipo

| tipo (`179_*.txt`) | descripción | rol | modelo LOD0 | vida / herido | mult. cadencia | IA | arma inicial | inst. | niveles |
|---|---|---|---|---|---|---|---|---|---|
| C7_All_KillerSuit | DiCola John, Killer Suit | enemigo | dicola_john_l0.kfs | 45 / 4 (clase 7) | 0.70 | mobster + radios killer_suit | mp5 | 69 | P2L4:12 P3L3:2 P3L5:3 P3L5b:4 P3L6:20 P3L7:28 |
| C7_All_AesirSecurity | Aesir Security Soldier | enemigo | Aesir_Security_L0.kfs | 45 / 4 (clase 7) | 0.70 | mercenary | deserteagle + mp5 | 27 | P3L6:27 |
| C5_All_Mercenary | Mercenary Soldier | enemigo | gunn_bruce_l0.kfs | 35 / 6 (clase 5) | 0.50 | mercenary | deserteagle + mp5 | 107 | P3L2:25 P3L2b:35 P3L3:5 P3L5:24 P3L5b:18 |
| C6_All_Trenchcoat | DiCola Bruno | enemigo | dicola_bruno_l0.kfs | 40 / 5 (clase 6) | 0.60 | mobster | beretta | 30 | P3L4:27 P3L5b:3 |
| C6_All_FatTrenchcoat | DiCola Buddy, gordo | enemigo | dicola_buddy_l0.kfs | 40 / 5 (clase 6) | 0.60 | mobster | beretta | 30 | P3L4:23 P3L5b:7 |
| C4_Out_Jamn | DiCola Jamn, matón callejero | enemigo | dicola_jamn_l0.kfs | 30 / 7 (clase 4) | 0.40 | mobster | beretta | 89 | P2L2:42 P2L2b:43 P2L3:4 |
| C5_In_Mac | DiCola Mac, traje oscuro | enemigo | dicola_mac_l0.kfs | 35 / 6 (clase 5) | 0.50 | mobster | pumpshotgun | 5 | P2L4:5 |
| C5_In_Ronald | DiCola Ronald | enemigo | dicola_ronald_l0.kfs | 35 / 6 (clase 5) | 0.50 | mobster | beretta | 4 | P2L4:4 |
| C2_Toilet_Horst | DiCola Horst, el del baño | enemigo | dicola_horst_l0.kfs | 20 / 9 (clase 2) | 0.20 | mobster | beretta | 1 | P2L1:1 |
| frankie_the_bat | Frankie the Bat | jefe | frankie_the_bat_l0.kfs | 240 / 50 (clase 10) | 0.35 | killer_suit (interés 360) | baseballbat | 2 | P2L0:1 P2L1:1 |
| bb | BB, Special | jefe | bb_l0.kfs | 240 / 50 (clase 10) | 0.5 | killer_suit (interés 360) | beretta | 3 | P3L4:3 |
| henchman_01 | Providence Pilate | jefe | Providence_Pilate_L0.kfs | 130 / 25 (clase 9) | 0.05 | killer_suit (interés 360) | sawedshotgun | 1 | P2L4:1 |
| henchman_03 | Salem Joe | jefe | Salem_Joe_L0.kfs | 130 / 25 (clase 9) | 0.1 | killer_suit (interés 360) | pumpshotgun | 1 | P2L4:1 |
| C3_All_Martin | DiCola Martin (reuso P1) | enemigo | dicola_martin_l0.kfs | 25 / 8 (clase 3) | 0.30 | mobster | beretta | 15 | P2L1:5 P2L3:4 P2L4:6 |
| C4_In_Ray | DiCola Ray, gordo (reuso P1) | enemigo | dicola_ray_l0.kfs | 30 / 7 (clase 4) | 0.40 | mobster | beretta | 13 | P2L1:6 P2L4:7 |
| C2_All_Buck | DiCola Buck (reuso P1) | enemigo | dicola_buck_l0.kfs | 20 / 9 (clase 2) | 0.20 | mobster | beretta | 10 | P2L1:7 P2L2b:1 P2L3:2 |
| C1_All_Johnny | Doe Johnny (reuso P1) | enemigo | doe_johnny_l0.kfs | 15 / 10 (clase 1) | 0.10 | mobster | beretta | 8 | P2L1:7 P3L4:1 |
| C3_In_Luciano | DiCola Luciano (reuso P1) | enemigo | dicola_luciano_l0.kfs | 25 / 8 (clase 3) | 0.30 | mobster | beretta | 7 | P2L4:7 |
| C2_Out_Dyson | DiCola Dyson (reuso P1) | enemigo | dicola_dyson_l0.kfs | 20 / 9 (clase 2) | 0.20 | mobster | beretta | 2 | P2L3:2 |

`P2L4` = `1_Part2_Level4.ldb` (The Manor), etc.; el mapa nivel↔capítulo está abajo. Clases 1-10 en `3_ai_ranges.h:1-10` (vida) y `:12-21` (herido), cadencia `:23-29`.

## Tabla 2 — tres perfiles de IA (no hay junkie en II/III)

| parámetro | mobster | mercenary | killer_suit / jefes | fuente |
|---|---|---|---|---|
| AimingSpeed | 10 | 10 | 16 | 3_ai_ranges.h:62 / 103 / 120 |
| AimingSpeedCone | 30 | 30 | 15 | :65 / :104 / :121 |
| AimUpDownSpeed | 30.0 | 30.0 | 30.0 | literal en cada `179_*.txt:15` |
| TurnLeftRightSpeed | 520 | 720 | 720 | :92 / :113 / :130 |
| VisualPerceivingRadius | 50 | 60 | 70 | :83 / :110 / :127 |
| GeneralPerceivingRadius | 2 | 3 | 5 | :80 / :109 / :126 |
| ActivationReactionTime | 0.0 | 0.0 | 0.0 | :86 / :111 / :128 |
| ShootingCone | 120 | 100 | 10 | :89 / :112 / :129 |
| PerceivingGroup{1,2,3} | 15/4/0 | 40/5/2 | 50/8/3 | :68-74 / :105-107 / :122-124 |
| EnemyInterestTime | 30 | 30 | 30; jefes 360 | :77 / :108 / :125, :138 |
| ActivateOtherCharactersRadius | 0 | 0 | 0 | :95 / :114 / :131 |

`C7_All_KillerSuit` es híbrido: apuntado, giro, visión y cono de `AI_MOBSTER_*`, pero los tres radios de percepción de `AI_KILLER_SUIT_*` (`179_C7_All_KillerSuit.txt:21-23`). Los cinco jefes usan el perfil killer_suit completo con interés 360.

## Armas por instancia (lo que dicen los scripts)
- Todos los tipos arrancan con **munición infinita** en su arma inicial: `C_SetInfiniteAmmo(w,1)` en `[OnInit]` (`179_*.txt:80-84`). Mercenario y Aesir Security llevan dos (deserteagle + mp5, ambas infinitas).
- `C_RemoveAllWeapons(x)` aparece mucho con `empty` (desarme al empezar): 32 llamadas en el mercenario (107 instancias), 6 en killer suit (69), 5 en Aesir Security (27).
- Armamento que reparte el nivel (top por tipo): mercenario `pumpshotgun(17) deserteagle(15) grenade(11)`; Jamn `pumpshotgun(25) deserteagle(9) ingram(8) grenade(6) sniper(4)`; killer suit `beretta(2) deserteagle(2) grenade(2)`; trenchcoat `pumpshotgun(6) deserteagle(4) ingram(4)`.
- `C_PickupAmmo` con valores absurdos es intencional: `pumpshotgun, 99999`, `deserteagle, 999999`, `ingram, 999999` (`1_Part2_Level2b.ldb@0x15ffe64`, `@0x1601dea`).
- Estados por tipo (los más usados): mercenario `merccombat(52) nonreactive(41) crouchandshootstatic(24) standandshoot(24)`; killer suit `nonreactive(65) merccombat(36) idle(18) idleuntilenemyseen(18)`; Jamn `mobstercombat(33) crouchandshoot(17) standandshoot(16) nonreactive(15)`.

## Niveles (36_levels.txt:460-1041)
| clave | `.ldb` | capítulo | AI |
|---|---|---|---|
| P2L0 | 1_Part2_Level0 | Nightmare (prólogo) | **FALSE** |
| P2L1 | 1_Part2_Level1 | The Hotel II - Baseball Bat | TRUE |
| P2L2 / P2L2b | 1_Part2_Level2 / 2b | The Docks / (continuación) | TRUE |
| P2L3 | 1_Part2_Level3 | Punchinello's Restaurant | TRUE |
| P2L4 | 1_Part2_Level4 | The Manor | TRUE |
| P3L1 | 1_Part3_Level1 | The Drug Trip (prólogo) | **FALSE** |
| P3L2 / P3L2b | 1_Part3_Level2 / 2b | Cold Steel / Cold Steel B | TRUE |
| P3L3 | 1_Part3_Level3 | The Military Bunker | TRUE |
| P3L4 | 1_Part3_Level4 | The Garage | TRUE |
| P3L5 / P3L5b | 1_Part3_Level5 / 5b | The Asgard Building / II | TRUE |
| P3L6 | 1_Part3_Level6 | The Aesir Corporation HQ | TRUE |
| P3L7 | 1_Part3_Level7 | Nicole Horne's Penthouse Suite | TRUE |

Con `EnableAI = FALSE` los registros siguen existiendo pero los usa el guion: P2L0 tiene 1 enemigo (Frankie) y P3L1 ninguno. Enemigos por nivel: P2 `1 27 42 44 12 43`, P3 `0 25 35 7 54 27 32 47 28`.

## Jefes
- **Frankie the Bat** (`179_frankie_the_bat.txt`): 240 hp, `baseballbat` infinito (`:81`), interés 360. Aparece en el Nightmare (P2L0, `::32_Boiler_Room::Frankie`) y otra vez en P2L1 (`::bar::frankie`) donde el script le da `ingramdual` (`1_Part2_Level1.ldb@0x1bc9d42`) y lo pone a disparar; el mismo tipo hace de matón normal.
- **BB** (`179_bb.txt`): 240 hp, beretta infinita; en P3L4 el script lo deja con `jackhammer` a una instancia, `empty` a otra y `jackhammer` de pickup a la tercera (`1_Part3_Level4.ldb@0x1924bf7`, `@0x19245b8`, `@0x1924aa8`).
- **Providence Pilate** (`henchman_01`) y **Salem Joe** (`henchman_03`): 130 hp, cadencia 0.05 / 0.1. Arrancan con escopeta infinita, pero el script de The Manor deja a Pilate solo `ingram` (`1_Part2_Level4.ldb@0x1545ada`) y a Joe `ingramdual` + granada (`@0x1544305`). `henchman_02` (Mugnaio Vince) no tiene ninguna instancia en II/III.

## Otros personajes con instancias (no son enemigos)
| tipo | inst. | papel según el script |
|---|---|---|
| scientist | 14 (P3L3) | civiles: `nonreactive`, sin armas, uno `C_RemoveAllWeapons(empty)` |
| rat | 9 (P2L3) | ratas del alcantarillado, `nonreactive` |
| invisible | 8 (P2L0, P2L4, P3L5, P3L7) | "Invisible character, used as AI target" (`179_invisible.txt:2`); blanco, no combatiente |
| nypd_cop | 6 (P2L1, P3L7) | policías de escena: `C_SetInvulnerable(true)`, `C_SetIdle(2)`, `C_RemoveAllWeapons(mp5)` |
| enemy_max_payne / 2 / 3 | 5 / 3 / 2 | "use only for staged events"; Max falso, 22 hp |
| inner_circle | 3 (P3L5) | "Inner circle guys (dead)"; `empty` |
| horne_nicole | 2 (P2L4, P3L7) | Nicole Horne: invulnerable, `nonreactive`, guion |
| punchinello_don | 1 (P2L4) | Don Punchinello: `C_SetImmortal(true)` y luego `C_SetHealth(0)` |
| woden_alfred | 1 (P3L5) | Alfred Woden, guion, `empty` |
| enemy_young_max_payne | 1 (P3L1) | Max joven, escena |
| dead_baby | 1 (P2L0) | cadáver del bebé, escena |
| michelle_payne | 1 (P3L1) | Michelle muerta, escena |

## Qué no existe en los archivos
- **accuracy**: ningún `[AI]` la declara; la dispersión es del arma (`rea/map/weapons.md:30`).
- **cover**: no hay sistema ni puntos de cobertura (`rea/map/ai_behaviour.md:201`).
- **reaction**: 0.0 en los tres perfiles.
- **overrides por nivel**: los `.ldb` no traen números de IA; el `179_*.txt` es la única fuente. Lo que sí cambia por nivel es arma, estado y targeting.
- Sonidos de alerta: vienen del esqueleto/IA, no del `179_` (que solo declara muerte y daño: `merc_*` `235_enemy.txt:2447,2594`, `killersuit_*` `:733,852`, `mobster_*` `:1704,1865`). No los atribuí por instancia.

## Huecos y avisos
- Recuento propio. Al recontar la Parte I con el mismo método salen 56 Johnny / 32 Mickey / 81 Buck / 19 Dyson / 19 Junkie / 18 Luciano / 11 Ray / 20 Martin, no los 57/33/82/19/15/19/11/20 de `rea/map/enemies.md`. La diferencia está en instancias con nombre raro (`::v-b01::e2`, `::StartRoom::Character 00`) y en los junkie de la intro; no la perseguí.
- Las cuentas incluyen cadáveres y ayudantes con nombre de tipo de enemigo: `::sewer::corpse1..3` (Buck), `::madovens::dead` (mercenario), `::secret_room::e_ladderdead` (Johnny). Son 5.
- `scientist` y `horne_nicole` tienen `[AI]` de mobster completo aunque no combatan: el archivo no distingue "civil" de "enemigo", lo decidió el script.
- No revisé `x_level*.ras` ni la máquina de estados del ejecutable (`0x004db1f0`); si algo cambia armas en runtime, no está aquí.
- Escala por dificultad: `1_difficulty.txt` (`EnemyHealthMultiplier` 0.25-1.4); la vida de la tabla es la base.
