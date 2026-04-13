
#include "dmx_engine.h" 
#include"../hw/hw_manager.h"
#include <esp_dmx.h>
#include <esp_task_wdt.h>

// Riferimenti globali
extern dmx_port_t dmxPort; 
extern bool dmxDriverInstalled;
extern volatile SemaphoreHandle_t dmx_mutex;
extern uint8_t *main_dmx_buffer;
extern uint8_t *keypad_dmx_buffer;
extern uint8_t *main_target_buffer;
extern TaskHandle_t dmxTaskHandle;
extern volatile int mutex_owner; 
extern bool keypadModeEnabled;


// Aggiungiamo il riferimento alla funzione di invio (che deve essere in un .h o sopra il task)
void sendArtDmx(uint16_t universe, uint8_t* dmxData);

void dmxTask(void *pvParameters) {
    while (!dmxDriverInstalled || dmx_mutex == NULL || main_dmx_buffer == NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    Serial.println("[DMX] Task operativo su Core 0.");
    
    uint8_t local_buffer[DMX_PACKET_SIZE]; 
    memset(local_buffer, 0, DMX_PACKET_SIZE);
    
    uint32_t lastSnifferTime = 0;
    uint32_t lastHeartbeat = 0;
    uint32_t lastOverrideLog = 0;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static uint8_t lastPinMode = 255;

    while (true) {
        esp_task_wdt_reset();

        if (!settings.isRunning) {
            if (sceneActive) {
                // Continua a mandare il buffer scena
                dmx_write(dmxPort, main_dmx_buffer, DMX_PACKET_SIZE);
                dmx_send(dmxPort);
                dmx_wait_sent(dmxPort, pdMS_TO_TICKS(30));
                vTaskDelay(pdMS_TO_TICKS(40));
                continue;
            }
            if (millis() - lastHeartbeat > 5000) {
                Serial.println("[CORE 0] Standby (Task vivo)");
                lastHeartbeat = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue; 
        }
         // ==========================================================
        // CASO KEYPAD ATTIVO (Override Totale)
        // ==========================================================


if (keypadModeEnabled) {


            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(local_buffer, keypad_dmx_buffer, DMX_PACKET_SIZE);
                xSemaphoreGive(dmx_mutex); 
            }
            
            // Invio Fisico (JZK)
            dmx_write(dmxPort, local_buffer, DMX_PACKET_SIZE);
            dmx_send(dmxPort);
            dmx_wait_sent(dmxPort, pdMS_TO_TICKS(30));
            
            // Invio Art-Net
            sendArtDmx(settings.universe, local_buffer);

            // Log di stato (usando la variabile lastOverrideLog che devi avere in alto)
            if (millis() - lastOverrideLog > 2000) {
                //Serial.println("[CORE 0] Keypad Mode Active - Override ON");
                lastOverrideLog = millis();
            }

            // Timing basato sul Refresh Rate
            uint32_t hz = (settings.refreshRate > 0) ? settings.refreshRate : 30;
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(1000 / hz));
        } 
        // ==========================================================
        // CASO B: FUNZIONAMENTO NORMALE
        // ==========================================================
        else {
            // ... qui continua il tuo "if (settings.mode == 0)" ...

        // --- LOGICA DMX ---
        if (settings.mode == 0) { 
            // MODO 0: RICEZIONE (DMX -> ARTNET)

                if (lastPinMode != 0) {
                        setRelay(RELAY_ON);
                        Serial.println("relay ON");
                        dmx_set_pin(dmxPort, DMX_TX_PIN, DMX_RX_PIN, -1);
                        lastPinMode = 0;
                        Serial.println("[DMX] Pin RX abilitato per Modo 0");
                    }
            dmx_packet_t packet;
            // Aspettiamo il pacchetto fisico in ingresso
            if (dmx_receive(dmxPort, &packet, pdMS_TO_TICKS(100))) {
                // Serial.println("[DMX] Pacchetto ricevuto!");
                if (packet.err == DMX_OK) {
                    if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        dmx_read(dmxPort, main_dmx_buffer, DMX_PACKET_SIZE);
                       //  Serial.printf("[DMX IN] Ch1:%d Ch2:%d Ch3:%d\n", main_dmx_buffer[1], main_dmx_buffer[2], main_dmx_buffer[3]);
                        xSemaphoreGive(dmx_mutex);
                        
                        // ORA INVIAMO VIA ART-NET
                        sendArtDmx(settings.universe, main_dmx_buffer);

                        if (millis() - lastSnifferTime > 2000) {
                            Serial.printf("[DMX->ART] Universo: %d | Hz: %d\n", settings.universe, settings.refreshRate);
                            lastSnifferTime = millis();
                        }
                    }
                }
            }
            // In modo 0 il timing lo dà la sorgente DMX, facciamo solo un micro-sleep
            vTaskDelay(pdMS_TO_TICKS(1)); 
        }
        else {
            // MODO 1: INVIO (ART-NET o STANDALONE -> DMX)
                if (lastPinMode != 1) {
                        setRelay(RELAY_OFF);
                        Serial.println("RELAY off");

                        dmx_set_pin(dmxPort, DMX_TX_PIN, -1, -1);
                        lastPinMode = 1;
                        Serial.println("[DMX] Pin RX disabilitato per Modo 1");
                    }


            // 1. Calcolo intervallo in base agli Hz (es. 1000/40 = 25ms)
                // Calcolo protetto: se refreshRate è 0, usa 30Hz (Standard Stabile)


               
                 uint32_t currentHz = (settings.refreshRate > 0) ? settings.refreshRate : 30;
                uint32_t periodMs = 1000 / currentHz; 


            
           

            // 2. Shadow Copy
            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                memcpy(local_buffer, main_dmx_buffer, DMX_PACKET_SIZE);
                xSemaphoreGive(dmx_mutex); 
            }
            
            // 3. INVIO FISICO
            dmx_write(dmxPort, local_buffer, DMX_PACKET_SIZE);
            dmx_send(dmxPort);
            dmx_wait_sent(dmxPort, pdMS_TO_TICKS(30));

            // 4. TIMING PRECISO basato sul Refresh Rate scelto nel Form
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(periodMs));
        }
    }
  }
}
void crossfadeTask(void *pvParameters) {
    while (true) {
        if (crossfadeActive && currentFadeTime > 0) {
                      //  Serial.printf("[CROSSFADE] Progress: %.2f fadeTime: %.1f\n", crossfadeProgress, currentFadeTime);

            float steps = (currentFadeTime * 1000.0f) / 20.0f;
            float stepSize = 1.0f / steps;
            
            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                crossfadeProgress += stepSize;
                
                if (crossfadeProgress >= 1.0f) {
                    // Fade completato — copia target in main buffer
                    crossfadeProgress = 1.0f;
                    crossfadeActive = false;
                    memcpy(main_dmx_buffer, main_target_buffer, 513);
                }  else {
            float t = crossfadeProgress;
            float tCurved;
            
            switch (settings.fadeCurve) {
                case 0: tCurved = t; break;
                case 1: tCurved = t * t * (3.0f - 2.0f * t); break;
                case 2: tCurved = sqrt(t); break;
                case 3: tCurved = log(1.0f + t * 9.0f) / log(10.0f); break;
                case 4: tCurved = (pow(10.0f, t) - 1.0f) / 9.0f; break;
                default: tCurved = t;
            }
            
            for (int i = 1; i <= 512; i++) {
                float start = (float)crossfade_buffer_a[i];
                float target = (float)main_target_buffer[i];
                main_dmx_buffer[i] = (uint8_t)constrain(
                    (int)(start + (target - start) * tCurved), 0, 255
                );
            }
        }
                xSemaphoreGive(dmx_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}