#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cmath>
#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_truck_common_channels.h"
#include "common/scssdk_telemetry_common_channels.h"

// Read-only SCS telemetry. No input hooks, memory scanning or game-file writes.
static SOCKET channel = INVALID_SOCKET;
static sockaddr_in destination{};
static bool winsock_ready = false, paused = true, placed = false;
static scs_value_dplacement_t placement{};
static float speed = 0, scale = 1;
static ULONGLONG last_send = 0;
static unsigned sequence = 0;

static void cleanup() {
    if (channel != INVALID_SOCKET) { closesocket(channel); channel = INVALID_SOCKET; }
    if (winsock_ready) { WSACleanup(); winsock_ready = false; }
}
static void transmit(bool force = false) {
    const auto now = GetTickCount64();
    if (channel == INVALID_SOCKET || (!force && now - last_send < 100)) return;
    last_send = now;
    char message[512];
    const int size = snprintf(message, sizeof(message),
        "{\"protocol\":1,\"source\":\"ets2-lane-guide\",\"sequence\":%u,\"connected\":true,"
        "\"paused\":%s,\"placed\":%s,\"x\":%.5f,\"y\":%.5f,\"z\":%.5f,"
        "\"heading\":%.8f,\"speed\":%.4f,\"scale\":%.4f}",
        ++sequence, paused ? "true" : "false", placed ? "true" : "false",
        placement.position.x, placement.position.y, placement.position.z,
        placement.orientation.heading, speed, scale);
    if (size > 0 && size < sizeof(message))
        sendto(channel, message, size, 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
}
static SCSAPI_VOID on_position(const scs_string_t, const scs_u32_t,
    const scs_value_t* value, const scs_context_t) {
    placed = value && value->type == SCS_VALUE_TYPE_dplacement;
    if (placed) placement = value->value_dplacement;
}
static SCSAPI_VOID on_float(const scs_string_t, const scs_u32_t,
    const scs_value_t* value, const scs_context_t context) {
    if (value && value->type == SCS_VALUE_TYPE_float)
        *static_cast<float*>(context) = value->value_float.value;
}
static SCSAPI_VOID on_frame(const scs_event_t, const void*, const scs_context_t) { transmit(); }
static SCSAPI_VOID on_pause(const scs_event_t event, const void*, const scs_context_t) {
    paused = event == SCS_TELEMETRY_EVENT_paused;
    transmit(true);
}

SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version, const scs_telemetry_init_params_t* params) {
    if (version != SCS_TELEMETRY_VERSION_1_01 && version != SCS_TELEMETRY_VERSION_1_00)
        return SCS_RESULT_unsupported;
    cleanup();
    paused = true; placed = false; placement = {}; speed = 0; scale = 1;
    last_send = 0; sequence = 0;
    const auto* api = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    WSADATA info;
    if (WSAStartup(MAKEWORD(2, 2), &info)) return SCS_RESULT_generic_error;
    winsock_ready = true;
    channel = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (channel == INVALID_SOCKET) { cleanup(); return SCS_RESULT_generic_error; }
    u_long nonblocking = 1;
    if (ioctlsocket(channel, FIONBIO, &nonblocking)) { cleanup(); return SCS_RESULT_generic_error; }
    destination.sin_family = AF_INET;
    destination.sin_port = htons(37539);
    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool registered =
        api->register_for_event(SCS_TELEMETRY_EVENT_frame_end, on_frame, nullptr) == SCS_RESULT_ok &&
        api->register_for_event(SCS_TELEMETRY_EVENT_paused, on_pause, nullptr) == SCS_RESULT_ok &&
        api->register_for_event(SCS_TELEMETRY_EVENT_started, on_pause, nullptr) == SCS_RESULT_ok &&
        api->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL,
            SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_no_value, on_position, nullptr) == SCS_RESULT_ok &&
        api->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_speed, SCS_U32_NIL,
            SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, on_float, &speed) == SCS_RESULT_ok;
    if (!registered) {
        api->common.log(SCS_LOG_TYPE_error, "[Lane Guide] Required telemetry channel registration failed.");
        cleanup(); return SCS_RESULT_generic_error;
    }
    api->register_for_channel(SCS_TELEMETRY_CHANNEL_local_scale, SCS_U32_NIL,
        SCS_VALUE_TYPE_float, SCS_TELEMETRY_CHANNEL_FLAG_none, on_float, &scale);
    api->common.log(SCS_LOG_TYPE_message, "[Lane Guide] Read-only telemetry ready on localhost UDP 37539.");
    transmit(true);
    return SCS_RESULT_ok;
}
SCSAPI_VOID scs_telemetry_shutdown() {
    if (channel != INVALID_SOCKET) {
        const char end[] = "{\"protocol\":1,\"source\":\"ets2-lane-guide\",\"connected\":false}";
        sendto(channel, end, sizeof(end) - 1, 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    }
    cleanup();
}
