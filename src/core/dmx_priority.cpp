#include "dmx_priority.h"

extern bool blackoutActive;
extern bool sceneActive;
extern bool keypadModeEnabled;

DmxSource getActiveSource() {
    if (blackoutActive)    return SOURCE_BLACKOUT;
    if (sceneActive)       return SOURCE_SNAP;
    if (keypadModeEnabled) return SOURCE_KEYPAD;
    if (settings.isRunning && settings.mode == 1) return SOURCE_ARTNET;
    if (settings.isRunning && settings.mode == 0) return SOURCE_ARTNET;
    return SOURCE_NONE;
}

const char* sourceToString(DmxSource s) {
    switch(s) {
        case SOURCE_BLACKOUT: return "BLACKOUT";
        case SOURCE_SNAP:     return "SNAP";
        case SOURCE_KEYPAD:   return "KEYPAD";
        case SOURCE_ARTNET:   return "ARTNET";
        default:              return "NONE";
    }
}

void applyRelayForSource(DmxSource source) {
    switch(source) {
        case SOURCE_BLACKOUT:
        case SOURCE_SNAP:
        case SOURCE_KEYPAD:
            setRelay(RELAY_OFF);
            break;
        case SOURCE_ARTNET:
            if (settings.mode == 0) {
                setRelay(RELAY_ON);
            } else {
                setRelay(RELAY_OFF);
            }
            break;
        case SOURCE_NONE:
        default:
            setRelay(RELAY_THRU);
            break;
    }
}