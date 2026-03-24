#ifndef ARTNET_ENGINE_H
#define ARTNET_ENGINE_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include "../config.h"

bool readArtDmx(uint8_t* dmxBuffer);
void sendArtDmx(uint16_t universe, uint8_t* dmxData);

#endif