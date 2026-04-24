#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <esp_dmx.h>
#include <LittleFS.h>

// --- PIN HARDWARE FISSI ---
#define DMX_TX_PIN 17 
#define DMX_RX_PIN 16
#define LED_PIN 4
#define BTN_PIN 25
#define RELAY_PIN 22
#define RELAY_PIN2 32

// --- COSTANTI ---
#define DMX_CHANNELS DMX_PACKET_SIZE
#define ARTNET_PORT 6454
#define MAX_SCENES 50        // ← nuovo
#define MAX_MACROS 10        // ← esplicito

// --- STRUTTURA DATI ---
struct Config {
    uint32_t refreshRate;
    uint16_t universe;
    uint16_t lastOffset;
    uint8_t ip[4];
    uint8_t gateway[4];
    uint8_t subnet[4];
    uint8_t target_ip[4];
    uint8_t mode;
    uint8_t use_dhcp;
    uint8_t use_unicast;
    uint8_t lastStep;
    bool isRunning;
    char ssid[32];
    char pass[32];
    char hostname[32];
    char macros[MAX_MACROS][64];
    // snapNames RIMOSSO — ora in scenes.json su LittleFS
    float fadeSnap = 0;
    float fadeMacro = 0;
    float fadeKeypad = 0;
    uint8_t soloLevel = 178;
    uint8_t blackoutAuto = 0;
    bool autoSave = false;
    uint8_t fadeCurve = 1;
    uint8_t ledMode = 0;
    char easyPin[8];
};

// --- VARIABILI GLOBALI ---
extern Config settings;
extern uint8_t *main_dmx_buffer;
extern volatile SemaphoreHandle_t dmx_mutex;
extern bool dmxDriverInstalled; 
extern dmx_port_t dmxPort;
extern bool artnetConfirmed;
extern bool sceneActive;
extern bool blackoutActive;
extern uint32_t lastPacketTime;
extern bool preBlackoutRunning;
extern float crossfadeProgress;
extern bool crossfadeActive;
extern uint8_t crossfade_buffer_a[];
extern float currentFadeTime;
extern uint8_t *main_target_buffer;
extern float keypadFadeProgress;
extern uint8_t keypad_fade_start[];
extern bool keypadFading;
extern int8_t activeSnapId;
extern TaskHandle_t dmxTaskHandle;
extern TaskHandle_t netTaskHandle;

// Nomi scene — array globale caricato da scenes.json
extern char sceneNames[MAX_SCENES][32];

#endif