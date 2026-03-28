#ifndef KEYPAD_ENGINE_H
#define KEYPAD_ENGINE_H

#include <Arduino.h>
#include "../config.h"

// Stato globale keypad
extern bool isSoloActive;
extern String lastGroupCmd;
extern int currentPivot;
extern int soloLevel;

// Dichiarazioni funzioni
void executeFixture(int pivot, int valDMX, String offsets);
void parseAndExecuteGroups(String target, int val, String off, int spc);
void processStandaloneCommand(String cmd, String type, String offsets, int step);

#endif