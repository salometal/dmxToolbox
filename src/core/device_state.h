#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <Arduino.h>

const char* getDeviceState();
const char* getPreviousDeviceState();
void saveCurrentState();
void restoreState();

#endif