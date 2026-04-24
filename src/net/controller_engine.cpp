#include "controller_engine.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../core/scene_manager.h"
#include "../core/dmx_priority.h"
#include "../hw/hw_manager.h"

extern volatile SemaphoreHandle_t dmx_mutex;
extern uint8_t *main_dmx_buffer;
extern bool artnetConfirmed;
extern bool sceneActive;
extern bool blackoutActive;
extern bool preBlackoutRunning;
extern float crossfadeProgress;
extern bool crossfadeActive;
extern int8_t activeSnapId;
extern bool keypadModeEnabled;

void setupControllerEndpoints(AsyncWebServer &server) {

    // -------------------------------------------------------
    // GET /api/status — stato completo JSON per ATOM Tally
    // -------------------------------------------------------
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;
        doc["running"]   = settings.isRunning;
        doc["mode"]      = settings.mode;
        doc["blackout"]  = blackoutActive;
        doc["scene"]     = sceneActive;
        doc["artnet"]    = artnetConfirmed;
        doc["crossfade"] = crossfadeActive;
        doc["universe"]  = settings.universe;
        doc["refresh"]   = settings.refreshRate;
        doc["heap"]      = (int)(ESP.getFreeHeap() / 1024);
        doc["uptime"]    = (uint32_t)(millis() / 1000);
        doc["hostname"]  = settings.hostname;
        doc["snapId"] = activeSnapId;
        doc["keypad"] = keypadModeEnabled;

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // -------------------------------------------------------
    // GET /api/snapshots — lista slot con nome non vuoto
    // -------------------------------------------------------
    server.on("/api/snapshots", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;
        JsonArray arr = doc.to<JsonArray>();

        for (int i = 0; i < MAX_SCENES; i++) {
            if (strlen(sceneNames[i]) > 0) {
                JsonObject obj = arr.createNestedObject();
                obj["id"]   = i;
                obj["name"] = sceneNames[i];
            }
        }

        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // -------------------------------------------------------
    // GET /api/snap/recall?id=X&fade=Y
    // -------------------------------------------------------
    server.on("/api/snap/recall", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("id")) {
            request->send(400, "application/json", "{\"error\":\"missing id\"}");
            return;
        }

        int id     = request->getParam("id")->value().toInt();
        float fade = request->hasParam("fade")
                     ? request->getParam("fade")->value().toFloat()
                     : 0.0f;

       if (id < 0 || id >= MAX_SCENES) {
            request->send(400, "application/json", "{\"error\":\"invalid id\"}");
            return;
        }

        applyRelayForSource(SOURCE_SNAP);
        runSnapExternal(id, fade);

        StaticJsonDocument<64> doc;
        doc["ok"]   = true;
        doc["id"]   = id;
        doc["fade"] = fade;
        String out;
        serializeJson(doc, out);
        request->send(200, "application/json", out);
    });

    // -------------------------------------------------------
    // GET /api/snap/release
    // -------------------------------------------------------
server.on("/api/snap/release", HTTP_GET, [](AsyncWebServerRequest *request) {
    sceneActive    = false;
    blackoutActive = false;
    settings.isRunning = preBlackoutRunning;
    preBlackoutRunning = false;

    applyRelayForSource(getActiveSource());

    request->send(200, "application/json", "{\"ok\":true}");
});
    // -------------------------------------------------------
    // GET /api/blackout — blackout remoto ATOM (indipendente)
    // -------------------------------------------------------
    server.on("/api/blackout", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            memset(main_dmx_buffer, 0, 513);
            sceneActive        = true;
            blackoutActive     = true;
            preBlackoutRunning = settings.isRunning;
            settings.isRunning = false;
            xSemaphoreGive(dmx_mutex);
        }
        applyRelayForSource(SOURCE_BLACKOUT);

        request->send(200, "application/json", "{\"ok\":true}");
    });
}