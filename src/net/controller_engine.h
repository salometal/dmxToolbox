#ifndef CONTROLLER_ENGINE_H
#define CONTROLLER_ENGINE_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "../config.h"

void setupControllerEndpoints(AsyncWebServer &server);

#endif