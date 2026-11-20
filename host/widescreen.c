// Widescreen: the 3D view shows more at the sides instead of stretching 4:3 (Hor+). Same math and sites as ThirteenAG's
// MaxPayne.WidescreenFix (MIT): source/MaxPayne.WidescreenFix/e2mfc.ixx P_Camera::validate, dllmain.cpp
// getFirstSceneToRenderHook / getSceneToRenderHook. Its byte patterns match our 1.05 exe once each. patches.py MID_HOOKS
// runs these before the instruction at the address. Decompiled sites agree (rea/map/widescreen.md).
#include "rt.h"
#include "host.h"

#define SPRITE_WIDTH 0x144        /* P_Sprite width, ComVars.ixx */

static float scale_x = 1.f, scale_y = 1.f;

// The size of the back buffer, from CreateDevice and Reset (host/d3d8.c). A 4:3 mode leaves the game untouched.
void rn_ws_size(uint32_t w, uint32_t h) {
    if (!w || !h) return;
    float aspect = (float)w / (float)h;
    scale_x = aspect > 4.f / 3.f ? aspect * 3.f / 4.f : 1.f;
    scale_y = aspect < 4.f / 3.f ? 4.f / (3.f * aspect) : 1.f;
}

// e2mfc P_Camera::validate 0x10114d60: 0x10114d7d stored tan(fov / 2) at +0x200 (X), 0x10114d9f the same times the
// viewport ratio at +0x204 (Y). Every reader of the projection takes these two tangents. Hook: the fstp st(0) 0x10114da5.
void rn_ws_camera(Ctx *c) {
    uint32_t cam = c->r[ECX];
    float x = ldf(c, cam + 0x200) * scale_x, y = ldf(c, cam + 0x204) * scale_y;
    memcpy(c->mem + cam + 0x200, &x, 4);
    memcpy(c->mem + cam + 0x204, &y, 4);
}

// Portal culling derives its frustum from the camera FOV and not from the tangents above; unscaled, rooms seen through a
// portal are clipped to the 4:3 part of the screen.
// getFirstSceneToRender 0x67a12d: [ebp-0x10] = tan(fov / 2), st(0) = it times the viewport height.
void rn_ws_portal_first(Ctx *c) {
    uint32_t a = c->r[EBP] - 0x10;
    float t = ldf(c, a) * scale_x;
    memcpy(c->mem + a, &t, 4);
    *fr(c, 0) *= scale_y;
}

// getSceneToRender 0x67aeba: st(1) = tan(fov / 2), st(0) = it times the viewport height.
void rn_ws_portal_next(Ctx *c) {
    *fr(c, 1) *= scale_x;
    *fr(c, 0) *= scale_y;
}

// e2mfc P_Sprite::executeAlways 0x10108594, after the pivot is subtracted: st(1) = -pivot X, [ebp-4] = width - pivot X.
// Fades are 640 wide at 0,0 and would cover the centered 4:3 part only (dllmain.cpp P_HudPosHook: 0x1000856c unrebased).
// The sniper scope pieces, which the fix also widens, are not moved here.
void rn_ws_sprite(Ctx *c) {
    if (scale_x <= 1.f || c->r[EAX] != 2 /* P_BitmapInterface top left */ || ldf(c, 0x10165190) != 0.f || ldf(c, 0x10165194) != 0.f
        || ldf(c, c->r[ESI] + SPRITE_WIDTH) != 640.f) return;
    float ext = (scale_x - 1.f) * 320.f;
    if (ext < 640.f) ext = 640.f;           // what the fix keeps: at least 640 each side
    *fr(c, 1) -= ext;
    float w = ldf(c, c->r[EBP] - 4) + ext;
    memcpy(c->mem + c->r[EBP] - 4, &w, 4);
}
