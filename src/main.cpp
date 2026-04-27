#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_dmx.h>
#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "core/artnet_engine.h"
#include "core/dmx_engine.h"
#include "core/scene_manager.h" 
#include "net/network_engine.h"
#include "net/keypad_engine.h"
#include "hw/hw_manager.h"
#include "net/sse_engine.h"


// --- GLOBALI ---
AsyncWebServer server(80);
WiFiUDP udp;
uint8_t packetBuffer[600];
//uint8_t *packetBuffer = NULL;
Config settings; 
uint8_t *main_dmx_buffer = NULL; 
uint8_t *keypad_dmx_buffer = NULL; // Dichiarazione buffer del keypad 
uint8_t *main_target_buffer = NULL;
volatile SemaphoreHandle_t dmx_mutex = NULL; 
bool dmxDriverInstalled = false; 
dmx_port_t dmxPort = 1; 
String wifiScanResults = "";
// 0 = Libero, 1 = Task DMX (Core 0), 2 = Art-Net (Core 1), 3 = WebServer
volatile int mutex_owner = 0;
bool keypadModeEnabled = false; // keypad enable

// Flag per gestire lo stato del socket UDP senza usare localPort()
bool udpActive = false;
bool artnetConfirmed = false;
bool sceneActive = false;
bool preBlackoutRunning = false;

// Provisioning
bool provisioningActive = false;
uint32_t provisioningStartTime = 0;
const uint32_t PROVISIONING_TIMEOUT_MS = 180000; // 3 minuti
const char* PROV_SSID = "DMX-TOOLBOX-PRV";
const char* PROV_PASS = "Dmx7oolbox!Prv9";
// fine provisioning 


float crossfadeProgress = 0.0f;
bool crossfadeActive = false;
uint8_t crossfade_buffer_a[513];
float currentFadeTime = 0.0f;
float keypadFadeProgress = 0.0f;
uint8_t keypad_fade_start[513]; // snapshot al momento dell'avvio fade
bool keypadFading = false;


uint32_t lastPacketTime = 0;


TaskHandle_t dmxTaskHandle = NULL;
TaskHandle_t netTaskHandle = NULL;


void initWiFiConnection() {

const char* apSSID = "DMX-toolbox";
    const char* apPASS = "12345678";
    IPAddress apIP(192, 168, 1, 1);
    IPAddress apSubnet(255, 255, 255, 0);

    if (strlen(settings.ssid) > 0) {
        Serial.printf("[NET] Tentativo connessione a: %s\n", settings.ssid);
        
        if (settings.use_dhcp == 0) {
            WiFi.config(IPAddress(settings.ip[0], settings.ip[1], settings.ip[2], settings.ip[3]),
                        IPAddress(settings.gateway[0], settings.gateway[1], settings.gateway[2], settings.gateway[3]),
                        IPAddress(settings.subnet[0], settings.subnet[1], settings.subnet[2], settings.subnet[3]));
        }
        
        WiFi.begin(settings.ssid, settings.pass);

        // --- IL CRONOMETRO DI SICUREZZA ---
        unsigned long startAttemptTime = millis();
        
        // Aspetta finché non si connette O finché non passano 30 secondi
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
            delay(500);
            Serial.print(".");
            esp_task_wdt_reset(); // Reset del watchdog per non far resettare l'ESP durante l'attesa
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\n[NET] WiFi Connesso con successo!");
            return; // Esci dalla funzione, tutto ok
        } else {
            Serial.println("\n[NET] Timeout raggiunto. Il router non risponde.");
        }
    }

    // --- FALLBACK AP ---
  
    Serial.printf("[NET] Avvio AP: %s\n", apSSID);
    WiFi.disconnect(); 
    WiFi.mode(WIFI_AP);
    
    // Configurazione IP 192.168.1.1
    WiFi.softAPConfig(apIP, apIP, apSubnet); // Gateway = apIP
    
    if (WiFi.softAP(apSSID, apPASS)) {
        Serial.print("[NET] AP Pronto. IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("[ERR] Errore avvio Access Point!");
    }

}

    void startProvisioning() {
        if (provisioningActive) return;

        if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[PROV] Impossibile avviare: non connesso a WiFi");
        return;
    }
        // Modalità dual STA+AP
        WiFi.mode(WIFI_AP_STA);
        
        // AP nascosto (hidden=1)
        WiFi.softAP(PROV_SSID, PROV_PASS, 1, 1);
        
        provisioningActive = true;
        provisioningStartTime = millis();
        
        Serial.println("[PROV] Provisioning AP avviato");
    }

    void stopProvisioning() {
        if (!provisioningActive) return;
        
        // Torna in solo STA
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        
        provisioningActive = false;
        Serial.println("[PROV] Provisioning AP fermato");
    }

String resolveHostname(const char* baseHostname) {
    String candidate = String(baseHostname);
    
    // Prima prova il nome base
    int found = MDNS.queryHost(candidate + ".local");
    if (found == 0) {
        return candidate;
    }
    
    // Prova con suffisso numerico progressivo
    for (int i = 1; i <= 20; i++) {
        candidate = String(baseHostname) + "-" + String(i);
        found = MDNS.queryHost(candidate + ".local");
        if (found == 0) {
            return candidate;
        }
    }
    
    // Fallback estremo — non dovrebbe mai arrivarci con 4-5 device
    return String(baseHostname) + "-" + String(millis() % 9999);
}


void setupMDNS(const char* hostname) {
    if (strlen(hostname) < 3) {
        hostname = "dmxtoolbox";
        Serial.println("[mDNS] Hostname non valido, uso default: dmxtoolbox");
    }

    if (!MDNS.begin(hostname)) {
        Serial.println("Errore configurazione mDNS!");
    } else {
        Serial.printf("mDNS avviato: http://%s.local\n", hostname);
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("artnet", "udp", 6454);
        MDNS.addService("dmxtoolbox", "tcp", 80);
        MDNS.addServiceTxt("dmxtoolbox", "tcp", "type", "pro");
        MDNS.addServiceTxt("dmxtoolbox", "tcp", "version", "2.5");
        MDNS.addServiceTxt("dmxtoolbox", "tcp", "caps", "dmxin,artnet,keypad,snap");
    }
}


void setup() {
    Serial.begin(115200);

     hw_init();   
pinMode(RELAY_PIN, OUTPUT);
pinMode(RELAY_PIN2, OUTPUT);
setRelay(RELAY_ON); // default al boot

    
    // Inizializzazione Watchdog (10 secondi)
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL); 

    // Inizializzazione Mutex e Buffer
    dmx_mutex = xSemaphoreCreateMutex();
    main_dmx_buffer = (uint8_t *)heap_caps_malloc(516, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (main_dmx_buffer != NULL) memset(main_dmx_buffer, 0, 516);

     keypad_dmx_buffer = (uint8_t *)heap_caps_malloc(516, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (keypad_dmx_buffer != NULL) {
        memset(keypad_dmx_buffer, 0, 516 );
    } 
    main_target_buffer = (uint8_t *)heap_caps_malloc(516, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (main_target_buffer != NULL) memset(main_target_buffer, 0, 516);

    delay(1000);


    // LittleFS
if(LittleFS.begin(true)) {
    if (LittleFS.exists("/config.bin")) {
        File file = LittleFS.open("/config.bin", "r");
        if (file) {
            file.read((uint8_t*)&settings, sizeof(Config));
            file.close();
            Serial.println("[FS] Configurazione caricata.");
        }
    } else {
        // --- PRIMO AVVIO IN ASSOLUTO ---
        Serial.println("[FS] Config non trovata. Inizializzazione Default...");
        memset(&settings, 0, sizeof(Config)); // Pulisce tutta la struct (nomi macro inclusi)
        
        // Imposta valori minimi di fabbrica
        strlcpy(settings.hostname, "dmxtoolbox", sizeof(settings.hostname));
        settings.refreshRate = 25;
        settings.universe = 0;
        strlcpy(settings.easyPin, "0000", sizeof(settings.easyPin));
        
        // Inizializza i nomi macro come vuoti (opzionale se usi memset sopra)
            for(int i=0; i<MAX_MACROS; i++) {
                memset(settings.macros[i], 0, sizeof(settings.macros[i]));
            }
        

        saveConfiguration(); // Crea il file config.bin iniziale
    }
    loadScenes();

    if (!settings.autoSave) {
    settings.isRunning = false;
    Serial.println("[SYSTEM] AutoSave disabilitato — boot in standby");
}

}



    // --- PROTEZIONE ANTI-ZERO E LIMITI ---
    // Se il file non esiste o è corrotto, settings.refreshRate potrebbe essere 0
    if (settings.refreshRate < 10 || settings.refreshRate > 44) {
        settings.refreshRate = 25; // Default Standard
        Serial.println("[SYSTEM] Refresh Rate resettato a 25Hz (Safe Default)");
    }

    // Altre sicurezze opzionali (se vuoi evitare blocchi)
    if (settings.universe > 32767) settings.universe = 0;
    
    // Scansione WiFi (solo al boot)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks();
    wifiScanResults = "";
    for (int i = 0; i < n; i++) {
        wifiScanResults += WiFi.SSID(i) + "|" + String(WiFi.RSSI(i)) + (i < n - 1 ? "," : "");
    }
    WiFi.scanDelete();




    // --- INSTALLAZIONE UNICA DEL DRIVER DMX ---
    if (!dmxDriverInstalled) {
        dmx_config_t dmx_cfg = DMX_CONFIG_DEFAULT;
        dmx_personality_t personalities[] = {{1, "Default"}};
        if (dmx_driver_install(dmxPort, &dmx_cfg, personalities, 1)) {
            dmx_set_pin(dmxPort, 17, 16, -1); 
            dmxDriverInstalled = true;
            Serial.println("[DMX] Driver installato stabilmente.");
        }
    }



    // Gestione Connessione WiFi
   initWiFiConnection();
    // Risolvi hostname solo se connesso in STA
    if (WiFi.status() == WL_CONNECTED) {
        String resolvedName = resolveHostname(settings.hostname);
        if (resolvedName != String(settings.hostname)) {
            Serial.printf("[mDNS] Conflitto hostname — uso temporaneamente: %s\n", resolvedName.c_str());
        }
        hw_boot();
        setupMDNS(resolvedName.c_str());
    } else {
        hw_boot();
        setupMDNS(settings.hostname);
    }
    setupWebServer();


    // --- AVVIO TASK (UNA VOLTA SOLA) ---
    xTaskCreatePinnedToCore(dmxTask, "DMX_Core0", 8192, NULL, 15, &dmxTaskHandle, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(networkTask, "Net_Core1", 8192, NULL, 3, &netTaskHandle, 1);

    //task fadein keypad
    xTaskCreatePinnedToCore(
    fadeTask,
    "fadeTask",
    4096,
    NULL,
    1,
    NULL,
    1  // Core 1 — stesso del network
    );
    //task crossfade 
    xTaskCreatePinnedToCore(
    crossfadeTask,
    "crossfadeTask",
    4096,
    NULL,
    1,
    NULL,
    1
);

    Serial.println("--- SISTEMA PRONTO ---");
}

void loop() { 
    hw_loop();
    esp_task_wdt_reset();

    if (provisioningActive && millis() - provisioningStartTime > PROVISIONING_TIMEOUT_MS) {
        stopProvisioning();
        sendEvent("provisioning_end", "{\"active\":false,\"reason\":\"timeout\"}");
        Serial.println("[PROV] Timeout — provisioning terminato");
    }

    if (settings.isRunning && settings.mode == 1) {
    } else {
        if (udpActive) {
            udp.stop();
            udpActive = false;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); 
}