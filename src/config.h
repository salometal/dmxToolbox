#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <esp_dmx.h>
#include <LittleFS.h>

// --- PIN HARDWARE FISSI ---
#define DMX_TX_PIN 17 
#define DMX_RX_PIN 16 

// --- COSTANTI ---
#define DMX_CHANNELS DMX_PACKET_SIZE
#define ARTNET_PORT 6454

// --- STRUTTURA DATI ---
struct Config {
    // 1. Tipi a 32-bit (Allineamento a 4 byte)
    uint32_t refreshRate;   // 4 byte 

    // 2. Tipi a 16-bit (Allineamento a 2 byte)
    uint16_t universe;      // 2 byte 
    uint16_t lastOffset;    // 2 byte 

    // 3. Tipi a 8-bit e Array di byte (Nessun allineamento critico)
    uint8_t ip[4];          // 4 byte 
    uint8_t gateway[4];     // 4 byte 
    uint8_t subnet[4];      // 4 byte 
    uint8_t target_ip[4];   // 4 byte 
    uint8_t mode;           // 1 byte (0: DMX->ART, 1: ART->DMX, 2: STANDALONE) 
    uint8_t use_dhcp;       // 1 byte 
    uint8_t use_unicast;    // 1 byte 
    uint8_t lastStep;       // 1 byte 
    bool isRunning;         // 1 byte (Il "freno a mano") 

    // 4. Array di caratteri (Stringhe fisse - mettile sempre in fondo)
    char ssid[32];          // 32 byte 
    char pass[32];          // 32 byte 
    char hostname[32];      // 32 byte 
    char macros[10][64];    // 640 byte 
    char snapNames[10][16]; // 160 byte 

};

// --- VARIABILI GLOBALI (DICHIARAZIONI) ---
// Queste dicono a tutti i file .h che le variabili esistono nel main.cpp
extern Config settings;
extern uint8_t *main_dmx_buffer;
extern volatile SemaphoreHandle_t dmx_mutex;
extern bool dmxDriverInstalled; 
extern dmx_port_t dmxPort;
extern bool artnetConfirmed;
extern bool sceneActive;
extern uint32_t lastPacketTime;


// AGGIUNGI QUESTI: Servono al loop() per gestire i task
extern TaskHandle_t dmxTaskHandle;
extern TaskHandle_t netTaskHandle;

#endif