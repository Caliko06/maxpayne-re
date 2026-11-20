<!-- Becario report 2026-11-04 (DeepSeek), read by the orchestrator, no second key yet: verify an address before patching on it. -->
# Menu: how it works, and how to add a "Cheats" submenu
Read-only RE. Basis: rea/c/MaxPayne.exe (Ghidra corpus), capstone on build/img/MaxPayne.exe, 349_menu.txt + 349_menuid.h, x_english stringdefines.h + stringtable.txt, rea/map/input.md, gameplay.md, host/jump.c, tools/ghost/ghost.mjs, rea/m3_menu_native.png. Nothing was run. FACT unless marked.

## 1. Grammar
Root = top-level [Menu] blocks. A page is X_MenuPage (vt 0x7d4a64): ctor 0x633fe0 -> 0x636710 (line) -> 0x634080, which builds one array entry per block (+0x26 array, +0x22 count, +0x2e focus, +0x2a parent, +8 mode, +0xc `id`).
```
[Menu]{ [Properties] Name; ToolTip; Headline; Background; id=MENUID_SUBMENU_*; InitialPosition;
  [Item]{ [General] name; ToolTip; ID; } + [Message] String="...";   <- clickable line, vt 0x7d4b0c
  [Back]{Name;ToolTip;ID=0}  [Toggle]{Name;ToolTip;ID}  [Slider]{Name;ToolTip;ID;Ticks}
  [InputConfiguration]{...+ActiveToolTip}  [InputField]  [Load]/[Save]{ID=slot 0..12}
  [Menu]{...}   <- nested = submenu: the parent line shows its Name, selecting it opens the page (0x635bb0)
  [RandomHeadline] String=..;   [DeveloperMenu]{ Headline=..; } }
[OnInit]{ [Message] String=..; }
```
Block names, case-insensitive (0x634080): Menu 0x88ed44, Item 0x88ed50, Toggle 0x88ed54, InputField 0x88ed58, Slider 0x88ed5c, InputConfiguration 0x88ed60, Back 0x88ed64, Save 0x88ed68, Load 0x88ed6c, RandomHeadline 0x88ed70, DeveloperMenu 0x88ed48. GCID_*/LNID_* are plain #define strings (stringdefines.h, x_english.ras d2: GCID_NEWGAME="New Game", LNID_PART1_PROLOGUE="Part I, Prologue") expanded by R_ScriptLoader_preprocess (rlmfc 0x1001faf0), so a literal "Cheats" is the same kind of value.

## 2. [Message] String
Menu: parsed at page build; 0x636dd0 counts/parses "message" (0x8818ac) into X_Message (0x51e7c0) and keeps them in the item vector +0x25. Select 0x637100 (slot +8) sends each with 0x511be0(0,0) then X_MessageQueue::send 0x51b6a0: invalid sender (broadcast), receiver resolved by name (0x512a00 globals: maxpayne_gamemode, x_modeswitch; gameplay.md:31-35).
[OnInit]: raw strings kept at +0x7a (loader 0x62fa40:994), parsed and sent on every menu-mode entry by 0x631230 (from 0x42a240:113). Game mode: level/FSM scripts share the queue but parse each line when it runs (0x51e9e0/0x51e8c0 + 0x51b6a0; callers 0x65db00 0x683f50 0x687390 0x7086e0, gameplay.md:29) with the level scope table, so ::room::object resolves there and in the console, not in menu.txt.

## 3. Enabled/disabled, ids, limits
X_MenuMode slot 22 (+0x58) = 0x42a740(id): id outside 5..10 -> 1. id 5 Resume -> 0x42a75a (has save 0x421ab0/0x410c50); id 6 Quit -> 1; ids 7..10 -> 0x42a78d/0x42a7b1/0x42a7b8/0x42a7bf = 0x421b10(entry) OR the developer byte [mode+0x1ce].
0x421b10 = entry+0 (0x421a50) + 0x44c050 = GetProfileIntA("Game Level" 0x86cfdc, <name>, 0) == 1: path HKCU\Software\Remedy Entertainment\Max Payne\Game Level, keys normal/nightmare/hell/timedmode (live: ~/Games/save-5186/registry.tsv holds only normal=1; entries 0x8a6938 normal, 0x8a6928 nightmare, 0x8a6918 hell, 0x8a6908 timedmode; 0x4299a0 initialises the nightmare one).
gm_enablegamelevel("", "<mode>") (menu.txt:84), handler 0x457380: arg0 empty -> WriteProfileInt("Game Level", arg1, 1) (MFC thunk 0x76f778, as 0x42c130); arg0 non-empty -> difficulty parse check only.
Nothing is hidden except [DeveloperMenu] lines and [RandomHeadline] substitution; the rest is greyed and focus skips disabled lines (0x635750). No fixed item cap: 0x634080 allocates operator_new(count<<2), count = blocks-1 minus RandomHeadline and (without -developer) DeveloperMenu. The real cap is the column: first line MenuTextOffsetY 265 (menu.txt:35), ~12-13 px per line at 640x480 (8 lines in m3_menu_native.png), Load uses 14; 26 would clip. Only ids 5..10 are special-cased; saves 0..12, toggles 1..8, sliders 1..10, configs 1..45 live in their item classes.

## 4. [DeveloperMenu]
0x634080:134 and :277 subtract/skip DeveloperMenu blocks when [base+0x136]==0; :433 builds them as plain X_MenuLine (0x633fe0) only when [base+0x136]!=0; 0x636710:56 parses such a block as Properties (Headline). So it is a headline text line: no ID, not selectable.
Not reachable without -developer: [base+0x136] is written at 0x62fa40:103 from 0x429d10 param_11 = cmdline_isDeveloper 0x40cd80 read in 0x415dc0 (~line 1049); that byte is also [mode+0x1ce], the OR-term for ids 7..10 (0x42a7a1) and what F12 checks.

## 5. Key path
WH_KEYBOARD hook 0x518360 -> typed ring -> menu update 0x6313e0 pops (vk,char) -> mode OnKey [vt+0x48] = 0x42c020 (`add ecx,0x187; call 0x421820`) -> 0x421820: vk 0x7b F12 needs cmdline_isDeveloper 0x40cd80 (byte 0x8a6483, "-developer"), 0x7a F11 needs 0x40cdd0 ("-developerkeys"), 0x79 F10 needs 0x40cd60; then it toggles the console overlay 0x417c90 (gameplay.md:46-47) or a dev panel.
The key then goes to the page container 0x6354e0: byte table 0x635680 indexed by vk-8 (only vk 8..0x6f) -> 0 Esc/Backspace back, 1 Enter select 0x635b60, 2/4 left/right, 3/5 up/down, 6 Delete, 7 F9, 8 ignore. vk >= 0x70 falls out of the table (=8), so F1 0x70 is unused in the menu.

## 6. Fragment for the 26 levels (script only, no patch)
Inside the root [Menu], after the Quit sibling and before that page's [Back], one [Item] per level:
```
  [Menu]{ [Properties] Name="Cheats"; ToolTip=""; Headline="Levels"; id=6; }
  [Item]{ [General] Name=LNID_PART1_PROLOGUE; ToolTip=""; ID=MENUID_ITEM;
    [Message] String="maxpayne_graphicnovelmode->mpgnm_removeallnotes();";
    [Message] String="maxpayne_gamemode->gm_ResetShootDodgeBulletTimeCounters();";
    [Message] String="maxpayne_gamemode->gm_resetlevelitems();";
    [Message] String="maxpayne_gamemode->gm_setgamelevel( "normal" );";
    [Message] String="maxpayne_gamemode->gm_resetperformancemeasurements();";
    [Message] String="maxpayne_gamemode->gm_init( part0_level1 );";
    [Message] String="x_modeswitch->s_modeswitch( game );"; }
```
Proven by existing code: the five middle lines and the last two are menu.txt:112-118 verbatim (only gm_init's argument changes); gm_init(<level>) + s_modeswitch(game) is the runtime-proven two-line jump (host/jump.c:6-9, ghost.mjs:906-941). ID=MENUID_ITEM(1) is always enabled (0x42a740).
The 26 (36_levels.txt:56-1064), Name/arg: Tutorial/tutorial | Part I, Prologue/part0_level1 | Part I, Chapters 1-9: part1_level1, 1b, 2, 2b, 3, 3b, 4, 5, 6 | Part II, Prologue/part2_level0 | Part II, Chapters 1-5: part2_level1, 2, 2b, 3, 4 | Part III, Prologue/part3_level1 | Part III, Chapters 1-8: part3_level2, 2b, 3, 4, 5, 5b, 6, 7 (LNID_* at stringdefines.h:211-237). Leave out startup_level and end_combat: not playable. Split the 26 into 4 submenus (Tutorial, Part I, II, III) or the tail clips. UNKNOWN: a jump from the in-game pause menu (ghost always jumps from the front-end menu).

## 7. Smallest patch for one free key (no -developer)
Ruled out: -developer/-developerkeys turns F12 into the console overlay, not a page, and also unlocks ids 7..10 and adds the DeveloperMenu lines; the keyboard table 0x635680 ends at vk 0x6f and its 9 commands are all taken, so no free index to remap.
Recommended: one HOST_FUNCS entry in tools/recomp/patches.py replacing X_MenuMode::onKey 0x42c020 (13 bytes, today only forwards the vk to 0x421820) with host code:
```
  if (vk != 0x70) return 0;                    // F1, free in the menu (>= 0x70 is "ignore")
  root = ld(mb + 0x92);                        // root page; 0x631390 sets page := [mb+0x92]
  n = ld(root + 0x22); arr = ld(root + 0x26);  // children, count (0x634080)
  for (i = 0; i < n; i++) { c = ld(arr + 4*i);
      if (ld(c + 0xc) == 6) { rn_call(0x6317d0, mb, c); return 1; } }   // 6 = MENUID_SUBMENU_CHEATS
```
mb = ecx on entry; a page's +0xc is its `id =`, so Cheats gets `id = 6;` (after 349_menuid.h:115 MENUID_SUBMENU_SAVE = 5). 0x6317d0(mb,page) is exactly what a submenu line does (0x635bb0), so key and mouse end in one place, in the front-end and the pause menu (same root page). F1 in game mode (0x44c610) is untouched: only the menu mode's onKey is replaced. Not built, not run.

## 8. Unknown
X_MenuSave 0x7d4c8c / X_MenuLoad 0x7d4ccc per-slot enabling, and what 0x421ab0 checks, were not read. The rest is from code, the shipped scripts or the OWNER's data. No browser, game or build was run.
