#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "../config.h"

// Dichiarazioni funzioni
void saveMacro(int id, const char* name);
void runMacro(int id);
void saveSnap(int id, const char* name);
void runSnap(int id);

#endif