// ws2_32 / tapi32 / netapi32: a PC with no network. Winsock starts, but every socket operation
// fails with WSAENETDOWN, and there are no modems or NetBIOS adapters.
#include "host.h"

#define WSAENETDOWN 10050
static uint32_t last_error;
static uint32_t fail(Ctx *c) { (void)c; last_error = WSAENETDOWN; return 0xffffffffu; }   // SOCKET_ERROR / INVALID_SOCKET

static uint32_t WSAStartup_(Ctx *c) {
    uint32_t ver = ARG(0) & 0xffff, p = ARG(1);
    memset(G(p), 0, 400);
    st(c, p, ver, 2);           // wVersion
    st(c, p + 2, 0x0202, 2);    // wHighVersion
    memcpy(G(p + 4), "WinSock 2.0", 12);
    memcpy(G(p + 261), "Running", 8);
    return 0;
}
static uint32_t WSACleanup_(Ctx *c) { (void)c; return 0; }
static uint32_t WSAGetLastError_(Ctx *c) { (void)c; return last_error; }
static uint32_t gethostbyname_(Ctx *c) { (void)c; last_error = WSAENETDOWN; return 0; }
static uint32_t htons_(Ctx *c) { uint32_t v = ARG(0) & 0xffff; return ((v & 0xff) << 8) | (v >> 8); }
static uint32_t htonl_(Ctx *c) { return __builtin_bswap32(ARG(0)); }

// TAPI: lineInitialize reports no devices; netapi32 Netbios: NRC_NOWILD-style "no adapter".
static uint32_t lineInitialize_(Ctx *c) { if (ARG(4)) st(c, ARG(4), 0, 4); return 0; }   // 0 line devices
static uint32_t lineShutdown_(Ctx *c) { (void)c; return 0; }
static uint32_t line_bad_device(Ctx *c) { (void)c; return 0x80000010u; }
static uint32_t line_bad_handle(Ctx *c) { (void)c; return 0x8000002bu; }
static uint32_t Netbios_(Ctx *c) {
    uint32_t ncb = ARG(0);
    st(c, ncb + 1, 0x23, 1);    // ncb_retcode = NRC_BRIDGE: invalid LAN adapter number
    return 0x23;
}

#define W(ord, fn, n) {"ws2_32.dll", "#" #ord, n, fn}
const Api net_apis[] = {
    W(115, WSAStartup_, 2), W(116, WSACleanup_, 0), W(111, WSAGetLastError_, 0), W(52, gethostbyname_, 1),
    W(8, htonl_, 1), W(14, htonl_, 1), W(9, htons_, 1), W(15, htons_, 1),
    W(23, fail, 3), W(22, fail, 2), W(2, fail, 3), W(4, fail, 3), W(10, fail, 3), W(21, fail, 5), W(3, fail, 1),
    W(13, fail, 2), W(1, fail, 3), W(20, fail, 6), W(19, fail, 4), W(17, fail, 6), W(16, fail, 4), W(7, fail, 5),
    W(18, fail, 5), W(5, fail, 3), W(6, fail, 3), {"ws2_32.dll", "WSAIoctl", 9, fail},
    {"tapi32.dll", "lineInitialize", 5, lineInitialize_}, {"tapi32.dll", "lineShutdown", 1, lineShutdown_},
    // lineInitialize reports 0 devices, so any device id is bad (LINEERR_BADDEVICEID / LINEERR_INVALLINEHANDLE).
    {"tapi32.dll", "lineNegotiateAPIVersion", 6, line_bad_device}, {"tapi32.dll", "lineGetDevCaps", 5, line_bad_device},
    {"tapi32.dll", "lineOpen", 9, line_bad_device}, {"tapi32.dll", "lineGetID", 6, line_bad_handle},
    {"tapi32.dll", "lineMakeCall", 5, line_bad_handle}, {"tapi32.dll", "lineAnswer", 3, line_bad_handle},
    {"tapi32.dll", "lineClose", 1, line_bad_handle},
    {"netapi32.dll", "Netbios", 1, Netbios_},
};
const int net_api_count = sizeof net_apis / sizeof net_apis[0];
