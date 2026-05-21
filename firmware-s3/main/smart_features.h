#pragma once
#include <stdbool.h>
#include "relay_packet.h"

// Alert thresholds
#define ALERT_COOLANT_WARN    95
#define ALERT_COOLANT_CRIT    105
#define ALERT_RPM_HIGH        8000
#define ALERT_SPEED_LIMIT     120

// Trip states
typedef enum {
    TRIP_IDLE,
    TRIP_STARTING,
    TRIP_ACTIVE,
    TRIP_ENDING,
} trip_state_t;

typedef struct {
    trip_state_t state;
    int64_t      start_us;
    int64_t      end_us;
    uint32_t     trip_count;
    uint32_t     active_seconds;
} trip_info_t;

typedef struct {
    bool coolant_warn;
    bool coolant_crit;
    bool rpm_high;
    bool speed_limit;
    bool any_active;
} alert_state_t;

extern trip_info_t g_trip;
extern alert_state_t g_alerts;

void smart_features_init(void);
void smart_features_update(const relay_packet_t *pkt);
float smart_get_board_temp(void);
