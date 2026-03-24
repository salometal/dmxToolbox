#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_dmx.h>
#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "artnet.h"
#include "dmx_logic.h"
#include "network_logic.h"
#include "keypad_logic.h"

// --- GLOBALI ---
AsyncWebServer server(80);
WiFiUDP udp;
uint8_t packetBuffer[600];
//uint8_t *packetBuffer = NULL;
Config settings = {0}; 
uint8_t *main_dmx_buffer = NULL; 
uint8_t *keypad_dmx_buffer = NULL; // Dichiarazione buffer del keypad 
volatile SemaphoreHandle_t dmx_mutex = NULL; 
bool dmxDriverInstalled = false; 
dmx_port_t dmxPort = 1; 
String wifiScanResults = "";
// 0 = Libero, 1 = Task DMX (Core 0), 2 = Art-Net (Core 1), 3 = WebServer
volatile int mutex_owner = 0;
bool keypadModeEnabled = false; // keypad enable

// Flag per gestire lo stato del socket UDP senza usare localPort()
bool udpActive = false;
bool wasRunningBeforeKeypad = false;
bool artnetConfirmed = false;
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





void setupMDNS() {

    if (strlen(settings.hostname) < 3) {
        strcpy(settings.hostname, "dmxtoolbox"); 
        Serial.println("[mDNS] Hostname non valido, uso default: dmxtoolbox");
    }

    // Usiamo l'hostname salvato nelle settings
   if (!MDNS.begin(settings.hostname)) {
        Serial.println("Errore configurazione mDNS!");
    } else {
        Serial.printf("mDNS avviato: http://%s.local\n", settings.hostname);
        
        // OPZIONALE: Annuncia che questo è un servizio HTTP e Art-Net
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("artnet", "udp", 6454);
        MDNS.addService("dmxtoolbox", "tcp", 80);
    }
}


void setup() {
    Serial.begin(115200);
    
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
        
        // Inizializza i nomi macro come vuoti (opzionale se usi memset sopra)
    for(int i=0; i<10; i++) {
    memset(settings.macros[i], 0, sizeof(settings.macros[i]));
    
    memset(settings.snapNames[i], 0, sizeof(settings.snapNames[i]));
}
        

        saveConfiguration(); // Crea il file config.bin iniziale
    }

}

// --- AGGIUNGI QUESTE RIGHE QUI ---
//Serial.println("[DEBUG] Forzatura temporanea MODO 0 per test stabilità...");
//settings.mode = 0; 
//settings.isRunning = false; // Fermiamo anche l'invio automatico per sicurezza
// ---------------------------------

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
    setupMDNS();
    setupWebServer(); 


    // --- AVVIO TASK (UNA VOLTA SOLA) ---
    xTaskCreatePinnedToCore(dmxTask, "DMX_Core0", 8192, NULL, 10, &dmxTaskHandle, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    xTaskCreatePinnedToCore(networkTask, "Net_Core1", 8192, NULL, 5, &netTaskHandle, 1);

    Serial.println("--- SISTEMA PRONTO ---");
}

void loop() { 
    esp_task_wdt_reset();
    
    // Gestione Art-Net UDP dinamica basata sul flag udpActive
    if (settings.isRunning && settings.mode == 1) {
    /*     if (!udpActive) {
            if (udp.begin(ARTNET_PORT)) {
                udpActive = true;
                Serial.println("[UDP] Socket Art-Net APERTO");
            }
        } */
    } else {
        if (udpActive) {
            Serial.println("[DEBUG-CRASH] Sto per eseguire udp.stop()...");
            Serial.flush(); // Forza l'invio immediato al PC
            udp.stop();
            udpActive = false;
            Serial.println("[UDP] Socket Art-Net CHIUSO");
        }
    }

    vTaskDelay(pdMS_TO_TICKS(100)); 
}