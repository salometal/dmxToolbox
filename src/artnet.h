#ifndef ARTNET_H
#define ARTNET_H

#include <WiFiUdp.h>
#include "config.h"

extern WiFiUDP udp;
extern Config settings;
extern uint8_t *main_dmx_buffer;
extern uint8_t *keypad_dmx_buffer;
extern volatile SemaphoreHandle_t dmx_mutex;
extern volatile int mutex_owner;
extern bool keypadModeEnabled;

const uint8_t artnetId[] = "Art-Net";

// ========================================================
// ART-NET OUT (ESP32 -> Rete)
// ========================================================
void sendArtDmx(uint16_t universe, uint8_t* dmxData) {
    if (dmxData == NULL) return;


    uint8_t packet[530]; 
    memset(packet, 0, 530);
    
    memcpy(packet, artnetId, 8);      
    packet[8]  = 0x00; packet[9]  = 0x50; // OpCode ArtDmx (0x5000)
    packet[10] = 0x00; packet[11] = 0x0e; // Protocol Version 14
    packet[12] = 0x00;                    // Sequence
    packet[13] = 0x00;                    // Physical
    packet[14] = (uint8_t)(universe & 0xFF);        
    packet[15] = (uint8_t)((universe >> 8) & 0xFF); 
    packet[16] = 0x02; packet[17] = 0x00; // Length (512 byte - Big Endian)

    memcpy(packet + 18, dmxData + 1, 512);

    IPAddress targetIP;
    if (settings.use_unicast && settings.target_ip[0] != 0) {
        targetIP = IPAddress(settings.target_ip[0], settings.target_ip[1], 
                             settings.target_ip[2], settings.target_ip[3]);
    } else {
        // --- MODALITÀ BROADCAST INTELLIGENTE (Directed Broadcast) ---
        IPAddress localIP = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        IPAddress subnet  = (WiFi.status() == WL_CONNECTED) ? WiFi.subnetMask() : IPAddress(255, 255, 255, 0);
        
        for (int i = 0; i < 4; i++) {
            targetIP[i] = localIP[i] | (~subnet[i]);
        }
    }

    udp.beginPacket(targetIP, 6454);
    udp.write(packet, 530);
    udp.endPacket();
    
 /* 
 // LOG INVIO ARTNET 
 static uint32_t lastUDPLog = 0;
    if (millis() - lastUDPLog > 2000) {
        Serial.println("[UDP] Pacchetto Art-Net inviato!");
        lastUDPLog = millis();
    } */
}



/* bool readArtDmx(uint8_t* dmxBuffer) {
   if (dmxBuffer == NULL) return false;

    // Se il Keypad è attivo, dobbiamo essere dei "muri"
    if (keypadModeEnabled) {
        // Svuota tutto il buffer UDP rimasto nel chip WiFi
        int packetSize;
        while ((packetSize = udp.parsePacket()) > 0) {
            udp.flush(); 
        }
        // OPZIONALE: Se vuoi che l'Art-Net IN non sporchi minimamente il buffer principale
        // memset(dmxBuffer, 0, 513); 
        return false; 
    }

    static uint8_t internalBuffer[600];
    // Pulizia preventiva per evitare di leggere spazzatura di pacchetti precedenti
    memset(internalBuffer, 0, 600);

    int readLen = udp.read(internalBuffer, 600);

    // Se non c'è nulla, esci subito
    if (readLen <= 0) return false;

    // --- LOG DI DEBUG ESTREMO ---
    // Stampiamo ogni pacchetto che entra, di qualsiasi natura sia.
    // Se crasha, l'ultimo log prima del reboot ci dirà chi è il colpevole.
    Serial.printf("[INCOMING] Len: %d | Hex: %02X %02X %02X %02X %02X %02X %02X %02X\n", 
                  readLen, 
                  internalBuffer[0], internalBuffer[1], internalBuffer[2], internalBuffer[3],
                  internalBuffer[4], internalBuffer[5], internalBuffer[6], internalBuffer[7]);
    Serial.flush(); // Obbligatorio per vedere il log prima del crash

    // Protezione: se il pacchetto è Art-Net deve essere almeno 18 byte
    if (readLen >= 18 && memcmp(internalBuffer, "Art-Net", 7) == 0) {
        
        uint16_t opCode = (internalBuffer[9] << 8) | internalBuffer[8];
        uint16_t incomingUniv = internalBuffer[14] | (internalBuffer[15] << 8);

        if (opCode == 0x5000 && incomingUniv == settings.universe) {
            uint16_t dataLen = (internalBuffer[16] << 8) | internalBuffer[17];
            
            if (dataLen > 512) dataLen = 512;
            if (dataLen > (readLen - 18)) dataLen = readLen - 18; 

            if (dataLen > 0) {
                memcpy(dmxBuffer + 1, internalBuffer + 18, dataLen);
                dmxBuffer[0] = 0;
                return true;
            }
        }
    }

    // Se arriviamo qui, il pacchetto è stato loggato ma non è Art-Net valido.
    return false;
}
 */

bool readArtDmx(uint8_t* dmxBuffer) {
    if (dmxBuffer == NULL) {
        Serial.println("[CRITICO] readArtDmx: dmxBuffer è NULL!");
        return false;
    }
    // Se il Keypad è attivo, dobbiamo essere dei "muri"
    if (keypadModeEnabled) {
        // Svuota tutto il buffer UDP rimasto nel chip WiFi
        int packetSize;
        while ((packetSize = udp.parsePacket()) > 0) {
            udp.flush(); 
        }
        // OPZIONALE: Se vuoi che l'Art-Net IN non sporchi minimamente il buffer principale
        // memset(dmxBuffer, 0, 513); 
        return false; 
    }

    // Usiamo un buffer locale temporaneo per il test, 
    // così evitiamo conflitti con memorie statiche o globali.
    uint8_t tempBuffer[640]; 
    memset(tempBuffer, 0, 640);

    // STEP 1: Lettura fisica dal socket
    int readLen = udp.read(tempBuffer, 640);
    
    if (readLen <= 0) return false;

    // STEP 2: Identificazione pacchetto
    Serial.printf("[READ] Ricevuti %d byte. Header: %c%c%c%c\n", 
                  readLen, tempBuffer[0], tempBuffer[1], tempBuffer[2], tempBuffer[3]);
    Serial.flush(); 

    if (readLen >= 12 && memcmp(tempBuffer, "Art-Net", 7) == 0) {
        uint16_t opCode = (tempBuffer[9] << 8) | tempBuffer[8];
        Serial.printf("[READ] OpCode rilevato: 0x%04X\n", opCode);
        Serial.flush();

        // Se è ArtDmx (0x5000)
        if (opCode == 0x5000) {
            if (readLen < 18) {
                Serial.println("[ERRORE] Pacchetto ArtDmx troppo corto!");
                return false;
            }

            uint16_t incomingUniv = tempBuffer[14] | (tempBuffer[15] << 8);
            uint16_t dataLen = (tempBuffer[16] << 8) | tempBuffer[17];

            Serial.printf("[READ] Univ: %d (Target: %d) | DataLen: %d\n", 
                          incomingUniv, settings.universe, dataLen);
            Serial.flush();

            if (incomingUniv == settings.universe) {
                // Protezione contro buffer overflow
                if (dataLen > 512) dataLen = 512;
                
                // STEP 3: Scrittura nel buffer DMX
                // Usiamo dmxBuffer+1 per saltare lo Start Code 0
                Serial.println("[READ] Inizio memcpy su dmxBuffer...");
                Serial.flush();
                
                memcpy(dmxBuffer + 1, tempBuffer + 18, dataLen);
                dmxBuffer[0] = 0; // Start Code standard

                Serial.println("[READ] Memcpy completata con successo.");
                Serial.flush();
                return true;
            }
        } else {
            Serial.printf("[READ] Pacchetto Art-Net ignorato (OpCode non 0x5000)\n");
            Serial.flush();
        }
    } else {
        Serial.println("[READ] Pacchetto non Art-Net o troppo corto.");
        Serial.flush();
    }

    return false;
}


#endif