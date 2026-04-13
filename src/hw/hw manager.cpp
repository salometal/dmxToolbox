#include "hw_manager.h"

Adafruit_NeoPixel led(1, LED_PIN, NEO_GRB + NEO_KHZ800);

bool ultimoStato = HIGH;
int colore = 0;

void hw_init() {
    pinMode(BTN_PIN, INPUT_PULLUP);
    led.begin();
    led.clear();
    led.show();
    Serial.println("[HW] LED e pulsante inizializzati.");
}

void hw_loop() {
    bool statoAttuale = digitalRead(BTN_PIN);
    
    if (ultimoStato == HIGH && statoAttuale == LOW) {
        colore = (colore + 1) % 3;
        switch (colore) {
            case 0: led.setPixelColor(0, led.Color(255, 0, 0)); break;
            case 1: led.setPixelColor(0, led.Color(0, 255, 0)); break;
            case 2: led.setPixelColor(0, led.Color(0, 0, 255)); break;
        }
        led.show();
        Serial.printf("[HW] Colore: %d\n", colore);
        delay(50);
    }
    
    ultimoStato = statoAttuale;
}

void setRelay(RelayMode mode) {
    switch (mode) {
        case RELAY_THRU:
            digitalWrite(RELAY_PIN, LOW);   // R1 → NC
            digitalWrite(RELAY_PIN2, LOW);  // R2 → NC
            Serial.println("[RELAY] Modalità THRU 1low 2 low");
            break;
        case RELAY_ON:
            digitalWrite(RELAY_PIN, HIGH);  // R1 → NO (MAX485 INPUT)
            digitalWrite(RELAY_PIN2, LOW);  // R2 → NC (thru attivo)
            Serial.println("[RELAY] Modalità ON - DMX IN attivo 1 high 2 low");
            break;
        case RELAY_OFF:
            digitalWrite(RELAY_PIN, LOW);   // R1 → NC (IN disconnesso)
            digitalWrite(RELAY_PIN2, HIGH); // R2 → NO (thru interrotto)
            Serial.println("[RELAY] Modalità OFF - ArtNet OUT 1 low 2 high");
            break;
    }
}