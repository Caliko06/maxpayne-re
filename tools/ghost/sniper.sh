#!/bin/sh
# Retail zoom is held Use (0x44d375 -> 0x4ceaf0 -> 0x4cc600), not the unused Q action at 0x8a960d.
# Test port 5193 only; ghost owns its headless, muted browser and temporary server.
set -eu
cd "$(dirname "$0")/../.."
node tools/ghost/ghost.mjs --port 5193 --secs 90 --out "${1:-out/sniper_1109/run3/hit}" --plan watch --camera-frames 4 --shots jpg \
  --env 'RN_JUMP=maxpayne_gamemode->gm_init(part1_level1);|x_modeswitch->s_modeswitch(game);|player->c_setinvulnerable(true);|player->C_Teleport(::p5::w1);|player->c_pickupweapon(sniper);|player->c_pickupammo(sniper,10);|::to_storage01::enemy_trigger.TRIGGER->T_Activate();|@w:60|@k:6:10|@w:30|@f:1:30|@w:60|@aim:0.0|@w:60|@k:E:30|@w:30|@m:sniper_shot|@f:1:60|@w:180|@m:control_return|@k:W:30|@w:60|@m:end,RN_SYS=50,RN_SOUNDLOG=1,RN_SECS=100'
