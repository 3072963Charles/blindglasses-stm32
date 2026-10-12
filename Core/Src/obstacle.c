#include "obstacle.h"
#include "stm32h7xx_hal.h"
#include "jq8900.h"
#include <stdbool.h>
#include <stdio.h>

/* ================================================================
 * Thresholds & constants
 * ================================================================ */
#define TOF_EMERGENCY_CM       30
#define TOF_WARNING_CM         80
#define URM09_EMERGENCY_CM     50
#define URM09_WARNING_CM       200
#define BOTTOM_EMERGENCY_CM    50    // stair edge / steep drop
#define BOTTOM_WARNING_CM      100   // uneven ground ahead
#define COOLDOWN_MS            2000
#define EMERGENCY_RECUR_MS     500
#define LEVEL_REPEAT_MS        3000  // repeat same-level alert every 3s

/* ---- voice arbitration ---- */
#define DEBOUNCE_CYCLES        3     // sensor state must persist 3 cycles (300ms)
#define DEBOUNCE_EMERGENCY     1     // emergencies debounce faster (100ms)
#define GLOBAL_COOLDOWN_MS     800   // minimum silence gap between announcements

/* ---- per-direction cooldown tracking ---- */
typedef enum {
    DIR_FRONT,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_BOTTOM,
    DIR_COUNT
} Direction;

typedef struct {
    uint32_t last_tick;
    uint8_t  last_track;
} DirState;

/* ---- zone ---- */
typedef enum {
    ZONE_SAFE = 0,
    ZONE_WARNING,
    ZONE_EMERGENCY
} Zone;

/* ---- voice priority (lower = higher priority) ---- */
typedef enum {
    PRIO_EMERGENCY = 0,   // P0: emergency tracks (1,3,5,7,9)
    PRIO_WARNING   = 1,   // P1: warning tracks (2,4,6,10,11)
    PRIO_INFO      = 2    // P2: safe (8), repeat broadcasts
} VoicePriority;

/* ---- voice track labels ---- */
static const char *track_label(uint8_t t)
{
    switch (t) {
    case 1:  return "前方危险，请停下";
    case 2:  return "前方注意";
    case 3:  return "左侧有障碍，请向右";
    case 4:  return "左侧注意";
    case 5:  return "右侧有障碍，请向左";
    case 6:  return "右侧注意";
    case 7:  return "注意脚下，有台阶";
    case 8:  return "前方安全，可以通行";
    case 9:  return "前方受阻，请停下";
    case 10: return "两侧狭窄，小心通过";
    case 11: return "下方注意";
    default: return "";
    }
}

static const char *zone_tag(Zone z)
{
    switch (z) {
    case ZONE_EMERGENCY: return "\033[31m!!\033[0m";
    case ZONE_WARNING:   return "\033[33m~~\033[0m";
    default:             return "\033[32m--\033[0m";
    }
}

/* ================================================================
 * Voice priority helpers
 * ================================================================ */
static VoicePriority get_track_priority(uint8_t track)
{
    switch (track) {
    case JQ8900_TRACK_FRONT_EMERGENCY:   // 1
    case JQ8900_TRACK_LEFT_EMERGENCY:    // 3
    case JQ8900_TRACK_RIGHT_EMERGENCY:   // 5
    case JQ8900_TRACK_BOTTOM_EMERGENCY:  // 7
    case JQ8900_TRACK_THREE_SIDE_BLOCK:  // 9
        return PRIO_EMERGENCY;
    case JQ8900_TRACK_FRONT_WARNING:     // 2
    case JQ8900_TRACK_LEFT_WARNING:      // 4
    case JQ8900_TRACK_RIGHT_WARNING:     // 6
    case JQ8900_TRACK_BOTH_SIDE_NARROW:  // 10
    case JQ8900_TRACK_BOTTOM_WARNING:    // 11
        return PRIO_WARNING;
    default:
        return PRIO_INFO;                // track 8 (safe), repeat, etc.
    }
}

/* ---- persistent state ---- */
static DirState g_dir[DIR_COUNT];
static uint16_t  g_prev_tof[4];
static float     g_prev_urm09;
static uint8_t   g_prev_track;
static uint32_t  g_last_alert_tick;
static uint8_t   g_print_skip;        // throttle full-print to every N cycles
static uint8_t   g_current_level = 3; // 1=DANGER, 2=WARNING, 3=SAFE
static uint32_t  g_last_level_tick;   // for repeat broadcast timer

/* ---- debounce state ---- */
static uint8_t   g_debounce_track;     // track candidate being debounced
static uint8_t   g_debounce_count;     // consecutive cycles counting this track

/* ---- voice arbitration state ---- */
static uint8_t   g_queued_track;       // track waiting to play (0=none)
static uint32_t  g_global_cooldown_end; // HAL tick when silence gap ends
static bool      g_was_busy;           // JQ8900 busy state from previous cycle

/* ================================================================
 * Helpers
 * ================================================================ */
static Zone classify_tof(uint16_t mm)
{
    if (mm == 0 || mm > 2000) return ZONE_SAFE;
    uint16_t cm = mm / 10;
    if (cm < TOF_EMERGENCY_CM) return ZONE_EMERGENCY;
    if (cm < TOF_WARNING_CM)   return ZONE_WARNING;
    return ZONE_SAFE;
}

static Zone classify_urm09(float cm)
{
    if (cm <= 0.0f || cm > 500.0f) return ZONE_SAFE;
    if (cm < URM09_EMERGENCY_CM) return ZONE_EMERGENCY;
    if (cm < URM09_WARNING_CM)   return ZONE_WARNING;
    return ZONE_SAFE;
}

/** Bottom TOF uses its own thresholds (stair/ground hazard). */
static Zone classify_bottom(uint16_t mm)
{
    if (mm == 0 || mm > 2000) return ZONE_SAFE;
    uint16_t cm = mm / 10;
    if (cm < BOTTOM_EMERGENCY_CM) return ZONE_EMERGENCY;
    if (cm < BOTTOM_WARNING_CM)   return ZONE_WARNING;
    return ZONE_SAFE;
}

static bool decreasing_tof(int i, uint16_t cur)
{
    if (cur == 0 || cur > 2000) return false;
    if (g_prev_tof[i] == 0 || g_prev_tof[i] > 2000) return false;
    return cur < g_prev_tof[i];
}

static bool decreasing_urm09(float cur)
{
    if (cur <= 0.0f || cur > 500.0f) return false;
    if (g_prev_urm09 <= 0.0f || g_prev_urm09 > 500.0f) return false;
    return cur < g_prev_urm09;
}

static Direction track_dir(uint8_t t)
{
    if (t <= 2)   return DIR_FRONT;
    if (t <= 4)   return DIR_LEFT;
    if (t <= 6)   return DIR_RIGHT;
    if (t == 7)   return DIR_BOTTOM;
    if (t == 11)  return DIR_BOTTOM;
    return DIR_FRONT;
}

/* ================================================================
 * Voice arbitration
 * ================================================================ */

/** Send a track to the JQ8900 module and update all bookkeeping. */
static void play_track_now(uint8_t track, uint32_t now)
{
    JQ8900_Play(track);
    g_was_busy = true;
    g_queued_track = 0;
    g_global_cooldown_end = 0;

    Direction dir = track_dir(track);
    g_dir[dir].last_tick  = now;
    g_dir[dir].last_track = track;
    g_last_alert_tick     = now;
    g_prev_track          = track;
    g_last_level_tick     = now;

    printf("  >>> Track %d: \"%s\" <<<\r\n", track, track_label(track));
}

/**
 * Try to play or queue a track through the arbitration system.
 * Returns true if the track was played immediately.
 */
static bool arbitrate_voice(uint8_t track, uint32_t now)
{
    if (track == 0) return false;

    /* ---- 0. If same track is already playing, skip ---- */
    uint16_t playing = JQ8900_GetPlayingTrack();
    if (playing > 0 && track == (uint8_t)playing) {
        return false;
    }

    VoicePriority new_prio = get_track_priority(track);
    bool jq_busy = JQ8900_IsBusy();

    if (!jq_busy) {
        /* ---- 1. JQ8900 is idle ---- */
        if (now >= g_global_cooldown_end) {
            /* cooldown elapsed — play now */
            play_track_now(track, now);
            return true;
        } else {
            /* still in cooldown gap — queue it */
            if (g_queued_track == 0
                || new_prio < get_track_priority(g_queued_track)) {
                g_queued_track = track;
                printf("  [QUEUE] Track %d queued (cooldown)\r\n", track);
            }
            return false;
        }
    } else {
        /* ---- 2. JQ8900 is busy playing ---- */
        VoicePriority cur_prio = get_track_priority((uint8_t)playing);

        if (new_prio < cur_prio) {
            /* higher priority — interrupt */
            JQ8900_Stop();
            play_track_now(track, now);
            printf("  [PREEMPT] Track %d interrupted for Track %d\r\n",
                   (int)playing, track);
            return true;
        } else if (new_prio == cur_prio && track != (uint8_t)playing) {
            /* same priority, different track — queue (don't interrupt) */
            if (g_queued_track == 0
                || new_prio < get_track_priority(g_queued_track)) {
                g_queued_track = track;
                printf("  [QUEUE] Track %d queued (busy, same prio)\r\n",
                       track);
            }
            return false;
        } else {
            /* lower priority — discard */
            return false;
        }
    }
}

/** Called every cycle to drain the queue when JQ8900 becomes idle. */
static void process_queue(uint32_t now)
{
    bool jq_busy = JQ8900_IsBusy();

    /* detect falling edge: was busy, now idle */
    if (g_was_busy && !jq_busy) {
        /* start global cooldown */
        g_global_cooldown_end = now + GLOBAL_COOLDOWN_MS;
    }
    g_was_busy = jq_busy;

    /* if idle, cooldown done, and a track is queued — play it */
    if (!jq_busy && g_queued_track > 0 && now >= g_global_cooldown_end) {
        uint8_t track = g_queued_track;
        g_queued_track = 0;
        printf("  [DEQUEUE] Playing queued Track %d\r\n", track);
        play_track_now(track, now);
    }
}

/* ================================================================
 * Core decision engine
 *
 * Sensor layout (tof_mm indices):
 *   [0] = Front TOF  (S0)  – supplementary to URM09
 *   [1] = Left  TOF  (S1)
 *   [2] = Right TOF  (S2)
 *   [3] = Bottom TOF (S3)  – ground / stair detection
 * ================================================================ */
void Obstacle_Process(float urm09_cm, const uint16_t tof_mm[4])
{
    float s[4];
    for (int i = 0; i < 4; i++) s[i] = tof_mm[i] / 10.0f;

    /* ---- classify (with escalation) ---- */
    Zone zu = classify_urm09(urm09_cm);
    if (zu == ZONE_WARNING && decreasing_urm09(urm09_cm)) zu = ZONE_EMERGENCY;

    // z[0] = Front TOF, z[1] = Left, z[2] = Right (use standard thresholds)
    // z[3] = Bottom TOF (uses bottom-specific thresholds)
    Zone z[4];
    z[0] = classify_tof(tof_mm[0]);
    if (z[0] == ZONE_WARNING && decreasing_tof(0, tof_mm[0]))
        z[0] = ZONE_EMERGENCY;

    z[1] = classify_tof(tof_mm[1]);
    if (z[1] == ZONE_WARNING && decreasing_tof(1, tof_mm[1]))
        z[1] = ZONE_EMERGENCY;

    z[2] = classify_tof(tof_mm[2]);
    if (z[2] == ZONE_WARNING && decreasing_tof(2, tof_mm[2]))
        z[2] = ZONE_EMERGENCY;

    z[3] = classify_bottom(tof_mm[3]);
    if (z[3] == ZONE_WARNING && decreasing_tof(3, tof_mm[3]))
        z[3] = ZONE_EMERGENCY;

    /* ---- front zone: URM09 primary, Front TOF (S0) supplementary ---- */
    Zone zf = zu;
    if (zf < z[0]) zf = z[0];  // promote front zone if TOF sees closer danger

    /* ---- composite detection ---- */
    bool front_trig   = (zf >= ZONE_WARNING);
    bool left_trig    = (z[1] >= ZONE_WARNING);
    bool right_trig   = (z[2] >= ZONE_WARNING);
    bool three_sided  = front_trig && left_trig && right_trig
                        && (zf == ZONE_EMERGENCY || z[1] == ZONE_EMERGENCY
                            || z[2] == ZONE_EMERGENCY);
    bool front_left   = (zf == ZONE_EMERGENCY && z[1] == ZONE_EMERGENCY);
    bool front_right  = (zf == ZONE_EMERGENCY && z[2] == ZONE_EMERGENCY);

    uint8_t  track           = 0;
    bool     is_emergency    = false;
    const char *guidance     = NULL;

    /* ===== priority decision tree ===== */

    if (three_sided) {
        track = JQ8900_TRACK_THREE_SIDE_BLOCK;  // 9
        is_emergency = true;
    }
    if (track == 0 && front_left) {
        track = JQ8900_TRACK_FRONT_EMERGENCY;   // 1
        is_emergency = true;
        if (z[2] == ZONE_SAFE) guidance = "右侧安全，可向右转";
    }
    if (track == 0 && front_right) {
        track = JQ8900_TRACK_FRONT_EMERGENCY;   // 1
        is_emergency = true;
        if (z[1] == ZONE_SAFE) guidance = "左侧安全，可向左转";
    }
    if (track == 0 && zf == ZONE_EMERGENCY) {
        track = JQ8900_TRACK_FRONT_EMERGENCY;   // 1
        is_emergency = true;
        if (z[1] == ZONE_SAFE && z[2] > ZONE_SAFE)
            guidance = "左侧通畅，建议向左转";
        else if (z[2] == ZONE_SAFE && z[1] > ZONE_SAFE)
            guidance = "右侧通畅，建议向右转";
    }
    if (track == 0 && z[3] == ZONE_EMERGENCY) {
        track = JQ8900_TRACK_BOTTOM_EMERGENCY;  // 7
        is_emergency = true;
    }
    if (track == 0 && z[1] == ZONE_EMERGENCY) {
        track = JQ8900_TRACK_LEFT_EMERGENCY;    // 3
        is_emergency = true;
    }
    if (track == 0 && z[2] == ZONE_EMERGENCY) {
        track = JQ8900_TRACK_RIGHT_EMERGENCY;   // 5
        is_emergency = true;
    }
    if (track == 0 && zf == ZONE_WARNING) {
        track = JQ8900_TRACK_FRONT_WARNING;     // 2
    }
    if (track == 0 && z[3] == ZONE_WARNING) {
        track = JQ8900_TRACK_BOTTOM_WARNING;    // 11
    }
    if (track == 0 && z[1] == ZONE_WARNING && z[2] == ZONE_WARNING) {
        track = JQ8900_TRACK_BOTH_SIDE_NARROW;  // 10
    }
    if (track == 0 && z[1] == ZONE_WARNING) {
        track = JQ8900_TRACK_LEFT_WARNING;      // 4
    }
    if (track == 0 && z[2] == ZONE_WARNING) {
        track = JQ8900_TRACK_RIGHT_WARNING;     // 6
    }

    uint32_t now = HAL_GetTick();

    /* ===== debounce filter ===== */
    if (track > 0) {
        if (track == g_debounce_track) {
            g_debounce_count++;
        } else {
            g_debounce_track = track;
            g_debounce_count = 1;
        }
    } else {
        /* no obstacle — reset debounce immediately */
        g_debounce_track = 0;
        g_debounce_count = 0;
    }

    uint8_t required_cycles = is_emergency ? DEBOUNCE_EMERGENCY
                                           : DEBOUNCE_CYCLES;
    bool debounced = (track > 0 && g_debounce_count >= required_cycles);

    /* ===== per-direction cooldown ===== */
    bool cooldown_ok = true;
    if (track > 0 && debounced) {
        Direction dir = track_dir(track);
        if (is_emergency) {
            if (track == g_prev_track
                && (now - g_last_alert_tick) < EMERGENCY_RECUR_MS)
                cooldown_ok = false;
        } else {
            if (track == g_dir[dir].last_track
                && (now - g_dir[dir].last_tick) < COOLDOWN_MS)
                cooldown_ok = false;
        }
    }

    /* ===== debug print ===== */
    g_print_skip++;
    if (g_print_skip >= 5 || track > 0) {
        g_print_skip = 0;
        printf("  [OBST] U:%-5.1f %s  F:%-5.1f %s  L:%-5.1f %s  "
               "R:%-5.1f %s  B:%-5.1f %s  |  Fz:%s  Deb:%d/%d\r\n",
               urm09_cm, zone_tag(zu),
               s[0], zone_tag(z[0]), s[1], zone_tag(z[1]),
               s[2], zone_tag(z[2]), s[3], zone_tag(z[3]),
               zf == ZONE_EMERGENCY ? "E" : zf == ZONE_WARNING ? "W" : "S",
               g_debounce_count, required_cycles);
    }

    /* ---- compute global level from worst zone ---- */
    uint8_t new_level = 3;
    Zone worst_zones[] = {zu, z[0], z[1], z[2], z[3]};
    for (int i = 0; i < 5; i++) {
        if (worst_zones[i] == ZONE_EMERGENCY) { new_level = 1; break; }
        if (worst_zones[i] == ZONE_WARNING)   new_level = 2;
    }

    /* ===== voice output (through arbitration) ===== */
    if (track > 0 && debounced && cooldown_ok) {
        bool played = arbitrate_voice(track, now);
        if (played && guidance) {
            printf("  [GUIDE] %s\r\n", guidance);
        }
    } else if (track > 0 && debounced && !cooldown_ok) {
        printf("  [COOLDOWN] Track %d suppressed\r\n", track);
    } else if (track > 0 && !debounced) {
        /* debounce in progress — silent */
    }

    /* ---- level-change announcement (through arbitration) ---- */
    if (new_level != g_current_level) {
        if (new_level == 3) {
            /* Transition to SAFE: play track 8 exactly once.
             * Only update g_current_level after the track actually plays,
             * so we don't lose the announcement if JQ8900 is busy. */
            if (!JQ8900_IsBusy() && now >= g_global_cooldown_end) {
                play_track_now(JQ8900_TRACK_SAFE, now);
                printf("  >>> Level 3: \"前方安全\" played <<<\r\n");
                g_current_level   = 3;
                g_last_level_tick = now;
            }
            /* else: busy or cooldown — retry next cycle */
        } else {
            /* Danger/Warning transition: update immediately */
            g_current_level   = new_level;
            g_last_level_tick = now;
        }
    } else if (new_level < 3 && track == 0 &&
               (now - g_last_level_tick) >= LEVEL_REPEAT_MS) {
        /* No new obstacle but still in danger/warning — repeat broadcast */
        uint8_t repeat_track = (new_level == 1) ? JQ8900_TRACK_DANGER
                                                : JQ8900_TRACK_WARNING;
        arbitrate_voice(repeat_track, now);
        g_last_level_tick = now;
        printf("  [REPEAT] Level %d re-announce requested\r\n", new_level);
    }

    /* ---- process queue (play queued track if JQ8900 became idle) ---- */
    process_queue(now);

    /* ===== persist for next cycle ===== */
    for (int i = 0; i < 4; i++) g_prev_tof[i] = tof_mm[i];
    g_prev_urm09 = urm09_cm;
}

uint8_t Obstacle_GetLevel(void)
{
    return g_current_level;
}

uint8_t Obstacle_GetCurrentTrack(void)
{
    uint16_t t = JQ8900_GetPlayingTrack();
    return (t > 0) ? (uint8_t)t : g_prev_track;
}
