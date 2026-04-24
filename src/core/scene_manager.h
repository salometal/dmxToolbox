#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "../config.h"

// Array globale nomi scene
extern char sceneNames[MAX_SCENES][32];

// Gestione scene
void loadScenes();
void saveScenes();
void saveSnap(int id, const char* name);
void runSnap(int id);
void runSnapExternal(int id, float fade);

// Gestione macro
void saveMacro(int id, const char* name);
void runMacro(int id);

#endif