#include "keypad_engine.h"
#include <Arduino.h>
#include "config.h"
// Riferimenti esterni
extern uint8_t *keypad_dmx_buffer;
// Buffer target per il fade
uint8_t keypad_target_buffer[513];

extern volatile SemaphoreHandle_t dmx_mutex;
extern volatile int mutex_owner;
extern bool keypadModeEnabled;
extern float keypadFadeProgress;
extern uint8_t keypad_fade_start[];
extern bool keypadFading;

// STATO DEL SISTEMA (Persistente tra le chiamate)
bool isSoloActive = false;
String lastGroupCmd = "";
int currentPivot = 1;
int soloLevel = settings.soloLevel; 

/**
 * FUNZIONE CORE: Applica l'intensità a un Pivot + i suoi Offsets
 * Qui è dove avviene il calcolo DMX "Ibrido"
 */
void executeFixture(int pivot, int valDMX, String offsets) {
    char* copyOff = strdup(offsets.c_str());
    char* ptrOff = strtok(copyOff, ",");
    
    while (ptrOff != NULL) {
        int offVal = atoi(ptrOff);
        int targetChan = pivot + (offVal - 1);
        
        if (targetChan >= 1 && targetChan <= 512) {
            Serial.printf("  [DMX Out] Canale %d -> Valore %d\n", targetChan, valDMX);
            if (settings.fadeKeypad > 0) {
                keypad_target_buffer[targetChan] = (uint8_t)valDMX;
            } else {
                keypad_dmx_buffer[targetChan] = (uint8_t)valDMX;
            }
        }
        ptrOff = strtok(NULL, ",");
    }
    free(copyOff);
}
/**
 * PARSER PRINCIPALE
 */
/**
 * FUNZIONE DI SUPPORTO: Parsing di gruppi (THRU / +)
 * Gestisce l'accensione di più fixture e aggiorna il currentPivot all'ultimo elemento.
 */
void parseAndExecuteGroups(String target, int val, String off, int spc) {
    int lp = currentPivot; // Valore di fallback
    lastGroupCmd = target;
    if (target.indexOf(" THRU ") != -1) {
        int tPos = target.indexOf(" THRU ");
        int startID = target.substring(0, tPos).toInt();
        int endID = target.substring(tPos + 6).toInt();
        
        if (startID > 0 && endID > 0) {
            for (int p = startID; p <= endID; p += spc) {
                executeFixture(p, val, off);
                lp = p; 
            }
        }
    } else {
        int startPos = 0;
        int plusPos = target.indexOf("+");
        while (startPos < target.length()) {
            String currentFixStr = (plusPos != -1) ? target.substring(startPos, plusPos) : target.substring(startPos);
            currentFixStr.trim(); // Modifica la stringa 'in place'
            int p = currentFixStr.toInt();
            if (p > 0) {
                executeFixture(p, val, off);
                lp = p;
            }
            if (plusPos == -1) break;
            startPos = plusPos + 1;
            plusPos = target.indexOf("+", startPos);
        }
    }
    currentPivot = lp; // Aggiornamento globale del pivot
}

/**
 * PARSER PRINCIPALE
 */
void processStandaloneCommand(String cmd, String type, String offsetStr, int spacing) {
    // 1. RICEZIONE E PULIZIA
    cmd.toUpperCase();
    cmd.trim();
    type.toUpperCase();
    type.trim();
    offsetStr.replace(".", ",");
    

    // TENTATIVO PRESA MUTEX
    if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        mutex_owner = 3; // Identificativo Standalone

        // 2. CHECK TASTI RAPIDI (CLEAR / SOLO)
        if (type == "CLEAR") {
            
            lastGroupCmd = "";
            currentPivot = 1;
            memset(keypad_dmx_buffer, 0, 513);
            memset(keypad_target_buffer, 0, 513); // ← aggiunto
            Serial.println("[SYSTEM] Blackout & Clear Totale");
            xSemaphoreGive(dmx_mutex);
            mutex_owner = 0;
            return;
        }

        if (type == "SOLO") {
            isSoloActive = !isSoloActive;
            soloLevel = settings.soloLevel; // Reset 70%
            Serial.printf("[SYSTEM] Modalità SOLO: %s\n", isSoloActive ? "ON" : "OFF");
            xSemaphoreGive(dmx_mutex);
            mutex_owner = 0;
            return;
        }

            // 3. CHECK NAVIGAZIONE (NEXT/LAST in SOLO)
        if ((type == "NEXT" || type == "LAST") && isSoloActive) {
            if (cmd != "") {
                memset(keypad_dmx_buffer, 0, 513);
                memset(keypad_target_buffer, 0, 513);
                parseAndExecuteGroups(cmd, soloLevel, offsetStr, spacing);
            }
            xSemaphoreGive(dmx_mutex);
            mutex_owner = 0;
            return;
        }
        // 4. PARSING STRINGA COMANDO
        if (cmd != "") {
            int atPos = cmd.indexOf(" AT ");
            bool isGroup = (cmd.indexOf("+") != -1 || cmd.indexOf(" THRU ") != -1);

           if (isSoloActive) {
    // --- MODALITÀ SOLO ---
                if (atPos != -1) {
                    String strVal = cmd.substring(atPos + 4);
                    soloLevel = (strVal == "FULL") ? 255 : map(strVal.toInt(), 0, 100, 0, 255);
                    soloLevel = constrain(soloLevel, 0, 255);
                    
                    // Snapshot per fade
                    if (settings.fadeKeypad > 0) {
                        memcpy(keypad_fade_start, keypad_dmx_buffer, 513);
                        keypadFadeProgress = 0.0f;
                        keypadFading = true;
                    }
                    memset(keypad_dmx_buffer, 0, 513);
                    memset(keypad_target_buffer, 0, 513);
                    executeFixture(currentPivot, soloLevel, offsetStr);
                } 
                else if (isGroup || cmd.toInt() > 0) {
                    // Snapshot per fade
                    if (settings.fadeKeypad > 0) {
                        memcpy(keypad_fade_start, keypad_dmx_buffer, 513);
                        keypadFadeProgress = 0.0f;
                        keypadFading = true;
                    }
                    memset(keypad_dmx_buffer, 0, 513);
                    memset(keypad_target_buffer, 0, 513);
                    
                    if (isGroup) {
                        parseAndExecuteGroups(cmd, soloLevel, offsetStr, spacing);
                    } else {
                        currentPivot = constrain(cmd.toInt(), 1, 512);
                        lastGroupCmd = cmd;
                        executeFixture(currentPivot, soloLevel, offsetStr);
                    }
                    Serial.printf("[SOLO] Attivo Pivot: %d\n", currentPivot);
                }
}
            else {
                // --- MODALITÀ STANDARD (CHAN) ---
                if (atPos != -1) {
                        String strTutteFixture = cmd.substring(0, atPos);
                        String strValore = cmd.substring(atPos + 4);
                        int dmxVal = (strValore == "FULL") ? 255 : map(strValore.toInt(), 0, 100, 0, 255);
                        
                        // Snapshot per fade
                        if (settings.fadeKeypad > 0) {
                            memcpy(keypad_fade_start, keypad_dmx_buffer, 513);
                            keypadFadeProgress = 0.0f;
                            keypadFading = true;
                        }
                        
                        parseAndExecuteGroups(strTutteFixture, constrain(dmxVal, 0, 255), offsetStr, spacing);
                    }
            }
        }

        // RILASCIO MUTEX FINALE
        xSemaphoreGive(dmx_mutex);
        mutex_owner = 0;
    }
}
void fadeTask(void *pvParameters) {
    while (true) {
        bool shouldFade = keypadModeEnabled && keypadFading;

        if (shouldFade) {
            
            float fadeTime = (currentFadeTime > 0) ? currentFadeTime : settings.fadeKeypad;
            float steps = (fadeTime * 1000.0f) / 20.0f;

            if (xSemaphoreTake(dmx_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                keypadFadeProgress += 1.0f / steps;
                
                if (keypadFadeProgress >= 1.0f) {
                    keypadFadeProgress = 0.0f;
                    currentFadeTime = 0.0f;
                    keypadFading = false; // ← aggiunto
                    memcpy(keypad_dmx_buffer, keypad_target_buffer, 513);
                } else {
                    float t = keypadFadeProgress;
                    float tCurved;
                    
                    switch (settings.fadeCurve) {
                        case 0: // Lineare
                            tCurved = t;
                            break;
                        case 1: // S-Curve
                            tCurved = t * t * (3.0f - 2.0f * t);
                            break;
                        case 2: // Equal Power
                            tCurved = sqrt(t);
                            break;
                        case 3: // Logaritmica
                            tCurved = log(1.0f + t * 9.0f) / log(10.0f);
                            break;
                        case 4: // Esponenziale
                            tCurved = (pow(10.0f, t) - 1.0f) / 9.0f;
                            break;
                        default:
                            tCurved = t;
                    }
                    
                    for (int i = 1; i <= 512; i++) {
                        float start = (float)keypad_fade_start[i];
                        float target = (float)keypad_target_buffer[i];
                        keypad_dmx_buffer[i] = (uint8_t)constrain(
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