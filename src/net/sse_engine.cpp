#include "sse_engine.h"

AsyncEventSource events("/events");

void setupSSE(AsyncWebServer &srv) {
    events.onConnect([](AsyncEventSourceClient *client) {
        Serial.println("[SSE] Client connesso");
        client->send("connected", "hello", millis(), 1000);
    });

    srv.addHandler(&events);
    Serial.println("[SSE] Endpoint /events registrato");
}

void sendEvent(const char* type, const String& payload) {
    if (events.count() == 0) return; // Nessun client connesso
    events.send(payload.c_str(), type, millis());
}