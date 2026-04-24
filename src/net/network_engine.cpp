#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <ESPmDNS.h>
#include "network_engine.h"
#include "core/scene_manager.h"
#include "core/artnet_engine.h"
#include "net/keypad_engine.h"
#include "../core/dmx_priority.h"
#include "update_engine.h"
#include "../hw/hw_manager.h"
#include "../config.h"
#include "controller_engine.h"
#include "lwip/udp.h"


// Riferimenti esterni definiti nel Main
extern AsyncWebServer server;
extern WiFiUDP udp;
extern bool udpActive;
//extern uint8_t *packetBuffer;
extern uint8_t packetBuffer[600];
extern String wifiScanResults; 
extern TaskHandle_t netTaskHandle; // CORREZIONE 1: Necessario per la gestione task
extern volatile int mutex_owner;
extern bool keypadModeEnabled;
extern bool wasRunningBeforeKeypad;
bool blackoutActive = false;
int8_t activeSnapId = -1;
extern uint8_t *keypad_dmx_buffer;



bool checkArtNetPresence() {
    Serial.println("[SNIFFER] Ricerca segnale Art-Net (Qualsiasi Universo/Tipo)...");
    
    // Pulizia e reset del socket per evitare residui nel buffer dell'ESP32
    udp.stop();
    delay(50);
    udp.begin(6454); 

    unsigned long startMs = millis();
    static uint8_t snifferBuffer[600]; 
    
    // Finestra di 5 secondi per coprire la sorgente lenta (Raspberry 2s)
    while (millis() - startMs < 5000) {
        int packetSize = udp.parsePacket();
        
        if (packetSize >= 10) { // Un ArtPoll è piccolo, un ArtDMX è grande.
            int readLen = udp.read(snifferBuffer, 600);
            
            // Unico controllo: l'intestazione standard Art-Net
            if (readLen >= 10 && memcmp(snifferBuffer, "Art-Net", 7) == 0) {
                Serial.printf("[SNIFFER] Art-Net RILEVATO in %d ms! (Opcode: 0x%02X%02X)\n", 
                              (int)(millis() - startMs), snifferBuffer[9], snifferBuffer[8]);
                              udp.stop();
                return true; // Trovato qualcosa? Esci subito e dai l'OK.
            }
        }
        
        delay(1); // Ciclo ultra-rapido per non perdere pacchetti UDP
        yield(); 
    }

    Serial.println("[SNIFFER] Timeout 5s: Nessun pacchetto Art-Net trovato.");
    udp.stop();
    return false;
}


String scanArtNetNodes() {
    Serial.println("[SCANNER] Avvio scansione Art-Net UDP...");
    
    // 1. Reset del socket (come nello sniffer)
    udp.stop();
    delay(50);
    udp.begin(6454); 

    // 2. Preparazione pacchetto ArtPoll
// 0x00, 0x20 = OpCode ArtPoll
// 0x00, 0x0E = Protocol Version 14 (quella standard)
uint8_t artpoll[14] = {'A','r','t','-','N','e','t', 0x00, 0x00, 0x20, 0x00, 0x0E, 0x02, 0x00};
    udp.beginPacket(IPAddress(255,255,255,255), 6454);
    udp.write(artpoll, 14);
    udp.endPacket();

    String foundNodes = "";
    unsigned long startMs = millis();
    static uint8_t scanBuf[600]; 

    // 3. Finestra di ascolto
    while (millis() - startMs < 1500) {
        int packetSize = udp.parsePacket();
        if (packetSize >= 200) {
            int readLen = udp.read(scanBuf, 600);
            
            // Header 'Art-Net' + OpCode 0x2100 (ArtPollReply)
            if (readLen >= 30 && memcmp(scanBuf, "Art-Net", 7) == 0 && scanBuf[9] == 0x21) {
                String ip = String(scanBuf[10]) + "." + String(scanBuf[11]) + "." + String(scanBuf[12]) + "." + String(scanBuf[13]);
                
                char sName[18];
                memcpy(sName, scanBuf + 26, 17);
                sName[17] = '\0';

                // Costruiamo il pezzetto di JSON
                if (foundNodes.length() > 0) foundNodes += ",";
                foundNodes += "{\"name\":\"" + String(sName) + "\",\"ip\":\"" + ip + "\",\"type\":\"artnet\"}";
                Serial.printf("[SCANNER] Trovato: %s (%s)\n", sName, ip.c_str());
            }
        }
        yield();
    }
    udp.stop();
    return foundNodes;
}



// --- FUNZIONE SALVATAGGIO ---
void saveConfiguration() {
    File file = LittleFS.open("/config.bin", "w");
    if (file) {
        file.write((const uint8_t*)&settings, sizeof(Config));
        file.close();
        Serial.println("[FS] Configurazione salvata con successo.");
    } else {
        Serial.println("[FS] Errore in scrittura config!");
    }
    // if (dmxDriverInstalled) dmx_driver_enable(dmxPort);
}

// --- CONFIGURAZIONE WEB SERVER (Chiamata una sola volta) ---
void setupWebServer() {
    // --- 1. ROTTE DINAMICHE PRIORITARIE ---
    // Gestione Root (Index)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/index.html")) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(404, "text/plain", "ERRORE: index.html mancante!");
        }
    });

    // Rotta per il tastierino (STANDALONE)
server.on("/standalone", HTTP_GET, [](AsyncWebServerRequest *request) {
    String cmd = request->hasParam("cmd") ? request->getParam("cmd")->value() : "";
    String type = request->hasParam("type") ? request->getParam("type")->value() : "";
    String off = request->hasParam("offsets") ? request->getParam("offsets")->value() : "";
    int stp = request->hasParam("step") ? request->getParam("step")->value().toInt() : 1;
    
    if (stp <= 0) stp = 1;
 
    // Passiamo tutto al parser: sarà lui a gestire le priorità
    processStandaloneCommand(cmd, type, off, stp);
    
    request->send(200, "text/plain", "OK");
});

    // Rotta DMX IN -> ARTNET OUT
// Rotta DMX IN -> ARTNET OUT (Aggiornata per Unicast/Target IP)
    server.on("/dmxin", HTTP_GET, [](AsyncWebServerRequest *request){
        settings.mode = 0; // Forza modo DMX IN -> Art-Net OUT
        Serial.println("--- RICEVUTA CHIAMATA /dmxin ---");

  

        // 1. Gestione Start/Stop
        if (request->hasParam("run")) {
            settings.isRunning = (request->getParam("run")->value() == "1");
        }

        // 2. Gestione Unicast/Broadcast
        if (request->hasParam("u_uni")) {
            String val = request->getParam("u_uni")->value();
            settings.use_unicast = (val == "on" || val == "1");
        } else {
            // Se il parametro manca (checkbox non spuntata), forza a false
            settings.use_unicast = false; 
        }

        // 3. Gestione Target IP (Solo se Unicast è attivo)
        if (request->hasParam("u_ip")) {
            IPAddress tip;
            if (tip.fromString(request->getParam("u_ip")->value())) {
                for(int i=0; i<4; i++) settings.target_ip[i] = tip[i];
            }
        }

        // 4. Gestione Universo
        if (request->hasParam("u")) {
            settings.universe = request->getParam("u")->value().toInt();
        }

        if (request->hasParam("r")) {
        settings.refreshRate = request->getParam("r")->value().toInt(); // Riceve "44", "40", valori in Hz .
        }
            if (settings.isRunning) {
                applyRelayForSource(getActiveSource());
            }
 // --- SALVATAGGIO CONFIGURAZIONE ---
        saveConfiguration(); // 


        Serial.printf("[WEB] DMX-IN Start: %d | Uni: %d | Target: %d.%d.%d.%d\n", 
                      settings.isRunning, settings.use_unicast, 
                      settings.target_ip[0], settings.target_ip[1], 
                      settings.target_ip[2], settings.target_ip[3]);

        request->send(200, "text/plain", settings.isRunning ? "START_OK" : "STOP_OK");
    });


    // Rotta ARTNET IN -> DMX OUT
server.on("/artnetin", HTTP_GET, [](AsyncWebServerRequest *request){
    settings.mode = 1; 
    
    if (request->hasParam("u")) {
        settings.universe = request->getParam("u")->value().toInt();
    }

    bool requestedRun = false;
    if (request->hasParam("run")) {
        requestedRun = (request->getParam("run")->value() == "1");
    }
//refresh rate
    if (request->hasParam("r")) {

        int rVal = request->getParam("r")->value().toInt();

        if (rVal >= 10 && rVal <= 44) { // Validazione di sicurezza

            settings.refreshRate = rVal;

            Serial.printf("[WEB] Refresh Rate impostato a: %d Hz\n", settings.refreshRate);

        }
    }
        
        if (requestedRun) {
                applyRelayForSource(getActiveSource()); // ← ArtNet IN attivo
  
            artnetConfirmed = false; // reset conferma al nuovo avvio
            udpActive = false;       // forza riapertura socket pulita
            settings.isRunning = true;
            Serial.println("[WEB] Art-Net IN: avvio ricerca...");
            request->send(200, "text/plain", "OK_START");
        } else {
                applyRelayForSource(SOURCE_NONE); // ← torna in thru
            settings.isRunning = false;
            artnetConfirmed = false; // reset anche allo stop
            udpActive = false;
            Serial.println("[WEB] Art-Net FERMATO dall'utente.");
            request->send(200, "text/plain", "OK_STOP");
        }

     // --- SALVATAGGIO CONFIGURAZIONE ---
        saveConfiguration(); // 
});
    // Lista WiFi
    server.on("/wifi_list", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", wifiScanResults);
    });
// Status di sistema
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
        String currentSSID = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "DISCONNECTED";
        String currentIP = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        String currentSubnet = (WiFi.status() == WL_CONNECTED) ? WiFi.subnetMask().toString() : "255.255.255.0";

        String s = currentSSID;   //0
        s += "|" + currentIP;     //1
        s += "|" + String(settings.mode);   //2
        s += "|" + String(settings.isRunning ? "1" : "0"); //3
        s += "|" + String(settings.use_unicast);  //4
        s += "|" + String(settings.universe);    //5
        s += "|" + String(settings.refreshRate);  //6
        s += "|" + currentSubnet; // 7
        s += "|" + String(settings.target_ip[0]) + "." + 
               String(settings.target_ip[1]) + "." +             //8
               String(settings.target_ip[2]) + "." + 
               String(settings.target_ip[3]);
        s += "|" + String(settings.hostname);   //9
        s += "|" + String(keypadModeEnabled ? "1" : "0"); // 10
        // Macro NAMES (Indice 11)
        s += "|"; 
            for(int i=0; i<10; i++) {
                s += String(settings.macros[i]);         //11 
                if(i < 9) s += ","; 
            }
       // SNAP NAMES (Indice 12)
        s += "|"; 
        for(int i=0; i<MAX_SCENES; i++) {
            s += String(sceneNames[i]);
            if(i < MAX_SCENES-1) s += ","; 
        }
        // ArtNet Confirmed (Indice 13)
        s += "|" + String(artnetConfirmed ? "1" : "0");
        s += "|" + String(sceneActive ? "1" : "0");       // 14 ← snap active flag
        s += "|" + String(blackoutActive ? "1" : "0"); // 15 blackout flag
        // Uptime in secondi (indice 16)
        s += "|" + String(millis() / 1000);
        // FS usage % (indice 17)
        size_t fsTotal = LittleFS.totalBytes();
        size_t fsUsed  = LittleFS.usedBytes();
        int fsPct = (fsTotal > 0) ? (int)((fsUsed * 100) / fsTotal) : 0;
        s += "|" + String(fsPct);
                    
        request->send(200, "text/plain", s);
    });

server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "");
});


    // --- 3. ROTTE DI CONFIGURAZIONE E SISTEMA ---
    server.on("/connect", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("[NET] Aggiornamento parametri ricevuto.");

    // WiFi (usando le tue chiavi 's' e 'p')
    if (request->hasParam("s")) strncpy(settings.ssid, request->getParam("s")->value().c_str(), 31);
    if (request->hasParam("p")) strncpy(settings.pass, request->getParam("p")->value().c_str(), 31);
    
    // Hostname, Universo, Mode (usando le tue chiavi)
    if (request->hasParam("h")) strncpy(settings.hostname, request->getParam("h")->value().c_str(), 31);
    if (request->hasParam("u")) settings.universe = request->getParam("u")->value().toInt();
    if (request->hasParam("m")) settings.mode = request->getParam("m")->value().toInt();
    
    // Unicast e Refresh (attenzione: usa i nomi che arrivano dal JS)
    if (request->hasParam("tx_mode")) settings.use_unicast = request->getParam("tx_mode")->value().toInt();
    if (request->hasParam("refresh")) settings.refreshRate = request->getParam("refresh")->value().toInt();

    // Logica DHCP identica alla tua
    settings.use_dhcp = request->hasParam("dhcp"); 

    // Lambda per il parsing degli IP (Sola sintassi, stessa logica)
    auto parseAndSet = [&](const char* pName, uint8_t* target) {
        if (request->hasParam(pName)) {
            IPAddress ip;
            if (ip.fromString(request->getParam(pName)->value())) {
                for(int i=0; i<4; i++) target[i] = ip[i];
            }
        }
    };

    if (!settings.use_dhcp) {
        parseAndSet("ip", settings.ip);
        parseAndSet("gw", settings.gateway);
        parseAndSet("sn", settings.subnet);
    }

    // IP Target Unicast
    parseAndSet("u_ip", settings.target_ip);

    request->send(200, "text/plain", "Parametri salvati. Riavvio in corso...");
    saveConfiguration(); 
    delay(2000); 
    ESP.restart();
});
server.on("/set-hostname", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("h")) {
        String newH = request->getParam("h")->value();
        strlcpy(settings.hostname, newH.c_str(), sizeof(settings.hostname));
        saveConfiguration(); // Salva subito su LittleFS

       AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Hostname salvato, Riavvio in corso ...");
        
        // 4. Forza la chiusura della connessione dopo l'invio
        response->addHeader("Connection", "close");
        request->send(response);

        // 5. Stampiamo su seriale e aspettiamo un attimo senza bloccare i pacchetti in uscita
        Serial.println("[SYSTEM] Hostname cambiato. Riavvio tra 2 secondi...");
        
        // Usiamo un timer di sistema per il riavvio invece del delay bloccante
        // Se non vuoi usare flag nel loop, questo è il metodo più brutale ma efficace:
        uint32_t wait = millis();
        while(millis() - wait < 1000) { 
            // Permettiamo al background Wi-Fi di respirare per 1 secondo
            yield(); 
        }
        ESP.restart();
      
    } else {
        request->send(400, "text/plain", "Parametro mancante");
    }
});
server.on("/discover", HTTP_GET, [](AsyncWebServerRequest *request){
    bool wasRunning = settings.isRunning;
    settings.isRunning = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    // --- PARTE 1: SCAN MDNS ---
    int nFam = MDNS.queryService("dmxtoolbox", "tcp");
    String json = "[";
    String report = "\n========= DISCOVERY REPORT =========\n";

    if (nFam > 0) {
        for (int i = 0; i < nFam; ++i) {
            if (i > 0) json += ",";
            json += "{\"name\":\"" + MDNS.hostname(i) + "\",\"ip\":\"" + MDNS.IP(i).toString() + "\",\"type\":\"family\"}";
            report += "  > MDNS:   " + MDNS.hostname(i) + " | IP: " + MDNS.IP(i).toString() + "\n";
        }
    } else {
        report += "  > MDNS:   Nessun nodo 'famiglia' trovato.\n";
    }

    // --- PARTE 2: SCAN ART-NET ---
    // Passiamo alla funzione la possibilità di scansionare e restituire i dati
    String artNetResults = scanArtNetNodes(); 
    
    if (artNetResults.length() > 0) {
        if (json.length() > 1){ json += ",";}
        
        json += artNetResults;
        
        // Estraiamo i nomi per il report (logica semplificata per il log seriale)
        report += "  > ARTNET: Nodi trovati (vedi log scanner sopra)\n";
    } else {
        report += "  > ARTNET: Nessun nodo esterno trovato.\n";
    }

    report += "====================================\n";
    Serial.println(report); // STAMPA IL REPORT COMPLETO

    json += "]"; //chiusura stringa json

    // --- RIPRISTINO ---
    settings.isRunning = wasRunning;

    if (settings.isRunning) {

        udp.stop(); 
        delay(10);
        udp.begin(6454);
    }


    request->send(200, "application/json", json);
});

server.on("/keypad_toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("state")) {
        String val = request->getParam("state")->value();
        bool requestedState = (val == "1");

        // 1. Logica di ATTIVAZIONE (da OFF a ON)
        if (requestedState && !keypadModeEnabled) {
            // Salva stato effettivo — considera anche udpActive per ArtNet
            bool effectivelyRunning = settings.isRunning || udpActive;
            wasRunningBeforeKeypad = effectivelyRunning;
            Serial.printf("[KEYPAD ON] wasRunning salvato: %d (isRunning:%d udpActive:%d mode:%d)\n", 
                effectivelyRunning, settings.isRunning, udpActive, settings.mode);

            settings.isRunning = true; 
            applyRelayForSource(SOURCE_KEYPAD);
            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                memset(keypad_dmx_buffer, 0, 513);
                keypadModeEnabled = true;
                xSemaphoreGive(dmx_mutex);
                Serial.println("[SYSTEM] Keypad Mode: ATTIVATO");
            }
        } 
        
        // 2. Logica di DISATTIVAZIONE
        else if (!requestedState && keypadModeEnabled) {
            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                memset(keypad_dmx_buffer, 0, 513);
                keypadModeEnabled = false;
                settings.isRunning = wasRunningBeforeKeypad;
                wasRunningBeforeKeypad = false;
                xSemaphoreGive(dmx_mutex);
            }

            // Forza riapertura socket in base alla modalità
            if (settings.isRunning && settings.mode == 1) {
                udpActive = false; // ArtNet IN → riapre socket
            }
            if (settings.isRunning && settings.mode == 0) {
                udpActive = false; // DMX IN → reset socket
            }

            applyRelayForSource(getActiveSource());

            Serial.printf("[KEYPAD OFF] isRunning: %d, mode: %d, source: %s\n",
                settings.isRunning, settings.mode, sourceToString(getActiveSource()));
            Serial.println("[SYSTEM] Keypad Mode: DISATTIVATO");
        }

        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing State");
    }
});

// --- ROTTA SALVATAGGIO MACRO ---
server.on("/save_macro", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("id") && request->hasParam("name")) {
        int id = request->getParam("id")->value().toInt();
        String name = request->getParam("name")->value();
        if (id >= 0 && id < MAX_MACROS) {
            String sanitized = name;
            sanitized.replace("|", "");
            sanitized.replace(",", "");
            sanitized = sanitized.substring(0, 15);
            saveMacro(id, sanitized.c_str());
            saveConfiguration();
            request->send(200, "text/plain", "OK");
        } else {
            request->send(400, "text/plain", "ERR_ID");
        }
    }
});

        // --- ROTTA ESECUZIONE MACRO ---
        server.on("/run_macro", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (request->hasParam("id")) {
                int id = request->getParam("id")->value().toInt();
                runMacro(id);
                request->send(200, "text/plain", "OK");
            }
        });
        // --- ROTTA SALVATAGGIO SNAP ---
        server.on("/save_snap", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (!settings.isRunning && !sceneActive) {
            Serial.println("[SNAP] Impossibile salvare in standby");
            request->send(400, "text/plain", "ERR_STANDBY");
            return;
        }
            if (request->hasParam("id") && request->hasParam("name")) {
                int id = request->getParam("id")->value().toInt();
                String name = request->getParam("name")->value();
                if (id >= 0 && id < MAX_SCENES) {
                    String sanitized = name;
                    sanitized.replace("|", "");
                    sanitized.replace(",", "");
                    sanitized = sanitized.substring(0, 15);
                    saveSnap(id, sanitized.c_str());
                    saveConfiguration();
                    request->send(200, "text/plain", "OK");
                }
            }
        });

        server.on("/run_snap", HTTP_GET, [](AsyncWebServerRequest *request) {
            if (request->hasParam("id")) {
                int id = request->getParam("id")->value().toInt();
                applyRelayForSource(SOURCE_SNAP);
                runSnap(id); // ← usa scene_manager con crossfade
                request->send(200, "text/plain", "OK");
            } else {
                request->send(400, "text/plain", "ERR_ID");
            }
        });

        server.on("/release_snap", HTTP_GET, [](AsyncWebServerRequest *request){
            sceneActive = false;
            blackoutActive = false;
            activeSnapId = -1;
            // preBlackoutRunning è già stato salvato al momento del blackout/snap
            bool wasRunning = preBlackoutRunning;
            settings.isRunning = preBlackoutRunning;
            preBlackoutRunning = false;

            applyRelayForSource(getActiveSource());

            Serial.println("[SCENE] Override rilasciato");
            request->send(200, "text/plain", "OK");
        });

    server.on("/download-config", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/config.bin")) {
            request->send(LittleFS, "/config.bin", "application/octet-stream");
        } else {
            request->send(404, "text/plain", "File config.bin non trovato.");
        }
    });
            // --- ROTTA GET PRESETS  ---
        server.on("/get_presets", HTTP_GET, [](AsyncWebServerRequest *request){
            String type = request->hasParam("type") ? request->getParam("type")->value() : "";
            if (type != "offset" && type != "spacing") {
                request->send(400, "text/plain", "ERR_TYPE");
                return;
            }
            String fileName = "/presets_" + type + ".json";
            if (LittleFS.exists(fileName)) {
                request->send(LittleFS, fileName, "application/json");
            } else {
                request->send(200, "application/json", "[]");
            }
        });

        // --- ROTTA SAVE PRESET ---
        server.on("/save_preset", HTTP_GET, [](AsyncWebServerRequest *request){
            String type = request->hasParam("type") ? request->getParam("type")->value() : "";
            String value = request->hasParam("value") ? request->getParam("value")->value() : "";
            if (type.length() == 0 || value.length() == 0) {
                request->send(400, "text/plain", "ERR_PARAMS");
                return;
            }
            String fileName = "/presets_" + type + ".json";
            
            // Leggi array esistente
            String json = "[]";
            if (LittleFS.exists(fileName)) {
                File f = LittleFS.open(fileName, "r");
                if (f) { json = f.readString(); f.close(); }
            }
            
            // Aggiungi valore se non esiste già
            if (json.indexOf("\"" + value + "\"") == -1) {
                json.remove(json.lastIndexOf("]"));
                if (json.length() > 1) json += ",";
                json += "\"" + value + "\"]";
            }
            
            File f = LittleFS.open(fileName, "w");
            if (f) { f.print(json); f.close(); }
            
            request->send(200, "application/json", json);
        });

        // --- ROTTA DELETE PRESET ---
        server.on("/delete_preset", HTTP_GET, [](AsyncWebServerRequest *request){
            String type = request->hasParam("type") ? request->getParam("type")->value() : "";
            String value = request->hasParam("value") ? request->getParam("value")->value() : "";
            if (type.length() == 0 || value.length() == 0) {
                request->send(400, "text/plain", "ERR_PARAMS");
                return;
            }
            String fileName = "/presets_" + type + ".json";
            
            if (!LittleFS.exists(fileName)) {
                request->send(200, "application/json", "[]");
                return;
            }
            
            File f = LittleFS.open(fileName, "r");
            String json = f.readString();
            f.close();
            
            // Rimuovi il valore
            String toRemove = "\"" + value + "\"";
            json.replace(","+toRemove, "");
            json.replace(toRemove+",", "");
            json.replace(toRemove, "");
            
            f = LittleFS.open(fileName, "w");
            if (f) { f.print(json); f.close(); }
            
            request->send(200, "application/json", json);
        });

            server.on("/blackout", HTTP_GET, [](AsyncWebServerRequest *request){
                if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    memset(main_dmx_buffer, 0, 513);
                    sceneActive = true;
                    blackoutActive = true;
                    preBlackoutRunning = settings.isRunning;
                    settings.isRunning = false;
                    xSemaphoreGive(dmx_mutex);
                }
                applyRelayForSource(SOURCE_BLACKOUT); // ← disconnette DMX IN
                Serial.println("[SYSTEM] Blackout eseguito");
                request->send(200, "text/plain", "OK");
            });

        // --- ROTTA GET SETUP ---
        server.on("/get_setup", HTTP_GET, [](AsyncWebServerRequest *request){
            String s = String(settings.fadeSnap);
            s += "|" + String(settings.fadeMacro);
            s += "|" + String(settings.fadeKeypad);
            s += "|" + String((int)round(settings.soloLevel * 100.0 / 255.0));
            s += "|" + String(settings.blackoutAuto);
            s += "|" + String(settings.autoSave ? "1" : "0");
            s += "|" + String(settings.fadeCurve);    
            s += "|" + String(settings.ledMode); // indice 7 
            s += "|" + String(settings.easyPin); // indice 8        
            
            request->send(200, "text/plain", s);
        });

        // --- ROTTA SAVE SETUP ---
        server.on("/save_setup", HTTP_GET, [](AsyncWebServerRequest *request){
            if (request->hasParam("fadesnap"))    settings.fadeSnap    = request->getParam("fadesnap")->value().toFloat();
            if (request->hasParam("fademacro"))   settings.fadeMacro   = request->getParam("fademacro")->value().toFloat();
            if (request->hasParam("fadekeypad"))  settings.fadeKeypad  = request->getParam("fadekeypad")->value().toFloat();
            if (request->hasParam("sololevel"))   settings.soloLevel   = map(request->getParam("sololevel")->value().toInt(), 0, 100, 0, 255);
            if (request->hasParam("blackoutauto")) settings.blackoutAuto = request->getParam("blackoutauto")->value().toInt();
            if (request->hasParam("autosave"))    settings.autoSave    = (request->getParam("autosave")->value() == "1");
            if (request->hasParam("fadecurve")) settings.fadeCurve = request->getParam("fadecurve")->value().toInt();
            if (request->hasParam("ledmode")) settings.ledMode = request->getParam("ledmode")->value().toInt();
            if (request->hasParam("easypin")) {
                String pin = request->getParam("easypin")->value();
                pin = pin.substring(0, 4); // max 4 caratteri
                Serial.print(pin);
                strlcpy(settings.easyPin, pin.c_str(), sizeof(settings.easyPin));
            }
            
            saveConfiguration();
            request->send(200, "text/plain", "OK");
        });
            server.on("/fader", HTTP_GET, [](AsyncWebServerRequest *request){
                String cmd = request->hasParam("cmd") ? request->getParam("cmd")->value() : "";
                String val = request->hasParam("val") ? request->getParam("val")->value() : "0";
                String off = request->hasParam("offsets") ? request->getParam("offsets")->value() : "1";
                int stp = request->hasParam("step") ? request->getParam("step")->value().toInt() : 1;
                
                if (cmd.length() > 0) {
                    String fullCmd = cmd + " AT " + val;
                    keypadFading = false;
                    float savedFade = settings.fadeKeypad;
                    settings.fadeKeypad = 0;
                    processStandaloneCommand(fullCmd, "", off, stp);
                    settings.fadeKeypad = savedFade;
                }
                
                request->send(200, "text/plain", "OK");
            });

            server.on("/identify", HTTP_GET, [](AsyncWebServerRequest *request){
    hw_identify();

    request->send(200, "text/plain", "OK");
                });
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/update.html")) {
            request->send(LittleFS, "/update.html", "text/html");
        } else {
            request->send(404, "text/plain", "update.html mancante");
        }
    });
    server.on("/easy", HTTP_GET, [](AsyncWebServerRequest *request){
    if (LittleFS.exists("/easy.html")) {
        request->send(LittleFS, "/easy.html", "text/html");
    } else {
        request->send(404, "text/plain", "easy.html mancante");
    }
    }); 
    server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest *request){
        memset(settings.ssid, 0, sizeof(settings.ssid));
        memset(settings.pass, 0, sizeof(settings.pass));
        memset(settings.ip, 0, sizeof(settings.ip));
        memset(settings.gateway, 0, sizeof(settings.gateway));
        memset(settings.subnet, 0, sizeof(settings.subnet));
        saveConfiguration(); 
        request->send(200, "text/plain", "Reset effettuato. Riavvio...");
         esp_task_wdt_reset();
        delay(1500);
        ESP.restart();
    });
    
setupControllerEndpoints(server);
setupUpdateEndpoints(server); 
    // --- 2. GESTIONE FILESYSTEM ---
    server.serveStatic("/", LittleFS, "/");

    server.begin();
}


// --- TASK PRINCIPALE NETWORK ---
void networkTask(void *pvParameters) {
 
    esp_task_wdt_add(NULL); 
 
   
    while (true) {
        esp_task_wdt_reset();

        // PUNTO A: Controllo Risorse
        if (main_dmx_buffer == NULL || dmx_mutex == NULL || !dmxDriverInstalled) {
            vTaskDelay(pdMS_TO_TICKS(100)); 
            continue; 
        }
        // ← AGGIUNGI QUESTO BLOCCO
        if (sceneActive) {
            bool canSend = (WiFi.status() == WL_CONNECTED || WiFi.softAPgetStationNum() > 0);
            if (canSend) {
                if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    sendArtDmx(settings.universe, main_dmx_buffer);
                    xSemaphoreGive(dmx_mutex);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (!settings.isRunning) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

     if (settings.mode == 1) { // RICEZIONE ARTNET
        
        if (!udpActive) {
        udp.stop(); // Pulizia precauzionale
        vTaskDelay(pdMS_TO_TICKS(50)); 
        
        if (udp.begin(6454)) {
            udpActive = true;
            Serial.println("[TASK Core 0] >>> Socket Art-Net APERTO <<<");
        } else {
            Serial.println("[TASK Core 0] !!! ERRORE Apertura Socket !!!");
        }
    }
     
        if (udpActive) {
        // 1. Chiamata parsePacket (la sonda)
        int packetSize = udp.parsePacket();
        
        // Log minimo per non intasare ma vedere il flusso
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 2000) {
            lastLog = millis();
        }

            if (packetSize >= 18) {
                if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    mutex_owner = 2;
                    bool got = readArtDmx(main_dmx_buffer);
                    if (got) {
                        static bool firstConfirm = true;
                        if (firstConfirm) {
                            Serial.println("[ARTNET] Sorgente confermata!");
                            firstConfirm = false;
                        }
                    }
                    xSemaphoreGive(dmx_mutex);
                    mutex_owner = 0;
                }
            } 
            else if (udp.available() > 0) { 
                udp.flush(); 
            }

            // Controllo timeout FUORI dal blocco packetSize — gira sempre
            if (artnetConfirmed && lastPacketTime > 0) {
                if (millis() - lastPacketTime > 5000) {
                    artnetConfirmed = false;
                    Serial.println("[ARTNET] Segnale perso! artnetConfirmed=false");
                }
            }

        vTaskDelay(pdMS_TO_TICKS(2)); 
    } else {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
        else if (settings.mode == 0) { // INVIO ARTNET
            // LOG CHIRURGICO 2
            
            
             bool canSend = (WiFi.status() == WL_CONNECTED || WiFi.softAPgetStationNum() > 0) && !keypadModeEnabled;          
               if (canSend) {
                if (dmx_mutex != NULL && xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                    mutex_owner = 2; // DMX Task prende il controllo
                    sendArtDmx(settings.universe, main_dmx_buffer);
                    xSemaphoreGive(dmx_mutex);
                    mutex_owner = 0; // Rilasciato
                }
            }
            
            uint32_t currentRefresh = (settings.refreshRate > 0 && settings.refreshRate <= 44) ? settings.refreshRate : 40;
            vTaskDelay(pdMS_TO_TICKS(1000 / currentRefresh));
        }
    }
}

