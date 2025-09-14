#ifndef UTILS_H
#define UTILS_H

#include <Arduino.h>

//================================
// PROJECT IDENTITY
//================================
// These can be overridden via build flags if desired.
#ifndef PROJECT_NAME
#define PROJECT_NAME "EvoCmdWingKeyboard"
#endif

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "0.3"
#endif

//================================
// DEBUG FUNCTIONS
//================================

// Debug setting
extern bool debugMode;

// Debug print functions - only output if debug mode is enabled
void debugPrint(const char* message);
void debugPrintf(const char* format, ...);

//================================
// UPLOAD / REBOOT COMMAND HANDLING
//================================
// Poll Serial for commands like IDENTIFY, REBOOT_BOOTLOADER, REBOOT_NORMAL
// Only available when a USB Serial interface is compiled in
#if defined(USB_SERIAL) || defined(USB_SERIAL_HID) || defined(USB_TRIPLE_SERIAL)
void checkSerialForReboot();
void processSerialCommand(String cmd);
#endif

#endif // UTILS_H
