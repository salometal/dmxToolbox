#ifndef SSE_ENGINE_H
#define SSE_ENGINE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void setupSSE(AsyncWebServer &srv);
void sendEvent(const char* type, const String& payload);

#endif