#ifndef OBSTACLE_H
#define OBSTACLE_H

#include <stdint.h>

void Obstacle_Process(float urm09_cm, const uint16_t tof_mm[4]);
uint8_t Obstacle_GetLevel(void);       // returns 1=DANGER, 2=WARNING, 3=SAFE
uint8_t Obstacle_GetCurrentTrack(void); // returns the last-played track (0=none)

#endif
