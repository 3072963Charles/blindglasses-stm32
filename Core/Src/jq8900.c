#include "jq8900.h"

static UART_HandleTypeDef *_huart;

/* ---- playback busy tracking ---- */
static uint16_t  _playing_track;     // currently playing track (0 = idle)
static uint32_t  _play_end_tick;     // HAL_GetTick() when playback finishes

/** Return estimated duration in ms for a given track index. */
static uint32_t get_duration(uint16_t index)
{
    switch (index) {
    case JQ8900_TRACK_LEFT_EMERGENCY:     // 3
    case JQ8900_TRACK_RIGHT_EMERGENCY:    // 5
        return JQ8900_DURATION_LONG;
    case JQ8900_TRACK_FRONT_EMERGENCY:    // 1
    case JQ8900_TRACK_BOTTOM_EMERGENCY:   // 7
    case JQ8900_TRACK_SAFE:              // 8
    case JQ8900_TRACK_THREE_SIDE_BLOCK:  // 9
    case JQ8900_TRACK_BOTH_SIDE_NARROW:  // 10
        return JQ8900_DURATION_NORMAL;
    default:
        return JQ8900_DURATION_SHORT;    // 2, 4, 6, 11
    }
}

void JQ8900_Init(UART_HandleTypeDef *huart) {
    _huart = huart;
    _playing_track = 0;
    _play_end_tick = 0;
}

/** Return estimated duration in ms for a given track index. */
bool JQ8900_IsBusy(void)
{
    if (_playing_track == 0) return false;
    if (HAL_GetTick() >= _play_end_tick) {
        _playing_track = 0;
        return false;
    }
    return true;
}

uint16_t JQ8900_GetPlayingTrack(void)
{
    if (!JQ8900_IsBusy()) return 0;
    return _playing_track;
}

/** Send a short 4-byte command: [0xAA] [CMD] [DATA] [CHECKSUM] */
static void JQ8900_SendShort(uint8_t cmd, uint8_t data)
{
    uint8_t pkt[4];
    pkt[0] = 0xAA;
    pkt[1] = cmd;
    pkt[2] = data;
    pkt[3] = (0xAA + cmd + data) & 0xFF;
    HAL_UART_Transmit(_huart, pkt, sizeof(pkt), 100);
}

/** Play track by index (1~65535).
 *  Protocol: AA 07 02 HI LO CHK
 *  Track N maps to file /NN.mp3 (zero-padded on flash). */
void JQ8900_Play(uint16_t index)
{
    uint8_t hi = (index >> 8) & 0xFF;
    uint8_t lo =  index & 0xFF;
    uint8_t cs = (0xAA + 0x07 + 0x02 + hi + lo) & 0xFF;
    uint8_t pkt[] = {0xAA, 0x07, 0x02, hi, lo, cs};
    HAL_UART_Transmit(_huart, pkt, sizeof(pkt), 100);

    /* Record busy state */
    _playing_track = index;
    _play_end_tick = HAL_GetTick() + get_duration(index);
}

/** Stop playback.  Protocol: AA 10 00 BA */
void JQ8900_Stop(void)
{
    JQ8900_SendShort(0x10, 0x00);
    _playing_track = 0;
    _play_end_tick = 0;
}

/** Set volume (0-30).  Protocol: AA 13 01 VOL CHK */
void JQ8900_SetVolume(uint8_t vol)
{
    if (vol > 30) vol = 30;
    uint8_t cs = (0xAA + 0x13 + 0x01 + vol) & 0xFF;
    uint8_t pkt[] = {0xAA, 0x13, 0x01, vol, cs};
    HAL_UART_Transmit(_huart, pkt, sizeof(pkt), 100);
}

/** Pause playback.  Protocol: AA 03 00 AD */
void JQ8900_Pause(void)
{
    JQ8900_SendShort(0x03, 0x00);
}

/** Next track.  Protocol: AA 06 00 B0 */
void JQ8900_Next(void)
{
    JQ8900_SendShort(0x06, 0x00);
}

/** Previous track.  Protocol: AA 05 00 AF */
void JQ8900_Prev(void)
{
    JQ8900_SendShort(0x05, 0x00);
}

/** Volume +1 step.  Protocol: AA 14 00 BE */
void JQ8900_VolumeUp(void)
{
    JQ8900_SendShort(0x14, 0x00);
}

/** Volume -1 step.  Protocol: AA 15 00 BF */
void JQ8900_VolumeDown(void)
{
    JQ8900_SendShort(0x15, 0x00);
}
