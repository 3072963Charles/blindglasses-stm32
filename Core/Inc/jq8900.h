#ifndef JQ8900_H
#define JQ8900_H

#include "stm32h7xx_hal.h"
#include <stdbool.h>

// JQ8900-16P Serial Voice Module
// UART 9600 baud, 8N1 (factory default)
// Protocol: 0xAA + CMD + DATA + CHECKSUM  (4-byte packet)
//       or: 0xAA + CMD + LEN + DATA... + CHECKSUM  (variable-length)
// Checksum = sum of all bytes truncated to 8 bits

// Track index mapping (files on flash root: /01.mp3 ~ /11.mp3)
#define JQ8900_TRACK_FRONT_EMERGENCY   1   // "前方危险，请停下"
#define JQ8900_TRACK_FRONT_WARNING     2   // "前方注意"
#define JQ8900_TRACK_LEFT_EMERGENCY    3   // "左侧有障碍，请向右"
#define JQ8900_TRACK_LEFT_WARNING      4   // "左侧注意"
#define JQ8900_TRACK_RIGHT_EMERGENCY   5   // "右侧有障碍，请向左"
#define JQ8900_TRACK_RIGHT_WARNING     6   // "右侧注意"
#define JQ8900_TRACK_BOTTOM_EMERGENCY  7   // "注意脚下，有台阶"
#define JQ8900_TRACK_SAFE              8   // "前方安全，可以通行"
#define JQ8900_TRACK_THREE_SIDE_BLOCK  9   // "前方受阻，请停下"
#define JQ8900_TRACK_BOTH_SIDE_NARROW 10   // "两侧狭窄，小心通过"
#define JQ8900_TRACK_BOTTOM_WARNING   11   // "下方注意"

// Legacy aliases for level-repeat broadcasts
#define JQ8900_TRACK_DANGER   JQ8900_TRACK_FRONT_EMERGENCY
#define JQ8900_TRACK_WARNING  JQ8900_TRACK_FRONT_WARNING

// Estimated track durations in ms (conservative: spoken duration + 300ms buffer)
#define JQ8900_DURATION_SHORT   1300   // "前方注意", "左侧注意", "右侧注意", "下方注意"
#define JQ8900_DURATION_NORMAL  2300   // "前方危险...", "注意脚下...", "前方安全...", "前方受阻...", "两侧狭窄..."
#define JQ8900_DURATION_LONG    2600   // "左侧有障碍...", "右侧有障碍..."

void JQ8900_Init(UART_HandleTypeDef *huart);
void JQ8900_Play(uint16_t index);
void JQ8900_Stop(void);
void JQ8900_SetVolume(uint8_t vol);    // 0-30
void JQ8900_Pause(void);
void JQ8900_Next(void);
void JQ8900_Prev(void);
void JQ8900_VolumeUp(void);
void JQ8900_VolumeDown(void);
bool     JQ8900_IsBusy(void);           // true = playback in progress
uint16_t JQ8900_GetPlayingTrack(void);  // currently playing track (0 = idle)

#endif
