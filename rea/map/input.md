# Input map: keyboard, mouse, bindings, window messages (MaxPayne.exe 1.05)

Sources: Ghidra decompiles rea/c/MaxPayne.exe, Capstone disassembly of build/img/MaxPayne.exe (functions Ghidra did not list
are marked ASM), the OWNER's x_english.ras (input.txt, 349_menu.txt, globalconfig.txt). Names below are mine.
Every address is a rebased VA of MaxPayne.exe.

## What the host must do (the short list)
1. The typed-key queue is fed by a WH_KEYBOARD hook (type 2), not by DirectInput. Hook proc 0x518360 stores VK + ToAscii char
   in a 32-entry ring. Game mode reads that ring for Esc, F1, F2, F5 (quicksave), F9 (quickload). The host never calls hook
   type 2 and has no GetKeyboardState/ToAscii: those keys do nothing today. Fix: in PeekMessageA/GetMessageA, for each removed
   WM_KEYDOWN/WM_SYSKEYDOWN call the hook chain (code 0 = HC_ACTION, wParam = VK, lParam = key lParam); add GetKeyboardState
   (256 bytes, bit 7 = down, bit 0 = toggle) and ToAscii (return 1 + the char). See "Typed-key ring".
2. DirectInput data the game needs: GetDeviceState keyboard 256 bytes (game tests byte != 0, 0x80 is fine), mouse 20 bytes
   (lX,lY,lZ,buttons[8], button down = bit 7); GetDeviceData with 16-byte entries; both devices Acquire OK every frame
   (a failed mouse Acquire skips the whole mouse update); GetDeviceData must return 0 (DI_BUFFEROVERFLOW 1 makes the game
   reset all key edges and retry). Wheel granularity 120 (GetProperty 3, offset 8). No joystick (object stays null).
3. Menu cursor is 640x480 virtual units, +1.25 units per DirectInput count (any render size). For 1:1 browser tracking
   send counts = movement_px * 640 / (render_w * 1.25). The menu cursor uses mouse button 0; the keyboard path is the typed-key ring (section 4), now on W/A/S/D/Space/Q (patches.py).
4. Gameplay look = DirectInput counts x (slider*1.4+0.1) x 0.0025, smoothed per frame; counts are positive down/right.
5. Win32: WM_ACTIVATEAPP(active) must be sent (0x40d270 -> acquire all / unacquire all). WM_SETCURSOR must end in SetCursor(0)
   (game draws its own cursor). Settings persist in the registry (HKCU, "Remedy Entertainment"), already persisted by host.
6. Console/dev keys exist only with the command-line flag "developerkeys" (0x867b84 -> byte 0x8a6489). Not needed.

## 1. Polling: DirectInput
- DirectInputCreateA(hinst, 0x700, &0x8ac6bc, 0): FUN_0051a6c0 (DInput_Create, ref-counted by DAT_008ac6c0; hinst saved 0x8ac6c4,
  hwnd saved 0x8ac6c8). DirectInput 7 interface; IDirectInput method used: CreateDevice (+0xc) only. No EnumDevices.
- Keyboard class ctor FUN_00517e70 (Keyboard_Ctor): CreateDevice(GUID_SysKeyboard {6F1D2B61-...} at 0x7da594), SetDataFormat
  (0x7da54c = c_dfDIKeyboard: size 0x100, 256 objs, flags 2), SetCooperativeLevel(hwnd, 0x16 = NONEXCLUSIVE|FOREGROUND|NOWINKEY),
  SetProperty(DIPROP_BUFFERSIZE = 16). Also installs the WH_KEYBOARD hook (below).
- Mouse class ctor FUN_00518c40 (Mouse_Ctor): CreateDevice(GUID_SysMouse {6F1D2B60-...} at 0x7da5a4), SetDataFormat (0x7da564 =
  c_dfDIMouse: size 0x14, 11 objs, flags 2 = relative), SetCooperativeLevel(hwnd, 5 = EXCLUSIVE|FOREGROUND),
  GetCapabilities (DIDEVCAPS size 0x2c; fails if DIDC_ATTACHED bit 0 is clear; dwButtons at +0x10 is stored at obj+0x2d and
  limits valid button indexes), SetProperty(BUFFERSIZE 16), GetProperty(DIPROP_GRANULARITY, obj offset 8 = wheel) stored as
  float at obj+0x83 (wheel counts per notch; must be nonzero, real value 120).
- Poll() and SetEventNotification are never called. Only vtable offsets used: +0xc GetCapabilities, +0x14 GetProperty, +0x18
  SetProperty, +0x1c Acquire, +0x20 Unacquire, +0x24 GetDeviceState, +0x28 GetDeviceData, +0x2c SetDataFormat, +0x34 SetCooperativeLevel.
- One poll per frame: mode manager tick FUN_00601250 (call site 0x60128d) calls InputMgr_Update FUN_00516c70(dt) -> only if
  DAT_0087e6a0 != 0 (initial value 1 in the file) -> Keyboard_Update (obj vtable slot 3 = 0x518530, ASM), Mouse_Update (0x519070).
  Before the modes' own update. The same flag gates every key/mouse query (0x516d60 getter, 0x517300/0x517320/0x517350).
- Activation: WM_ACTIVATEAPP active -> 0x40baf0 -> InputMgr_AcquireAll 0x516d20 (flag = 1); inactive -> 0x40bb50 -> InputMgr_UnacquireAll
  0x516ce0 (flag = 0, so all input reads 0). 0x516cb0 = unacquire+acquire then Update(0) (flush; called at 0x451a30, 0x451b6c).
- Keyboard_Update 0x518530 (each frame): Acquire (accept 0 or 1) -> GetDeviceState(0x100, obj+9). On DIERR_INPUTLOST (0x8007001e)
  re-Acquire and retry; any other error throws X_InputDeviceException. Then loop GetDeviceData(16-byte entries, count 16, flags 0)
  into 0x8ac2f8; a nonzero result resets all 256 key records (0xf bytes each, array ptr obj+0x109) and retries until it returns 0.
  Per key: FUN_0051ace0 clears the pressed (+8) and released (+9) edge bytes, then each buffered event (dwOfs = DIK, dwData bit 7 =
  down, dwTimeStamp ms) calls FUN_0051ad80: sets pressed/released edge and runs a double-click state machine (0 idle,1 down,
  2 up-wait,3 double). Window = FUN_0051ad30(setting) = setting*0.55 s; elapsed = (ts - last)*0.001.
- Mouse_Update 0x519070 (each frame): Acquire (<0 -> OutputDebugString "mouse acquire failed", return, no update), GetDeviceState(0x14, obj+9):
  lX obj+9, lY obj+0xd, lZ obj+0x11, buttons obj+0x15+i (>>7). Then GetDeviceData into 0x8ac5b0 (same retry rule); only entries with
  dwOfs > 0xb (buttons, DIMOFS_BUTTON0 = 0xc) are used, axes come from GetDeviceState. Both calls are made per frame, so the
  host must keep two independent views (state delta and event buffer).
- Mouse math (per frame): s = FUN_00519480(sensitivity slider) = slider*1.4 + 0.1 (consts 0x7d045c, 0x7cb8b4); v = counts * s * obj+0x87;
  FUN_00519370 smooths: acc += v; out = acc*0.5; acc -= out (0x7cbdd4 = 0.5; flushes the residue when v is exactly 0). obj+0x87 is set
  by game mode each tick (0x44d4d5 -> FUN_00519880, clamp 0..1, 0 = look frozen). Scaled position accumulates at obj+0x5b/+0x5f,
  raw counts at obj+0x6b/+0x6f. Look value handed to the game FUN_005195c0 = (1/dt) * (delta scaled) * 0.0025 (units per second;
  dt = obj+0x4d); Y negated when "Invert Mouse" (obj+0x51). Consumer (VERIFIED 2026-11-02, exe 0x4cefae..0x4cefdd, PlayerInput
  vtable 0x7cf974): the getters are slot +0x48 = 0x4ceab0 (X, jumps to 0x4cc4c0) and +0x4c = 0x4cead0 (Y, 0x4cc520), both through
  0x4cdc70 -> 0x4ce160 -> 0x51a160 (slider type 1/2 -> 0x5195c0). Look = getter * 180.0 (0x7cfb08) * f * 0.017453292 (0x7cfb04, deg->rad) with
  f = float at [ebp-4] (the frame step: the getter divides by dt, so f cancels it). Result: radians per DirectInput count =
  0.0025 * pi * s = 0.00785 * (slider*1.4+0.1): 0.00628 rad (0.36 deg) per count at the default slider 0.5, smoothed 0.5/frame.
  Stored at game object +0x19 (from slot +0x4c, Y) and +0x1d (X). The 0x4cc560-style calls at 0x42aa7c are button queries, not aim.
- Wheel: lZ / granularity accumulates (obj+0x7b); FUN_005198e0 returns +1/-1 once |delta| >= 1 notch (only if the device reports more than 2 axes:
  DIDEVCAPS copy at obj+0x1d, dwAxes at obj+0x29, FUN_005198d0), sign flipped by "Invert Mouse Wheel" (obj+0x52).
- Joystick: global 0x8ac2ec stays 0; every use is guarded (e.g. 0x516c9c). No EnumDevices.

## 2. Bindings
- Input manager globals: 0x8ac2e4 keyboard, 0x8ac2e8 mouse, 0x8ac2ec joystick (accessors FUN_00516d70/80/90).
- Action table at 0x8a9168, 32 actions, stride 0x29 (loop end 0x8a9688). One action = 4 bindings of 9 bytes {int type, int value, 1 byte}
  at +9*i (set by FUN_00519a00), then slider type (int) at +0x24 (FUN_00421a60), 1 spare byte. Binding types: 1 keyboard (value = DIK),
  2 mouse button (0 = left), 3 joystick button. Slider types: 1 mouse X, 2 mouse Y, 3 wheel, 4-9 joystick axes (FUN_0051a160 / 0x4ce160).
- Value of an action (FUN_004cdc70): +1.0 if binding 0 or 2 is down, -1.0 (0x7ce988) if binding 1 or 3 is down (FUN_004ce0a0), else the
  slider. "Down" = FUN_00519a10; "pressed this frame" = FUN_00519ac0; "released" = FUN_00519b20. Bindings 2 and 3 are the alternates.
- Defaults (FUN_004cb090, DIK in hex; second key = alternate binding):
  | action (script name) | index | defaults |
  |---|---|---|
  | RUN / WALKBACK | 0 | W 11 + Up c8 / S 1f + Down d0 |
  | STRAFELEFT / STAFERIGHT (sic) | 1 | A 1e + Left cb / D 20 + Right cd |
  | SHOOT | 2 | mouse button 0 |
  | RELOAD | 3 | R 13 + Numpad0 52 |
  | JUMP | 4 | Space 39 + RShift 36 |
  | CROUCH | 5 | C 2e + RCtrl 9d |
  | DODGELEFT/RIGHT/FORWARD/BACKWARD/MODIFIER | 6-10 | unbound |
  | AIMUP / AIMDOWN | 11 | mouse Y slider |
  | AIMLEFT / AIMRIGHT | 12 | mouse X slider |
  | USE | 13 | E 12 + Enter 1c |
  | SELECTWEAPONSLOT0..5 (melee, handgun, shotgun, machinegun, throwable, sniper) | 14-19 | keys 1..6 (02..07); slots 6-10 (20-24) unbound |
  | BESTWEAPON | 25 | G 22 |
  | NEXTWEAPON / PREVIOUSWEAPON | 26 | wheel slider + [ 1a / ] 1b |
  | PAUSE | 27 | P 19 + Pause c5 |
  | PAINKILLER | 28 | Tab 0f + Backspace 0e |
  | SNIPERZOOM (not used by retail zoom; see correction below) | 29 | Q 10 + Numpad1 4f |
  | SLOWMOTION | 30 | unbound |
  | BULLETTIME | 31 | mouse button 1 (right) + LShift 2a |
  Names are registered by FUN_004cce80 (#RUN#, #WALKBACK# ... strings 0x87bf70-0x87c1a0). Action address = 0x8a9168 + index*0x29.
- Fixed keys (not in the table, game mode only, through the key ring, VK codes): Esc 0x1b (0x44c431, opens the menu), F1 0x70 (0x44c610),
  F2 0x71 (0x44c751), F5 0x74 quicksave (0x44c8ea, calls 0x410c30/0x410e20), F9 0x78 quickload (0x44c7c7; text "Press F9 again to Quickload,
  Esc to Cancel"). Dispatcher: FUN_0044c1b0 at 0x44c28a pops the ring, calls OnKey (vtable slot 0x48), then switch (vk-0x1b) table 0x44e984/0x44e954.
  Dev-only (flag "developerkeys"): PgUp 21, PgDn 22, End 23, Home 24, Ins 2d, Del 2e; 'C' 0x43 console (0x44c170); F10-F12 (0x421820).
- Esc also aborts the AI-network calculation while loading: GetAsyncKeyState(0x1b) at 0x43a85b (FUN_0043a7e0).
- Storage: no file in the game dir. MFC profile: AfxGetModuleState()->CWinApp::GetProfileIntA, and 0x40a4c0 calls SetRegistryKey("Remedy Entertainment"),
  so it is the registry: HKCU\Software\Remedy Entertainment\<app>\Input Settings. Values: "Input Settings Version" (must be >= 9, 0x87bf64, else
  defaults), then per action i (format "%2d %s", 0x87bf68): "Slider type", "Button0 type", "Button0 value" ... "Button3 value"; then
  "Invert Mouse", "Invert Mouse Wheel", "Mouse Sensitivity X", "Mouse Sensitivity Y" (0-10000, default 5000, x0.0001 -> 0..1, out of range -> 0.5),
  "Invert Joystick", "Joystick Sensitivity", "Double-Click Speed" (default 500). Load FUN_004cb3d0, save FUN_004cb7d0 (defaults FUN_004cb090).
- Key names in the controls menu: input.txt (x_english.ras, parsed by FUN_00516da0): [key] ScanCode = n; KeyName = "..." for the keys that need a
  custom name; the rest come from GetKeyNameTextA (FUN_00519b80, lParam = scancode<<16, extended bit 24), mouse buttons "MB n", wheel "MOUSE WHEEL",
  suffix " DCLICK". Rebind capture is FUN_004cd5c0: Esc (DIK 1) cancels, Enter/NumEnter/mouse0 held block until released, any key, mouse move, wheel binds.

## 3. Window messages and Win32 calls
- Main window (MFC) message map at 0x7cb970, handlers only: WM_GETMINMAXINFO 0x24 -> 0x40d170, WM_SETFOCUS 7 and WM_KILLFOCUS 8 -> 0x40d260 (default),
  WM_ACTIVATEAPP 0x1c -> 0x40d270, WM_NCLBUTTONDBLCLK 0xa3 -> 0x40d2b0 (HTCAPTION -> toggle fullscreen 0x40d210), WM_SYSCOMMAND 0x112 -> 0x40d2d0
  (swallows SC_KEYMENU 0xf100 and SC_SCREENSAVE 0xf140 in fullscreen), WM_SETCURSOR 0x20 -> 0x40d340, WM_COMMAND id 0x8003 -> 0x40d210, WM_DESTROY 2,
  WM_CLOSE 0x10, WM_PAINT 0xf. No WM_KEYDOWN, WM_CHAR, WM_MOUSEMOVE, mouse button or WM_MOUSEWHEEL handler: input is DirectInput + the key hook.
- WM_SETCURSOR 0x40d340: if the app object says so (virtual +0x28 of FUN_0040bbc0(), probably fullscreen) and not IsIconic -> SetCursor(NULL) and return 1; else LoadCursorA(NULL, IDC_ARROW 0x7f00
  or IDC_WAIT 0x7f02) + Default. 0x40d040 (fullscreen toggle) also SetCursor(NULL), SetWindowPos TOPMOST to GetSystemMetrics(0/1), SetForegroundWindow, ShowWindow(5).
- Imports of MaxPayne.exe (USER32) that touch input: SetWindowsHookExA 0x517e70 (WH_KEYBOARD) and 0x40a4c0 (a second hook, type not read; the host log shows WH_CBT 5 for MFC), UnhookWindowsHookEx 0x517ce0 / 0x40adc0,
  CallNextHookEx 0x518360, GetKeyboardState 0x518360, ToAscii 0x518360, GetKeyNameTextA 0x519b80, GetAsyncKeyState 0x43a7e0, SetCursor 0x40d040 / 0x40d340,
  LoadCursorA 0x40d340, IsIconic 0x40c020 / 0x40c6d0 / 0x40d340, SetForegroundWindow 0x40c6d0 / 0x40d040. The pump (PeekMessageA, TranslateMessage, DispatchMessageA) is in
  0x409ac0, 0x40c020, 0x40c440, 0x43a7e0.
- NOT imported by MaxPayne.exe or the five DLLs: GetCursorPos, SetCursorPos, ClipCursor, ShowCursor, MapVirtualKey, GetKeyState, VkKeyScan, SetCapture. The game never
  positions or confines the OS cursor; the menu cursor is a game-side position.

## Typed-key ring (the WH_KEYBOARD hook)
- 0x517e70 does SetWindowsHookExA(2, 0x518360, hinst 0x8ac6c4, GetCurrentThreadId()) -> handle 0x8ac5a8 and clears the ring (FUN_00518430).
- Hook 0x518360: code < 0 -> CallNextHookEx. code == 0 and lParam bit 31 == 0 (key going down, repeats included): GetKeyboardState(0x8ac3f8);
  ring[w].vk = wParam (0x8ac504 + 4*w); ToAscii(wParam, lParam, state, &out, 0): result 1 -> char at 0x8ac584 + w, else 0; w = (w+1) mod 32 (0x8ac500);
  the oldest entry is dropped when the ring is full (reader 0x8ac4fc).
- Reader: FUN_00517300 (empty? 1 when queue empty or flag 0x87e6a0 = 0), FUN_00517320 (pop vk, char), FUN_005172e0 (clear, 0x637760).
  Callers: game mode 0x44c26b (dispatch above), menu mode 0x6313e0, dev mode 0x49b6d0.
- Real Windows calls this hook when GetMessage/PeekMessage removes a WM_KEYDOWN/WM_SYSKEYDOWN (and key-ups with bit 31 = 1, ignored by the proc).
  lParam bits: 0-15 repeat, 16-23 scancode, 24 extended, 30 previous state, 31 transition (0 = press).

## 4. Menu keyboard (CORRECTED 2026-11-02: the retail menu always had a keyboard; an earlier note here said it had none)
- Menu mode (vtable 0x7d4670): update slot 14 = FUN_006313e0. The mode's own OnKey slot 18 = 0x60dec0 is `xor al,al; ret 8`, but the update
  does not need it: for each typed key (ring, VK + char) it calls OnKey and then the MENU CONTAINER FUN_006354e0(vk, char) on obj+0x8e.
  Mouse: cursor += counts*0.0025*500 (1.25 units), clamp 0..640 / 0..480 (0x7ce990/0x7ce98c), click = button 0 -> FUN_00635810 (hit test
  per item, vtable +0x2c; sets the focus index obj+0x2e). Per frame FUN_00635be0 clears every item's focus flag (item vtable +0x34(0)) and
  sets it on the focused one (+0x34(1)): the same highlight for hover and keyboard. Disabled items (+0x1c = 0, e.g. greyed Resume/Save)
  are skipped by the focus move (FUN_00635750 loops until +0x1c is true).
- FUN_006354e0 order: (1) the focused item's OnKey (vtable +0x20, args vk, char, 0) first; a nonzero result consumes the key. Default
  0x636bc0 returns 0. X_MenuInputConfig (0x7d4bcc) OnKey 0x6382f0 and X_MenuInputField (0x7d4b8c) OnKey 0x637820 consume everything while
  they capture a key / edit text, so no navigation key leaks out of them. (2) switch (vk-8) 0..0x70: byte table 0x635680 -> dword table
  0x63565c: 0 = 0x08 Backspace / 0x1b Esc = back (popup close, else owner vtable +0xa4); 1 = 0x0d Enter = select (FUN_00635b60, item
  +8; skipped when FUN_00517270 has bit 2 = Alt, Alt+Enter is the fullscreen toggle); 2/4 = 0x25/0x27 Left/Right -> FUN_00635b40(-1/+1)
  -> item +0x28 (sliders, toggles, option lists); 3/5 = 0x26/0x28 Up/Down -> FUN_00635750(-1/+1) (focus move, wraps); 6 = 0x2e Delete ->
  item +0x24 (save slot delete); 7 = 0x78 F9 -> owner +0x98; 8 = ignore. The hint line says "Use the cursor keys or mouse".
- Item classes (vtables): X_MenuPage 0x7d4a64, Line 0x7d4acc, Item 0x7d4b0c, BackItem 0x7d4b4c, InputField 0x7d4b8c, InputConfig 0x7d4bcc,
  Slider 0x7d4c0c (+0x28 = 0x6388a0), Toggle 0x7d4c4c, Save 0x7d4c8c, Load 0x7d4ccc. The same container serves every screen (main,
  options, controls, audio, load/save slots), so one key map covers them all. Slot names: item +8 select, +0x1c enabled, +0x20 OnKey,
  +0x24 delete, +0x28 adjust, +0x2c hit test (pos, pressed, held), +0x30 update, +0x34 set focus.
- Key rebind capture FUN_004cd5c0 (the controls screen): Esc (DIK 1) cancels. A key binds on press (double-click window decides DCLICK).
  It first waits until Enter (0x1c), NumEnter (0x9c) and mouse 0 are all up (flag 0x8a9709), otherwise the key that selected the item
  would bind itself. Calls: item OnKey -> owner +0x64 each ring event.
- PATCH (tools/recomp/patches.py "Menu keyboard on the left hand"): table 0x635680[vk-8] = W 0x57 -> 3 (Up), S 0x53 -> 5 (Down), A 0x41 -> 2
  (Left), D 0x44 -> 4 (Right), Space 0x20 -> 1 (Enter), Q 0x51 -> 0 (Esc). The NumEnter guard in 0x4cd5c0 (DIK 0x9c at 0x4cd61e and
  0x4cd66d) watches Space (DIK 0x39) instead, since Space now selects. E is NOT a select key: guarding a third key needs code space,
  and an unguarded E would bind itself on the controls screen. Arrow keys, Enter, Esc, Backspace still work (retail).
  RUNTIME PROOF (web :5183, headless, 2026-11-02): S x3 from the main menu focuses Options (hint text changes); Space opens Options;
  A x5 lowers the Audio > Music Volume slider; Options > Controls > Movement > Space on Forward shows the capture prompt, Space does not
  self-bind, S binds ("Up or S", the old W is displaced) and does not move the focus.
- Auto-repeat: Windows repeats a held key's WM_KEYDOWN (lParam bit 30 set); the hook takes repeats. web/input.js sends bit 9 (0x200|DIK)
  for a browser auto-repeat, host/input.c turns it into WM_KEYDOWN with bit 30 (user32_key down = 2), no DirectInput change (DirectInput
  reports changes only, as before). Holding S scrolls the menu, holding D/A slides a slider.
- Text entry: X_MenuInputField exists (0x637600 ctor) but no retail screen was seen using it; saves are named by the game (prefixes built by
  0x410300), files savegame%03i.mps / .mds (0x410f20). The char half of the ring serves the developer console only.
- Other modes with a real OnKey (0x7cc404: 0x429830, 0x7cc500: 0x42c020, 0x7ce920: 0x49c110) just forward to the developer-key handler 0x421820.
- Graphic novel mode (vtable 0x7d4724 / 0x631a60): keys through DirectInput state, see graphicnovel.md 3 (A/D page, W/S chapter, Space/E play-stop,
  Esc/F1 stop-exit). All in the default action table, so the left hand covers it.

## Default bindings: what has no left-hand key (2026-11-02, from the table in section 2)
Every action but two has a left-hand default: move W/S/A/D, reload R, jump Space, crouch C, use E (+Enter), weapons 1-6, best weapon G,
painkiller Tab, sniper zoom E (Use; see retail correction below), bullet time LShift, pause P. SHOOT is mouse button 0 only (alternates unbound), NEXT/PREVIOUS weapon are
the wheel plus [ and ] (right hand). The controls screen (Options > Controls) rebinds any of them now with W/S/Space on the left hand.

## NOT VERIFIED
- Mouse look consumer past +0x19/+0x1d (camera clamp, sign). What F1 (0x44c610) and F2 (0x44c751) do exactly.
- Keyboard_Update 0x518530 is named now (names/MaxPayne.exe.tsv). CORRECTED 2026-11-02: there is no "controls class" at 0x42a4e0-0x42b7ef (0x42a4c0/d0/e0 are 7-byte getters of an unknown class); the real controls code is the PlayerInput family 0x4cbc20-0x4cf560. No X_InputDeviceJoystick exists: its global 0x8ac7d8 is never created and the joystick global 0x8ac2ec is always null.

## Mouse look on the web (2026-11-02)
Chain, no scaling anywhere: web/input.js mousemove (movementX per CSS px, pointer locked, requestPointerLock({unadjustedMovement: true}),
plain lock as fallback) -> Atomics.add ring[2..3] -> host/input.c input_poll (exchange to 0, one dinput8_motion per frame) -> dinput8.c
o->axis[] += counts -> GetDeviceState writes lX/lY and clears them (relative, flags 2). The sum of counts per frame reaches the game
unchanged, so the game's own math above (sensitivity slider, 0.5 smoothing, 0.00785 rad/count) is the only scaling. Not provable headless:
that Chrome gives raw device counts under unadjustedMovement on this Mac (the lock needs a real pointer); the fallback path is accelerated.

## Coverage (2026-11-02, names in rea/map/names/): exe input (PlayerInput, keyboard, mouse, DirectInput, bindings)
Names come from Ghidra-corpus + disassembly reading by a Sonnet worker, spot-checked by the platform slice; one source = weak, two = strong.
'understood' = body read and role stated; 'named' = name justified by class/shape only. Full table: rea/map/names/<image>.tsv (addr, name, level, confidence, summary, evidence).

| set | functions in table | named before | named now | unknown | understood (strong) | named outside the table |
|---|---|---|---|---|---|---|
| input (subsystem) | 190 | 1 | 188 | 2 (0x576440, 0x576720: a chunk loader, not input) | 143 (131) | 145 |

Source: `names_input.json` (333 entries: 188 of the 190 table functions + 145 outside the table, marked `outside_table`).
Levels: understood = body read; named = name from shape, class or doc only. Weak = one source.

#### Classes and objects

##### Input manager (no RTTI, prefix `input_manager_`)
- Refcounted singleton, refcount `DAT_008ac2f0`. Layout: byte +0, hinst +1, hwnd +5, byte +9.
- Created by `0x516990` (ctor `0x516970`), released by `0x516be0`.
- Globals: keyboard `0x8ac2e4`, mouse `0x8ac2e8`, joystick `0x8ac2ec` (never set). Master enable flag `0x87e6a0`.
- Methods: update `0x516c70` (calls slot +0xc of each device), reacquire `0x516cb0`, unacquire-all `0x516ce0`,
  acquire-all `0x516d20`, getters `0x516d60` flag, `0x516d70` keyboard, `0x516d80` mouse, `0x516d90` joystick.
- `0x516da0` loads input.txt: key names into `std::map<int,string>` at `0x8abcd0`, plus name strings
  (`0x8abcb8` mouse button, `0x8abca0` wheel, `0x8abc90` mouse X, `0x8abc80` mouse Y, `0x8abc70` DCLICK suffix; mapping inferred, weak).

##### X_InputDevice (vtable `0x7d0464`) and DirectInput
- Base: ctor `0x51a670`, scalar deleting dtor `0x51a6a0` (slot 0), dtor body `0x51a7f0`. Slots 1-3 are purecall (`0x76f964`).
- Shared IDirectInput: `0x51a6c0` (DirectInputCreateA 0x700 into `0x8ac6bc`, refcount `0x8ac6c0`), `0x51a800` release.
- Slot 1/2 of keyboard and mouse is the same merged code: `0x5194a0` Unacquire (vtbl+0x20), `0x5194b0` Acquire (vtbl+0x1c).

##### X_InputDeviceKeyboard (vtable `0x7d02f4`)
- Slots: 0 `0x517c70` scalar deleting dtor, 1 `0x5194a0` unacquire, 2 `0x5194b0` acquire, 3 `0x518530` update.
- ctor `0x517c10`, dtor body `0x517c90`, shutdown `0x517ce0`, init `0x517e70`, resetFields `0x517e50`, initialized flag `0x517e40` (+0x10d).
- Fields: DirectInput device +5, state +9, key record array +0x109 (0xf bytes each), flag +0x10d.
- Queries: `0x5184c0` isDown, `0x5184d0` isDoubleClick, `0x5184f0` wasPressed, `0x518510` wasReleased.
- Typed-key ring (WH_KEYBOARD hook `0x518360`, handle `0x8ac5a8`): 32 entries; VK at `0x8ac504+4*i`, chars at `0x8ac584+i`,
  write index `0x8ac500`, read index `0x8ac4fc`, per-poll snapshot `0x8ac4f8`, key state `0x8ac3f8`.
  Helpers: clear `0x518430`, empty `0x518450`, pop `0x518470`. Gated by `0x87e6a0`: `0x5172e0`, `0x517300`, `0x517320`, `0x517350`, modifier mask `0x517270`.
- NEW: update `0x518530` copies `0x8ac500` into `0x8ac4f8` at the start of every poll; the empty test uses that snapshot.

##### X_InputDeviceMouse (vtable `0x7d0404`)
- Slots: 0 `0x518ae0` scalar deleting dtor, 1/2 shared unacquire/acquire, 3 `0x519070` update.
- ctor `0x518a60`, dtor body `0x518b00`, shutdown `0x518b50`, init `0x518c40`, resetFields `0x518bd0`, initialized `0x518bc0` (+0x8b).
- Fields: state +9 (lX +9, lY +0xd, lZ +0x11, buttons +0x15+i), dwAxes +0x29, button count +0x2d, dt +0x4d, invertMouse +0x51,
  invertWheel +0x52, sensX +0x53, sensY +0x57, scaled pos +0x5b/+0x5f, raw +0x6b/+0x6f, wheel +0x7b, look gate +0x87 (default 1.0).
- Math: `0x519480` sens = slider*1.4+0.1; `0x519370` smoothing (out = acc*0.5); `0x5195c0` look delta; `0x519710` menu cursor delta; `0x5198e0` wheel notch.
- Setters/getters: sens X `0x519850`/`0x519860`, sens Y `0x742870`/`0x519870`, invert mouse `0x519960`/`0x541110`, invert wheel `0x576430`/`0x576420`, look gate `0x519880`, hasWheel `0x5198d0`.
- Queries: `0x5194c0` button count, `0x5194d0` isDown, `0x519500` dclick, `0x519540` pressed, `0x519580` released.

##### Exceptions (all `X_InputDeviceException*`, built inline too)
- Ctors: `0x5182e0` base (0x7d0398), `0x518300` CantCreateDIDObject (0x7d0374), `0x518320` CantSetDataFormat (0x7d0350),
  `0x518340` CantSetCooperativeLevel (0x7d032c), `0x519030` CantGetCapabilities (0x7d043c), `0x519050` DeviceNotAttached (0x7d0418).
  UnhookKBFailed (0x7d0308), CantRead (0x7d03bc), CantAcquire (0x7d03e0), CantCreateDIObject (0x7d0478) are set inline in 0x517ce0, 0x518530/0x519070, 0x51a6c0.
- `0x518870` maps a DIERR code to its string.

##### Joystick (no RTTI class; prefix `input_joystick_`)
- Global object `0x8ac7d8` built by static init `0x51a880`; never created as a device. Button count +0x12d, records +0x149, invert +0x151, sensitivity +0x152.
- Functions `0x51a8c0`-`0x51aa50` are named weak by shape.

##### Key record (0xf bytes, prefix `input_key_record_`)
- Fields: state int +0 (0 idle, 1 down, 2 up-wait, 3 double-click), timestamp +4, pressed edge +8, released edge +9, dclick timer float +0xa, constructed +0xe.
- `0x51ac60` array ctor, `0x51ac90` ctor, `0x51aca0` reset-if-constructed, `0x51acc0` zero, `0x51ace0` per-frame tick, `0x51ad80` state machine.
- Double-click setting global `0x87f000`: `0x51ad60` set, `0x51ad70` get, `0x51ad30` window = setting*0.55, `0x51ad50` state==3.

##### Binding (9 bytes {type, value, flag}) and slider (5 bytes {type, flag})
- Binding types: 1 keyboard DIK, 2 mouse button, 3 joystick button, 4-6 double-click variants.
- `0x519990` ctor, `0x519a00` set, `0x519a10` isDown, `0x519ac0` isPressed, `0x519b20` isReleased, `0x519b80` display name.
- Slider: `0x51a100` ctor, `0x51a160` value (1 mouse X, 2 mouse Y, 3 wheel, 4-9 joystick), `0x51a2c0` display name.

##### Action table
- 32 actions at `0x8a9168`, stride 0x29 (4 bindings 0-1 directions and 2-3 alternates, slider at +0x24), end `0x8a96ac`; static init `0x4caea0`.
- Index: 0 RUN, 1 STRAFE, 2 SHOOT, 3 RELOAD, 4 JUMP, 5 CROUCH, 6-9 DODGE left/right/forward/back, 10 DODGEMODIFIER, 11 AIMUP/DOWN `0x8a932b`,
  12 AIMLEFT/RIGHT `0x8a9354`, 13 USE `0x8a937d`, 14-24 weapon slots (`0x8a93a6`), 25 BESTWEAPON, 26 NEXT/PREV, 27 PAUSE `0x8a95bb`, 28 PAINKILLER,
  29 SNIPERZOOM, 30 SLOWMOTION, 31 BULLETTIME `0x8a965f`.
- Action methods: `0x4ce060` clear, `0x4ce0a0` isDown, `0x4ce0e0` isPressed, `0x4ce120` isReleased, `0x4ce160` slider value,
  `0x4ce170`/`0x4ce180` binding type/value, `0x4cdc00` set binding, `0x4cdc20`/`0x4cdc60` slider, `0x4cdc70` value, `0x4cdcb0` pressed value,
  `0x4cdcf0` binding text, `0x4cd5c0` rebind capture.
- Settings: defaults `0x4cb090`, load `0x4cb3d0` (registry "Input Settings"), save `0x4cb7d0`. Tags in texts: `0x4cce80` (#RUN# etc.).

##### Access handle (5 bytes {owner flag ptr, acquired})
- Is the object at PlayerInput+5 (`0x751da0` = `mov eax,[ecx+5]`). ctor `0x4cafc0`, init `0x4caff0`, release `0x4cb070` (refcount `0x8a9688`: first loads, last saves).
- Wrapper family `0x4cbc20`-`0x4ccbf0`: one method per (action, operation): value, down, pressed, released, capture, name, clear. All gated by `0x516d60`.
  Names are `input_access_<action>_<op>`; dodge triggers `0x4cc0c0/0x4cc190/0x4cc260/0x4cc330` add the modifier test `0x4cc400` and the sign of strafe/run.
- Settings cache: `0x8a968c` invertMouse, `0x8a968d` invertWheel, `0x8a9690` sensX, `0x8a9694` sensY, `0x8a9698` invertJoystick, `0x8a969c` joystick sensitivity.

##### MaxPayne_PlayerInput (vtable `0x7cf974`, 50 slots; base X_PlayerInputInterface vtable `0x7cfa40`)
- ctor `0x4ce4f0`, init `0x4ce630`, dtor `0x4ce590`, scalar deleting dtor `0x4ce570` (slot 49), shutdown `0x4ce880`, resetState `0x4cf310`.
- Slots: 0 `0x4ce8d0` run axis, 1 `0x4ce8e0` strafe axis, 2 `0x4ce8f0` shoot down, 3 `0x4ce900` shoot pressed, 4 `0x4ceb10` weapon-select poll, 5 `0x4cebe0` reset weapon select,
  6 `0x4ce910` reload, 7 `0x4ce920` jump, 8 `0x4ce930` crouch, 9-12 `0x4ce940/980/9c0/a00` jump + strafe/run side, 13 `0x4cea40` dodge modifier,
  14-17 `0x4cea70/80/90/a0` dodge L/R/F/B, 18 `0x4ceab0` aim up/down, 19 `0x4cead0` aim left/right, 20 `0x4ceaf0` use down, 21 `0x4ceb00` use pressed,
  22 `0x4cec20` sub-object thunk, 23 `0x4cf2e0` yaw delta, 24 `0x4cf2f0` pitch delta, 25 `0x4cf300` field +0x21 ptr, 26 `0x4cf2b0` clear weapon request,
  27 `0x4cf2d0` set weapon request, 28 `0x4cec60` reinit, 29 `0x4ceca0` reset, 30 `0x4cec30` clear look delta, 32 `0x4cecd0` update, 34 `0x4cf410` empty,
  47 `0x4cf420` load, 48 `0x4cf560` save. Slots 31, 33, 37, 39-45 are no-ops (`0x723bc0`/`0x723bd0`); 35/36 `0x64ab20`, 38 `0x646010`, 46 `0x422170` not read.
- Not in the vtable but called by `0x44c1b0`: `0x4cea50` bullet time down, `0x4cea60` slow-motion pressed, `0x4cec40` pause pressed, `0x4cec50` painkiller pressed.
- Fields: pending weapon flag +0x3a / index +0x3b, yaw delta +0x19, pitch delta +0x1d, byte +0x2d (aim axes return 0 when set: a "controls locked" flag, weak), access handle +5.

##### FlyCamera input helpers (`MaxPayne_FlyCamera`, vtable `0x7d0194`, update slot 34 = `0x508b70`)
- `0x508550` set target, `0x508580` yaw, `0x508610` pitch, `0x5086a0` roll ([ ]), `0x5086e0` forward, `0x5087c0` strafe, `0x508890` up/down (not read), `0x508910` enabled, `0x508920` holder init, `0x508930` speed multiplier, `0x508980` scale.

##### Mode key-press messages (0x40 bytes, 3 strings)
- `0x511430` ctor, `0x511590` load from script "globalmodesettings", `0x5114a0` free, `0x511900` release handles, `0x511940` resolve, `0x5119c0` send via `0x51b6a0` (X_MessageQueue::send).

#### Unknown or left unnamed
- `0x576440`, `0x576720`: chunk loader/writer (openChunk, matrix array stride 0x34), not input; table misclassification.
- `0x42a4d0`, `0x42a4e0`: tiny getters (+0x1c2, +0x1c6) of an unknown class; `0x42a4e0`-`0x42b7ef` "controls class" has no function entries.
- `0x4cb3d0`/`0x4cb7d0` bodies read by strings only; `0x4cf060` and `0x4ceb10` only partly read.
- PlayerInput slots 35, 36, 38, 46 (`0x64ab20`, `0x646010`, `0x422170`) are shared code outside the area.
- Names of tiny shared COMDAT stubs (`shared_*`) are by shape only; their real owner classes differ.

## Retail sniper zoom correction (2026-11-09)

- FACT: `0x44b6c3..0x44b6db` constructs `MaxPayne_PlayerInput` at GM+0x258;
  `0x4ce550` writes vtable `0x7cf974`. Slot +0x50 at `0x7cf9c4` is `0x4ceaf0`,
  which calls `0x751da0` and jumps to `0x4cc600`. This is the held Use wrapper,
  action `0x8a937d`, not the action named SNIPERZOOM (`0x8a960d`).
- FACT: `0x4cc625 -> 0x4ce0a0 -> 0x519a10 -> 0x5184c0` reads the keyboard
  state byte at keyboard+9+DIK. The zoom switch at `0x44d375` needs a held key,
  then release to retain zoom (states 0 -> 1 -> 2), then a second press to exit.
  Default Use is E (DIK 0x12) or Enter (0x1c). No buffered press edge or mouse
  button is required for these keyboard bindings. Four bindings remain 9 bytes each.
- FACT: action 29 has only five immediate references in this retail executable:
  two default setters, capture, display and clear. No gameplay reader found. Q being
  initialized at `0x4cb2c9` does not establish that gameplay uses it.
- FACT: web/input.js maps KeyE to 0x12 and KeyQ to 0x10. host/input.c tracks
  down state and sends buffered transitions; host/dinput8.c GetDeviceState
  already writes 0x80 for held keys, matching the original `0x5184c0` check.
- Prior art: Wine keyboard_handle_event stores 0x80/0 and independently queues
  changed DIK events: https://github.com/wine-mirror/wine/blob/master/dlls/dinput/keyboard.c#L80-L108
  The original manual says Sniper Zoom = Use Key:
  https://www.manuallib.com/download/2023-10-26/MAX%20PAYNE%20%E6%89%8B%E5%86%8C.pdf
  Community reproduction: https://steamcommunity.com/app/12140/discussions/0/4558288923613011044/
- Decision: preserve host DIK delivery and the retail game. Aliasing Q onto E or
  rewriting slot +0x50 would alter retail behavior without a host defect.
  Disassembly evidence: out/sniper_1109/input-re/dispatch.asm.
