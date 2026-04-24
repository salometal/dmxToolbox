#ifndef DMX_PRIORITY_H
#define DMX_PRIORITY_H

#include <Arduino.h>
#include "../config.h"
#include "../hw/hw_manager.h"

typedef enum {
    SOURCE_NONE,
    SOURCE_ARTNET,
    SOURCE_KEYPAD,
    SOURCE_SNAP,
    SOURCE_BLACKOUT
} DmxSource;

DmxSource getActiveSource();
const char* sourceToString(DmxSource s);
void applyRelayForSource(DmxSource source);

#endif