#include <Arduino.h>
#include "Keysend.h"
#include "utils.h"

void setup() {
  // Initialize Serial
  if (debugMode) {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {
   }
  }
  debugPrint("Booting EvoCmdWingKeyboard...");

  // Initialize keyboard matrix
  keyboardInit();
}

void loop() {
  keyboardScan();

  #if defined(USB_SERIAL) || defined(USB_SERIAL_HID) 
  checkSerialForReboot();
  #endif
  
}
