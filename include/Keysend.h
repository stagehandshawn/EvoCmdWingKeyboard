// Keysend.h
#ifndef KEYSEND
#define KEYSEND

#include <Arduino.h>
#include <Keyboard.h>

// Initializes USB keyboard and matrix GPIOs
void keyboardInit();

// Runs one scan cycle, debounces, and sends key events
void keyboardScan();

// Optionally release all keys and modifiers (panic/cleanup)
void keyboardReleaseAll();

#endif
