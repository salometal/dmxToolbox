#include "scene_manager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

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
extern int8_t activeSnapId;

// Array globale nomi scene
char sceneNames[MAX_SCENES][32];

// -------------------------------------------------------
// LOAD / SAVE scenes.json
// -------------------------------------------------------
void loadScenes() {
    // Inizializza tutto vuoto
   for (int i = 0; i < MAX_SCENES; i++) {
        sceneNames[i][0] = '\0';
    }

    if (!LittleFS.exists("/scenes.json")) {
        // File non esiste — primo avvio, crealo vuoto
        File f = LittleFS.open("/scenes.json", "w");
        if (f) {
            f.print("[]");
            f.close();
            Serial.println("[SCENES] scenes.json creato vuoto");
        } else {
            Serial.println("[SCENES] Errore creazione scenes.json");
        }
        return;
    }
        File f = LittleFS.open("/scenes.json", "r");
    if (!f) return;

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("[SCENES] Errore parsing scenes.json");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        int id = obj["id"] | -1;
        const char* name = obj["name"] | "";
        if (id >= 0 && id < MAX_SCENES) {
            strlcpy(sceneNames[id], name, sizeof(sceneNames[id]));
        }
    }
    Serial.println("[SCENES] scenes.json caricato");
}

void saveScenes() {
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.to<JsonArray>();

    for (int i = 0; i < MAX_SCENES; i++) {
        if (strlen(sceneNames[i]) > 0) {
            JsonObject obj = arr.createNestedObject();
            obj["id"]   = i;
            obj["name"] = sceneNames[i];
        }
    }

    File f = LittleFS.open("/scenes.json", "w");
    if (!f) return;
    serializeJson(doc, f);
    f.close();
    Serial.println("[SCENES] scenes.json salvato");
}

// -------------------------------------------------------
// MACRO
// -------------------------------------------------------
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
    if (!LittleFS.exists(fileName)) return;

    File f = LittleFS.open(fileName, "r");
    if (!f) return;

    if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (settings.fadeMacro > 0) {
            memcpy(keypad_fade_start, keypad_dmx_buffer, 513);
            f.read(keypad_target_buffer, 513);
            keypadFadeProgress = 0.0f;
            keypadFading = true;
            currentFadeTime = settings.fadeMacro;
        } else {
            f.read(keypad_dmx_buffer, 513);
        }
        f.close();
        xSemaphoreGive(dmx_mutex);
    }
}

// -------------------------------------------------------
// SCENE (ex SNAP)
// -------------------------------------------------------
void saveSnap(int id, const char* name) {
    if (id < 0 || id >= MAX_SCENES) return;

    strlcpy(sceneNames[id], name, sizeof(sceneNames[id]));
    saveScenes(); // persiste scenes.json

    File f = LittleFS.open("/s" + String(id) + ".dat", "w");
    if (f) {
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            f.write(main_dmx_buffer, 513);
            xSemaphoreGive(dmx_mutex);
        }
        f.close();
    }
}

void runSnap(int id) {
    if (id < 0 || id >= MAX_SCENES) return;

    String fileName = "/s" + String(id) + ".dat";
    File f = LittleFS.open(fileName, "r");
    if (!f) return;

    if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        preBlackoutRunning = false;
        f.read(main_target_buffer, 513);
        f.close();

        if (settings.fadeSnap > 0) {
            memcpy(crossfade_buffer_a, main_dmx_buffer, 513);
            currentFadeTime = settings.fadeSnap;
            crossfadeProgress = 0.0f;
            crossfadeActive = true;
        } else {
            memcpy(main_dmx_buffer, main_target_buffer, 513);
            crossfadeActive = false;
        }

        preBlackoutRunning = settings.isRunning;
        sceneActive = true;
        settings.isRunning = false;
        xSemaphoreGive(dmx_mutex);
    }
    activeSnapId = id;
}

void runSnapExternal(int id, float fade) {
    if (id < 0 || id >= MAX_SCENES) return;

    String fileName = "/s" + String(id) + ".dat";
    File f = LittleFS.open(fileName, "r");
    if (!f) return;

    if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        preBlackoutRunning = false;
        f.read(main_target_buffer, 513);
        f.close();

        if (fade > 0.0f) {
            memcpy(crossfade_buffer_a, main_dmx_buffer, 513);
            currentFadeTime = fade;
            crossfadeProgress = 0.0f;
            crossfadeActive = true;
        } else {
            memcpy(main_dmx_buffer, main_target_buffer, 513);
            crossfadeActive = false;
        }

        preBlackoutRunning = settings.isRunning;
        sceneActive = true;
        settings.isRunning = false;
        xSemaphoreGive(dmx_mutex);
    }
    activeSnapId = id;
}