#include "device_state.h"
#include "../config.h"

extern bool keypadModeEnabled;
extern bool sceneActive;
extern bool blackoutActive;
extern bool provisioningActive;
extern bool artnetConfirmed;

const char* getDeviceState() {
    if (blackoutActive)                 return "BLACKOUT";
    if (sceneActive)                    return "SCENE_PLAYING";
    if (keypadModeEnabled)              return "KEYPAD_MODE";
    if (!settings.isRunning)            return "STANDBY";
    if (settings.mode == 0)             return "DMX_ACTIVE";
    if (settings.mode == 1 && !artnetConfirmed)          return "ARTNET_SEARCHING";
    if (settings.mode == 1 && artnetConfirmed)           return "ARTNET_ACTIVE";
    return "STANDBY";
}