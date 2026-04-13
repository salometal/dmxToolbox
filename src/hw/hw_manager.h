// hw_manager.h
#ifndef HW_MANAGER_H
#define HW_MANAGER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"

typedef enum {
    RELAY_THRU,   // Spento/default — thru passivo
    RELAY_ON,     // Modo 0 — DMX IN attivo + thru
    RELAY_OFF     // Modo 1 — ArtNet OUT, IN disconnesso
} RelayMode;

void setRelay(RelayMode mode);

void hw_init();
void hw_loop();

#endif