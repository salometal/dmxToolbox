#ifndef DMX_ENGINE_H
#define DMX_ENGINE_H

#include <Arduino.h>
#include <esp_dmx.h>
#include "../config.h"

// Dichiarazione del task
void dmxTask(void *pvParameters);

#endif