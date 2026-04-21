#include "hw_manager.h"

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

extern bool keypadModeEnabled;
extern bool artnetConfirmed;
extern bool sceneActive;
extern bool blackoutActive;
extern void saveConfiguration();

bool ultimoStato = HIGH;
volatile bool identifyRunning = false;

// --- IDENTIFY TASK ---
void identifyTask(void* param) {
    identifyRunning = true;
    
    uint32_t colors[] = {
        led.Color(255, 0, 0),
        led.Color(255, 128, 0),
        led.Color(255, 255, 0),
        led.Color(0, 255, 0),
        led.Color(0, 128, 255),
        led.Color(0, 0, 255),
        led.Color(128, 0, 255),
        led.Color(255, 0, 255),
    };
    
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 8; i++) {
            led.setPixelColor(0, colors[i]);
            led.show();
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
    for (int i = 0; i < 5; i++) {
        led.setPixelColor(0, led.Color(255, 255, 255));
        led.show();
        vTaskDelay(pdMS_TO_TICKS(300));
        led.clear();
        led.show();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    led.clear();
    led.show();
    identifyRunning = false;
    vTaskDelete(NULL);
}

void hw_identify() {
    xTaskCreate(identifyTask, "identify", 2048, NULL, 1, NULL);
}

// --- BOOT SEQUENCE ---
void hw_boot() {
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    uint32_t color = wifiOk ? led.Color(0, 255, 0) : led.Color(255, 0, 0);
    
    for (int i = 0; i < 6; i++) {
         esp_task_wdt_reset();
        led.setPixelColor(0, color);
        led.show();
        delay(250);
        led.clear();
        led.show();
        delay(250);
    }
}

// --- INIT ---
void hw_init() {
    pinMode(BTN_PIN, INPUT_PULLUP);
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(RELAY_PIN2, OUTPUT);
    
    led.begin();
    led.clear();
    led.show();
    
    Serial.println("[HW] LED e pulsante inizializzati.");
}

// --- LOOP ---
void hw_loop() {
    // Pulsante
// --- PULSANTE CON LOGICA A TEMPO ---
bool statoAttuale = digitalRead(BTN_PIN);
static uint32_t pressStart = 0;
static bool actionDone = false;

if (statoAttuale == LOW) {
    if (ultimoStato == HIGH) {
        pressStart = millis();
        actionDone = false;
    }

    uint32_t duration = millis() - pressStart;

    // Feedback visivo progressivo mentre si tiene premuto
    if (duration >= 10000 && !actionDone) {
        // Lampeggio rosso — segnala che al rilascio farà factory reset
        static uint32_t lastBlink = 0;
        static bool blinkOn = false;
        if (millis() - lastBlink > 200) {
            blinkOn = !blinkOn;
            lastBlink = millis();
            led.setPixelColor(0, blinkOn ? led.Color(255, 0, 0) : led.Color(0, 0, 0));
            led.show();
        }
    } else if (duration >= 5000 && !actionDone) {
        // Lampeggio giallo — segnala che al rilascio farà wifi reset
        static uint32_t lastBlink2 = 0;
        static bool blinkOn2 = false;
        if (millis() - lastBlink2 > 300) {
            blinkOn2 = !blinkOn2;
            lastBlink2 = millis();
            led.setPixelColor(0, blinkOn2 ? led.Color(255, 200, 0) : led.Color(0, 0, 0));
            led.show();
        }
    }

} else {
    // Pulsante rilasciato — esegui azione in base alla durata
    if (ultimoStato == LOW && !actionDone) {
        actionDone = true;
        uint32_t duration = millis() - pressStart;

        if (duration >= 10000) {
            // Factory reset completo
            for (int i = 0; i < 6; i++) {
                led.setPixelColor(0, led.Color(255, 0, 0));
                led.show(); delay(100);
                led.clear();
                led.show(); delay(100);
            }
            memset(&settings, 0, sizeof(Config));
            strlcpy(settings.hostname, "dmxtoolbox", sizeof(settings.hostname));
            settings.refreshRate = 25;
            strlcpy(settings.easyPin, "0000", sizeof(settings.easyPin));
            saveConfiguration();
            Serial.println("[BTN] Factory reset eseguito. Riavvio...");
            delay(500);
            ESP.restart();

        } else if (duration >= 5000) {
            // Reset solo WiFi
            for (int i = 0; i < 4; i++) {
                led.setPixelColor(0, led.Color(255, 200, 0));
                led.show(); delay(150);
                led.clear();
                led.show(); delay(150);
            }
            memset(settings.ssid, 0, sizeof(settings.ssid));
            memset(settings.pass, 0, sizeof(settings.pass));
            saveConfiguration();
            Serial.println("[BTN] Reset WiFi eseguito. Riavvio...");
            delay(500);
            ESP.restart();

        } else if (duration < 2000) {
            // Click breve — identify
            hw_identify();
        }
    }
}

ultimoStato = statoAttuale;

 if (identifyRunning) return;
    // LED spento — nessuna logica colore
    if (settings.ledMode == 1) {
        led.clear();
        led.show();
        return;
    }

    // Determina colore in base allo stato
    uint32_t color = 0;
    bool pulse = false;

    if (blackoutActive) {
        color = led.Color(180, 0, 80); // Viola
    } else if (sceneActive) {
        color = led.Color(0, 200, 255); // Azzurro
    } else if (keypadModeEnabled) {
        color = led.Color(255, 0, 0);   // Rosso
    } else if (settings.isRunning && settings.mode == 0) {
        color = led.Color(0, 255, 0);   // Verde — DMX IN
    } else if (settings.isRunning && settings.mode == 1) {
        if (artnetConfirmed) {
            color = led.Color(0, 0, 255);    // Blu — ArtNet confermato
        } else {
            color = led.Color(255, 200, 0);  // Giallo — in ricerca
            pulse = true;
        }
    } else {
        color = led.Color(200, 200, 200); // Bianco standby
    }

    // Pulse mode
    if (settings.ledMode == 2 || pulse) {
        static uint32_t lastPulse = 0;
        static bool pulseOn = false;
        if (millis() - lastPulse > (pulse ? 400 : 1000)) {
            pulseOn = !pulseOn;
            lastPulse = millis();
        }
        if (pulseOn) {
            led.setPixelColor(0, color);
        } else {
            led.clear();
        }
    } else {
        led.setPixelColor(0, color);
    }

    led.show();
}

void setRelay(RelayMode mode) {
    switch (mode) {
        case RELAY_THRU:
            digitalWrite(RELAY_PIN, LOW);   // R1 → NC
            digitalWrite(RELAY_PIN2, LOW);  // R2 → NC
            Serial.println("[RELAY] Modalità THRU 1low 2 low");
            delay(15); // ← attesa commutazione fisica
            break;
        case RELAY_ON:
            digitalWrite(RELAY_PIN, HIGH);  // R1 → NO (MAX485 INPUT)
            digitalWrite(RELAY_PIN2, LOW);  // R2 → NC (thru attivo)
            Serial.println("[RELAY] Modalità ON - DMX IN attivo 1 high 2 low");
            delay(50); // ← attesa commutazione fisica
            break;
        case RELAY_OFF:
            digitalWrite(RELAY_PIN, LOW);   // R1 → NC (IN disconnesso)
            digitalWrite(RELAY_PIN2, HIGH); // R2 → NO (thru interrotto)
            Serial.println("[RELAY] Modalità OFF - ArtNet OUT 1 low 2 high");
            delay(15); // ← attesa commutazione fisica
            break;
    }
}


/* 
// COLORI BASE
led.Color(255, 0, 0)      // Rosso
led.Color(0, 255, 0)      // Verde
led.Color(0, 0, 255)      // Blu
led.Color(255, 255, 0)    // Giallo
led.Color(0, 255, 255)    // Ciano/Azzurro
led.Color(255, 0, 255)    // Magenta
led.Color(255, 255, 255)  // Bianco
led.Color(0, 0, 0)        // Spento

// ARANCIO / AMBRA
led.Color(255, 128, 0)    // Arancio
led.Color(255, 80, 0)     // Arancio scuro
led.Color(255, 160, 0)    // Ambra

// VIOLA
led.Color(128, 0, 255)    // Viola
led.Color(180, 0, 255)    // Viola chiaro
led.Color(80, 0, 180)     // Viola scuro
led.Color(180, 0, 80)     // Viola rosato

// VERDE
led.Color(0, 200, 0)      // Verde medio
led.Color(0, 128, 0)      // Verde scuro
led.Color(128, 255, 0)    // Verde lime

// ROSA / CORALLO
led.Color(255, 20, 80)    // Rosa acceso
led.Color(255, 80, 120)   // Rosa chiaro
led.Color(255, 60, 0)     // Corallo

// AZZURRO / TEAL
led.Color(0, 180, 255)    // Azzurro
led.Color(0, 200, 200)    // Teal
led.Color(0, 128, 200)    // Blu oceano

// BIANCO CALDO / FREDDO
led.Color(255, 200, 100)  // Bianco caldo
led.Color(200, 200, 255)  // Bianco freddo
led.Color(180, 180, 180)  // Grigio */