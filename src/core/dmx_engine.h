#ifndef DMX_ENGINE_H
#define DMX_ENGINE_H

#include <Arduino.h>
#include <esp_dmx.h>
#include "../config.h"

// Stato crossfade
extern float crossfadeProgress; // 0.0 = A, 1.0 = B
extern bool crossfadeActive;
extern uint8_t crossfade_buffer_a[513]; // snapshot scena A
void crossfadeTask(void *pvParameters);

void dmxTask(void *pvParameters);

#endif