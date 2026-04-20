#include "update_engine.h"
#include <LittleFS.h>
#include <Update.h>
#define DEST_FS_USES_LITTLEFS
#include <ESP32-targz.h>
#include <vector>
#include <esp_task_wdt.h>
#include "../config.h"
#include "network_engine.h"   // ← per saveConfiguration

// --------------------------------------------------------------------------
// UTILITY: Serializza config in JSON string
// --------------------------------------------------------------------------
static String configToJson() {
    String j = "{";
    j += "\"ssid\":\""     + String(settings.ssid)     + "\",";
    j += "\"pass\":\""     + String(settings.pass)     + "\",";
    j += "\"hostname\":\"" + String(settings.hostname) + "\",";
    j += "\"ip\":["        + String(settings.ip[0])    + "," + String(settings.ip[1])    + "," + String(settings.ip[2])    + "," + String(settings.ip[3])    + "],";
    j += "\"gateway\":["   + String(settings.gateway[0])+ "," + String(settings.gateway[1])+ "," + String(settings.gateway[2])+ "," + String(settings.gateway[3])+ "],";
    j += "\"subnet\":["    + String(settings.subnet[0]) + "," + String(settings.subnet[1]) + "," + String(settings.subnet[2]) + "," + String(settings.subnet[3]) + "],";
    j += "\"target_ip\":["  + String(settings.target_ip[0])+"," + String(settings.target_ip[1])+"," + String(settings.target_ip[2])+"," + String(settings.target_ip[3])+"],";
    j += "\"use_dhcp\":"   + String(settings.use_dhcp)    + ",";
    j += "\"use_unicast\":" + String(settings.use_unicast) + ",";
    j += "\"universe\":"   + String(settings.universe)    + ",";
    j += "\"mode\":"       + String(settings.mode)        + ",";
    j += "\"refreshRate\":" + String(settings.refreshRate) + ",";
    j += "\"fadeSnap\":"   + String(settings.fadeSnap)    + ",";
    j += "\"fadeMacro\":"  + String(settings.fadeMacro)   + ",";
    j += "\"fadeKeypad\":" + String(settings.fadeKeypad)  + ",";
    j += "\"soloLevel\":"  + String(settings.soloLevel)   + ",";
    j += "\"blackoutAuto\":" + String(settings.blackoutAuto) + ",";
    j += "\"autoSave\":"   + String(settings.autoSave ? "true" : "false") + ",";
    j += "\"fadeCurve\":"  + String(settings.fadeCurve)   + ",";
    j += "\"ledMode\":"    + String(settings.ledMode)     + ",";
    j += "\"easyPin\":"    + String(settings.easyPin)     + ",";

    // Nomi snapshot
    j += "\"snapNames\":[";
    for (int i = 0; i < 10; i++) {
        j += "\"" + String(settings.snapNames[i]) + "\"";
        if (i < 9) j += ",";
    }
    j += "],";

    // Nomi macro
    j += "\"macroNames\":[";
    for (int i = 0; i < 10; i++) {
        j += "\"" + String(settings.macros[i]) + "\"";
        if (i < 9) j += ",";
    }
    j += "]";

    j += "}";
    return j;
}

// --------------------------------------------------------------------------
// UTILITY: Applica JSON config a settings (parsing manuale minimale)
// --------------------------------------------------------------------------
static void applyJsonToConfig(const String& json) {
    // Helper lambda per estrarre valore stringa
    auto extractStr = [&](const char* key, char* dest, size_t maxLen) {
        String search = String("\"") + key + "\":\"";
        int pos = json.indexOf(search);
        if (pos < 0) return;
        pos += search.length();
        int end = json.indexOf("\"", pos);
        if (end < 0) return;
        String val = json.substring(pos, end);
        strlcpy(dest, val.c_str(), maxLen);
    };

    auto extractInt = [&](const char* key) -> long {
        String search = String("\"") + key + "\":";
        int pos = json.indexOf(search);
        if (pos < 0) return -1;
        pos += search.length();
        return json.substring(pos).toInt();
    };

    auto extractFloat = [&](const char* key) -> float {
        String search = String("\"") + key + "\":";
        int pos = json.indexOf(search);
        if (pos < 0) return 0.0f;
        pos += search.length();
        return json.substring(pos).toFloat();
    };

    auto extractBool = [&](const char* key) -> bool {
        String search = String("\"") + key + "\":";
        int pos = json.indexOf(search);
        if (pos < 0) return false;
        pos += search.length();
        return json.substring(pos).startsWith("true");
    };

    auto extractArray4 = [&](const char* key, uint8_t* arr) {
        String search = String("\"") + key + "\":[";
        int pos = json.indexOf(search);
        if (pos < 0) return;
        pos += search.length();
        for (int i = 0; i < 4; i++) {
            arr[i] = (uint8_t)json.substring(pos).toInt();
            pos = json.indexOf(",", pos);
            if (pos < 0) break;
            pos++;
        }
    };

    extractStr("ssid",     settings.ssid,     sizeof(settings.ssid));
    extractStr("pass",     settings.pass,     sizeof(settings.pass));
    extractStr("hostname", settings.hostname, sizeof(settings.hostname));

    extractArray4("ip",        settings.ip);
    extractArray4("gateway",   settings.gateway);
    extractArray4("subnet",    settings.subnet);
    extractArray4("target_ip", settings.target_ip);

    long v;
    v = extractInt("use_dhcp");    if (v >= 0) settings.use_dhcp    = (uint8_t)v;
    v = extractInt("use_unicast"); if (v >= 0) settings.use_unicast = (uint8_t)v;
    v = extractInt("universe");    if (v >= 0) settings.universe    = (uint16_t)v;
    v = extractInt("mode");        if (v >= 0) settings.mode        = (uint8_t)v;
    v = extractInt("refreshRate"); if (v >= 0) settings.refreshRate = (uint32_t)v;
    v = extractInt("soloLevel");   if (v >= 0) settings.soloLevel   = (uint8_t)v;
    v = extractInt("blackoutAuto");if (v >= 0) settings.blackoutAuto= (uint8_t)v;
    v = extractInt("fadeCurve");   if (v >= 0) settings.fadeCurve   = (uint8_t)v;
    v = extractInt("ledMode");     if (v >= 0) settings.ledMode     = (uint8_t)v;
    
    extractStr("easyPin", settings.easyPin, sizeof(settings.easyPin));

    settings.fadeSnap   = extractFloat("fadeSnap");
    settings.fadeMacro  = extractFloat("fadeMacro");
    settings.fadeKeypad = extractFloat("fadeKeypad");
    settings.autoSave   = extractBool("autoSave");

    // Nomi snapshot
    String search = "\"snapNames\":[";
    int pos = json.indexOf(search);
    if (pos >= 0) {
        pos += search.length();
        for (int i = 0; i < 10; i++) {
            pos = json.indexOf("\"", pos) + 1;
            int end = json.indexOf("\"", pos);
            if (end < 0) break;
            strlcpy(settings.snapNames[i], json.substring(pos, end).c_str(), sizeof(settings.snapNames[i]));
            pos = end + 1;
        }
    }

    // Nomi macro
    search = "\"macroNames\":[";
    pos = json.indexOf(search);
    if (pos >= 0) {
        pos += search.length();
        for (int i = 0; i < 10; i++) {
            pos = json.indexOf("\"", pos) + 1;
            int end = json.indexOf("\"", pos);
            if (end < 0) break;
            strlcpy(settings.macros[i], json.substring(pos, end).c_str(), sizeof(settings.macros[i]));
            pos = end + 1;
        }
    }
}


// --------------------------------------------------------------------------
// SETUP ENDPOINTS
// --------------------------------------------------------------------------
void setupUpdateEndpoints(AsyncWebServer &srv) {

    // ------------------------------------------------------------------
    // GET /backup
    // ------------------------------------------------------------------
    srv.on("/backup", HTTP_GET, [](AsyncWebServerRequest *request) {

        // 1. Scrivi config JSON su LittleFS
        String json = configToJson();
        File f = LittleFS.open("/backup_config.json", "w");
        if (!f) {
            request->send(500, "text/plain", "Errore scrittura config");
            return;
        }
        f.print(json);
        f.close();

        // 2. Costruisci lista file da includere
        std::vector<TAR::dir_entity_t> dirEntities;

        // Aggiungi config JSON
            TAR::dir_entity_t cfgEntry;
            cfgEntry.path = "/backup_config.json";  // ← assegnazione diretta, non strlcpy
            cfgEntry.is_dir = false;
            File cfgF = LittleFS.open("/backup_config.json", "r");
            cfgEntry.size = cfgF ? cfgF.size() : 0;
            if (cfgF) cfgF.close();
            dirEntities.push_back(cfgEntry);

            // Aggiungi scene /s0.dat ... /s49.dat
            for (int i = 0; i < 50; i++) {
                String path = "/s" + String(i) + ".dat";
                if (LittleFS.exists(path)) {
                    TAR::dir_entity_t e;
                    e.path = path;          // ← assegnazione diretta
                    e.is_dir = false;
                    File tmp = LittleFS.open(path, "r");
                    e.size = tmp ? tmp.size() : 0;
                    if (tmp) tmp.close();
                    dirEntities.push_back(e);
                }
            }

            // Aggiungi macro /m0.dat ... /m9.dat
            for (int i = 0; i < 10; i++) {
                String path = "/m" + String(i) + ".dat";
                if (LittleFS.exists(path)) {
                    TAR::dir_entity_t e;
                    e.path = path;          // ← assegnazione diretta
                    e.is_dir = false;
                    File tmp = LittleFS.open(path, "r");
                    e.size = tmp ? tmp.size() : 0;
                    if (tmp) tmp.close();
                    dirEntities.push_back(e);
                }
            }

        // 3. Comprimi tutto in un tar.gz su LittleFS
        const char* backupPath = "/dmxtoolbox_backup.tar.gz";
        LittleFS.remove(backupPath);

        size_t compressedSize = TarGzPacker::compress(
            &LittleFS,        // filesystem sorgente
            dirEntities,      // lista file da includere
            &LittleFS,        // filesystem destinazione
            backupPath        // percorso file di output
        );

        LittleFS.remove("/backup_config.json");

        if (compressedSize == 0) {
            Serial.println("[BACKUP] Errore compressione");
            request->send(500, "text/plain", "Errore generazione backup");
            return;
        }

        Serial.printf("[BACKUP] Generato: %u byte\n", compressedSize);

        // 4. Invia il file al browser come download
        AsyncWebServerResponse *response = request->beginResponse(
            LittleFS, backupPath, "application/gzip", true
        );
        response->addHeader("Content-Disposition",
            "attachment; filename=\"dmxtoolbox_backup.tar.gz\"");
        request->send(response);
    });

    // ------------------------------------------------------------------
    // POST /restore
    // ------------------------------------------------------------------
    srv.on("/restore", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            // Questa risposta parte DOPO che il file è stato caricato
            // Il riavvio avviene qui se l'estrazione è andata bene
            if (LittleFS.exists("/restore_done.flag")) {
                LittleFS.remove("/restore_done.flag");
                request->send(200, "text/plain", "OK");
                delay(1500);
                ESP.restart();
            } else {
                request->send(500, "text/plain", "ERRORE");
            }
        },
        [](AsyncWebServerRequest *request, String filename,
           size_t index, uint8_t *data, size_t len, bool final) {

            // File statico per ricevere l'upload
            static File uploadFile;

            if (index == 0) {
                Serial.printf("[RESTORE] Inizio ricezione: %s\n", filename.c_str());
                LittleFS.remove("/restore_upload.tar.gz");
                LittleFS.remove("/restore_done.flag");
                uploadFile = LittleFS.open("/restore_upload.tar.gz", "w");
            }

            if (uploadFile) uploadFile.write(data, len);

            if (final) {
                if (uploadFile) uploadFile.close();
                Serial.printf("[RESTORE] Ricevuti %u byte totali\n", index + len);

                // Estrai il tar.gz — API reale TarGzUnpacker
                TarGzUnpacker *unpacker = new TarGzUnpacker();
                unpacker->haltOnError(true);
                unpacker->setTarVerify(false);
                unpacker->setupFSCallbacks(targzTotalBytesFn, targzFreeBytesFn);
                unpacker->setGzProgressCallback(BaseUnpacker::targzNullProgressCallback);
                unpacker->setTarProgressCallback(BaseUnpacker::targzNullProgressCallback);
                unpacker->setTarStatusProgressCallback(
                    BaseUnpacker::defaultTarStatusProgressCallback);
                unpacker->setLoggerCallback(BaseUnpacker::targzPrintLoggerCallback);

                bool ok = unpacker->tarGzExpander(
                    LittleFS,                       // filesystem sorgente
                    "/restore_upload.tar.gz",       // file da estrarre
                    LittleFS,                       // filesystem destinazione
                    "/"                             // cartella di destinazione
                );

                delete unpacker;
                LittleFS.remove("/restore_upload.tar.gz");

                if (ok) {
                    Serial.println("[RESTORE] Estrazione OK");

                    // Applica config dal JSON estratto
                    File cfg = LittleFS.open("/backup_config.json", "r");
                    if (cfg) {
                        String jsonStr = cfg.readString();
                        cfg.close();
                        applyJsonToConfig(jsonStr);
                        saveConfiguration();
                        LittleFS.remove("/backup_config.json");
                        Serial.println("[RESTORE] Config applicata");
                    }

                    // Crea flag per segnalare successo alla risposta HTTP
                    File flag = LittleFS.open("/restore_done.flag", "w");
                    if (flag) { flag.print("1"); flag.close(); }
                } else {
                    Serial.println("[RESTORE] Errore estrazione!");
                }
            }
        }
    );

    // ------------------------------------------------------------------
    // POST /update/firmware — OTA firmware .bin
    // ------------------------------------------------------------------
    srv.on("/update/firmware", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(
                200, "text/plain", ok ? "OK" : "ERRORE"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (ok) {
                Serial.println("[OTA FW] Riavvio...");
                delay(500);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest *request, String filename,
           size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA FW] Avvio: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA FW] OK: %u byte\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
            esp_task_wdt_reset();
        }
    );

    // ------------------------------------------------------------------
    // POST /update/fs — OTA filesystem littlefs.bin
    // ------------------------------------------------------------------
    srv.on("/update/fs", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(
                200, "text/plain", ok ? "OK" : "ERRORE"
            );
            response->addHeader("Connection", "close");
            request->send(response);
            if (ok) {
                Serial.println("[OTA FS] Riavvio...");
                delay(500);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest *request, String filename,
           size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[OTA FS] Avvio: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_SPIFFS)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA FS] OK: %u byte\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
            esp_task_wdt_reset();
        }
    );

    // ------------------------------------------------------------------
    // GET /update/progress — percentuale OTA in corso
    // ------------------------------------------------------------------
    srv.on("/update/progress", HTTP_GET, [](AsyncWebServerRequest *request) {
        int progress = 0;
        if (Update.isRunning()) {
            size_t total   = Update.size();
            size_t written = Update.progress();
            if (total > 0) progress = (int)((written * 100) / total);
        }
        request->send(200, "text/plain", String(progress));
    });

    Serial.println("[UPDATE] Endpoint registrati.");
}

