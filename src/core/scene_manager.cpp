#include "scene_manager.h"
#include <LittleFS.h>

extern volatile SemaphoreHandle_t dmx_mutex;
extern uint8_t *main_dmx_buffer;
extern uint8_t *keypad_dmx_buffer;
extern uint8_t *main_target_buffer;
extern uint8_t keypad_target_buffer[];
extern uint8_t keypad_fade_start[];  
extern bool keypadFading;
extern float keypadFadeProgress;
extern float currentFadeTime;        
extern bool preBlackoutRunning;

void saveMacro(int id, const char* name) {
    strlcpy(settings.macros[id], name, sizeof(settings.macros[id]));
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
                if (settings.fadeMacro > 0) {
                    memcpy(keypad_fade_start, keypad_dmx_buffer, 513);
                    f.read(keypad_target_buffer, 513);
                    keypadFadeProgress = 0.0f;
                    keypadFading = true; // ← aggiunto
                    currentFadeTime = settings.fadeMacro;
                    
                } else {
                    f.read(keypad_dmx_buffer, 513);
                }
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
           
            int written = f.write(main_dmx_buffer, 513);
            
            xSemaphoreGive(dmx_mutex);
            f.close();
        }
    }
}

void runSnap(int id) {
    String fileName = "/s" + String(id) + ".dat";
    
    
    File f = LittleFS.open(fileName, "r");
    if (f) {
      
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            preBlackoutRunning = false;
            int bytesRead = f.read(main_target_buffer, 513);
           
            f.close();
                   
            if (settings.fadeSnap > 0) {
                memcpy(crossfade_buffer_a, main_dmx_buffer, 513);
                currentFadeTime = settings.fadeSnap;
                crossfadeProgress = 0.0f;
                crossfadeActive = true;
            } else {
                // Copia diretta senza fade
                memcpy(main_dmx_buffer, main_target_buffer, 513);
                crossfadeActive = false;
            }
            
            
            preBlackoutRunning = settings.isRunning;
            sceneActive = true;
            settings.isRunning = false;
            xSemaphoreGive(dmx_mutex);
        }
    }else {
       
    }
    
}