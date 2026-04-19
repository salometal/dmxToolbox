#ifndef NETWORK_ENGINE_H
#define NETWORK_ENGINE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include "../config.h"

void saveConfiguration();
void setupWebServer();
void networkTask(void *pvParameters);
void setupUpdateEndpoints(AsyncWebServer &srv);

#endif