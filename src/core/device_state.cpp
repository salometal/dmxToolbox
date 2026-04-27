#include "device_state.h"
#include "../config.h"
#include "../core/dmx_priority.h"
#include "../hw/hw_manager.h"

extern bool keypadModeEnabled;
extern bool sceneActive;
extern bool blackoutActive;
extern bool artnetConfirmed;
extern bool udpActive;
extern bool wasRunningBeforeKeypad;
extern uint8_t *main_dmx_buffer;
extern volatile SemaphoreHandle_t dmx_mutex;

static char previousState[32] = "STANDBY";

const char* getDeviceState() {
    if (blackoutActive)                          return "BLACKOUT";
    if (sceneActive)                             return "SCENE_PLAYING";
    if (keypadModeEnabled)                       return "KEYPAD_MODE";
    if (!settings.isRunning)                     return "STANDBY";
    if (settings.mode == 0)                      return "DMX_ACTIVE";
    if (settings.mode == 1 && !artnetConfirmed)  return "ARTNET_SEARCHING";
    if (settings.mode == 1 && artnetConfirmed)   return "ARTNET_ACTIVE";
    return "STANDBY";
}

const char* getPreviousDeviceState() {
    return previousState;
}

extern bool udpActive;

void saveCurrentState() {
    // Priorità esplicita — keypad prima di tutto
    if (keypadModeEnabled) {
        strlcpy(previousState, "KEYPAD_MODE", sizeof(previousState));
    } else if (udpActive && settings.mode == 1) {
        strlcpy(previousState, "ARTNET_ACTIVE", sizeof(previousState));
    } else if (udpActive && settings.mode == 0) {
        strlcpy(previousState, "DMX_ACTIVE", sizeof(previousState));
    } else {
        strlcpy(previousState, getDeviceState(), sizeof(previousState));
    }
    Serial.printf("[STATE] Stato salvato: %s\n", previousState);
}

void restoreState() {
    Serial.printf("[STATE] Ripristino stato: %s\n", previousState);

    if (strcmp(previousState, "ARTNET_ACTIVE") == 0 || 
        strcmp(previousState, "ARTNET_SEARCHING") == 0) {
        settings.mode = 1;
        applyRelayForSource(SOURCE_ARTNET);
        settings.isRunning = true;
        udpActive = false;
        artnetConfirmed = false;

    } else if (strcmp(previousState, "DMX_ACTIVE") == 0) {
        settings.mode = 0;
        applyRelayForSource(SOURCE_ARTNET);
        settings.isRunning = true;

    } else if (strcmp(previousState, "KEYPAD_MODE") == 0) {
        applyRelayForSource(SOURCE_KEYPAD);
        settings.isRunning = true;
        keypadModeEnabled = true;

    } else if (strcmp(previousState, "BLACKOUT") == 0) {
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memset(main_dmx_buffer, 0, 513);
            sceneActive = true;
            blackoutActive = true;
            settings.isRunning = false;
            xSemaphoreGive(dmx_mutex);
        }
        applyRelayForSource(SOURCE_BLACKOUT);

    } else {
        // STANDBY o qualsiasi altro stato
        applyRelayForSource(SOURCE_NONE);
        settings.isRunning = false;
    }

    // Reset previousState a STANDBY dopo il ripristino
    strlcpy(previousState, "STANDBY", sizeof(previousState));
}