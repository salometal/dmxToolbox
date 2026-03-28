#include "scene_manager.h"
#include <LittleFS.h>

extern volatile SemaphoreHandle_t dmx_mutex;
extern uint8_t *main_dmx_buffer;
extern uint8_t *keypad_dmx_buffer;

void saveMacro(int id, const char* name) {
    strlcpy(settings.macros[id], name, sizeof(settings.macros[id]));
    // salva config
    String fileName = "/m" + String(id) + ".dat";
    File f = LittleFS.open(fileName, "w");
    if (f) {
        f.write(keypad_dmx_buffer, 513);
        f.close();
    }
}

void runMacro(int id) {
    String fileName = "/m" + String(id) + ".dat";
    if (LittleFS.exists(fileName)) {
        File f = LittleFS.open(fileName, "r");
        if (f) {
            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                f.read(keypad_dmx_buffer, 513);
                f.close();
                xSemaphoreGive(dmx_mutex);
            }
        }
    }
}

void saveSnap(int id, const char* name) {
    strlcpy(settings.snapNames[id], name, sizeof(settings.snapNames[id]));
    File f = LittleFS.open("/s" + String(id) + ".dat", "w");
    if (f) {
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            f.write(main_dmx_buffer, 513);
            xSemaphoreGive(dmx_mutex);
            f.close();
        }
    }
}

void runSnap(int id) {
    File f = LittleFS.open("/s" + String(id) + ".dat", "r");
    if (f) {
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            f.read(main_dmx_buffer, 513);
            f.close();
            xSemaphoreGive(dmx_mutex);
        }
    }
}