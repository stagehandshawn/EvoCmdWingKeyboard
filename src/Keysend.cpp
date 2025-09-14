#include <Arduino.h>
#include "Keysend.h"
#include <Keyboard.h>
#include "utils.h"

// Matrix
static const uint8_t NUM_ROWS = 10;
static const uint8_t NUM_COLS = 14;

// Debounce time in milliseconds
static const uint32_t DEBOUNCE_MS = 5;

// Scan interval in milliseconds
static const uint32_t SCAN_INTERVAL_MS = 1;
// Column select settle time in microseconds
static const uint32_t SELECT_SETTLE_US = 5;

// ================================
// Matrix pin mapping (Teensy 4.0)
//
// COL2ROW diodes: diodes from Column (anode) to Row (cathode).
// Scanning: drive one ROW LOW at a time, and read COLUMNS with pull-ups.
// Pressed key => corresponding column reads LOW when its row is selected.
// ================================

static const uint8_t colPins[NUM_COLS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14
};

static const uint8_t rowPins[NUM_ROWS] = {
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24
};

// Onboard LED pin for Teensy 4.0
static const uint8_t LED_PIN = 13;

// ================================
// Key action
// ================================

// Modifier bitmask
enum ModMask : uint8_t {
  MOD_NONE  = 0,
  MOD_LCTRL = 1 << 0,
  MOD_LALT  = 1 << 1,
  MOD_LSHIFT= 1 << 2,
};

// A key action can be:
// - a base key (ASCII or KEY_* usage) with optional modifiers (held while pressed)
// - a pure modifier (e.g., physical Left Shift)
struct KeyAction {
  bool valid;           // false: empty position
  bool modifierOnly;    // true: no base key, only modifiers
  uint16_t baseKey;     // ASCII or KEY_* usage (Teensy uses 0xF000 marker)
  ModMask mods;         // modifiers to hold while this key is pressed
};

// Helper constructors
static inline KeyAction KA_empty() { return {false, false, 0, MOD_NONE}; }
static inline KeyAction KA_base(char ascii) { return {true, false, (uint16_t)ascii, MOD_NONE}; }
static inline KeyAction KA_key(uint16_t keycode) { return {true, false, keycode, MOD_NONE}; }
static inline KeyAction KA_chord(char ascii, ModMask m) { return {true, false, (uint16_t)ascii, m}; }
static inline KeyAction KA_chord_key(uint16_t keycode, ModMask m) { return {true, false, keycode, m}; }
static inline KeyAction KA_mod(ModMask m) { return {true, true, 0, m}; }

// ================================
// QMK-derived keymap -> KeyAction
//
// We omit KC_NO positions; only non-empty entries are defined.
// Notes:
// - For keypad keys (e.g., KC_KP_PLUS, KC_PAST) we map to '+' and '*'
//   to keep compatibility without relying on keypad-specific constants.
// - Special keys use Arduino/Teensy KEY_* where needed.
// ================================

// Alias for readability
static constexpr ModMask C = MOD_LCTRL;
static constexpr ModMask A = MOD_LALT;

// Use Teensy core-provided keycodes (from keylayouts.h via Arduino.h)
// and modifier key codes (MODIFIERKEY_*). Do not redefine them here.

// Matrix mapping: [row][col]
static const KeyAction keymap[NUM_ROWS][NUM_COLS] = {
  // Row 0
  { KA_chord('p', A), KA_chord('n', A), KA_chord('h', A), KA_chord('o', C), KA_chord('u', C),
    KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty() },

  // Row 1
  { KA_chord('f', A), KA_chord('u', A), KA_chord('g', A), KA_chord('m', C), KA_chord('c', C),
    KA_key(KEY_F1), KA_key(KEY_F2), KA_key(KEY_F3), KA_key(KEY_F4), KA_empty(),
    KA_key(KEY_F5), KA_key(KEY_F6), KA_key(KEY_F7), KA_key(KEY_F8) },

  // Row 2
  { KA_chord('f', A), KA_chord('d', A), KA_chord('b', A), KA_chord('d', C), KA_chord('l', C),
    KA_key(KEY_INSERT), KA_key(KEY_HOME), KA_key(KEY_PAGE_UP), KA_key(KEY_F12), KA_empty(),
    KA_key(KEY_DELETE), KA_key(KEY_END), KA_key(KEY_PAGE_DOWN), KA_key(KEYPAD_PLUS) },

  // Row 3
  { KA_empty(), KA_empty(), KA_chord('w', A), KA_chord('s', C), KA_chord('h', C),
    KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty() },

  // Row 4
  { KA_empty(), KA_empty(), KA_chord('v', A), KA_chord('e', C), KA_chord('z', C),
    KA_chord('r', A), KA_chord('k', A), KA_chord('z', A), KA_empty(), KA_chord('j', A),
    KA_empty(), KA_chord('l', A), KA_chord('.', A), KA_chord(',', A) },

  // Row 5
  { KA_empty(), KA_empty(), KA_chord('[', A), KA_chord('f', C), KA_chord('n', C), KA_chord('g', C),
    KA_empty(), KA_base('7'), KA_base('8'), KA_base('9'), KA_chord('=', A), KA_empty(), KA_chord('o', A), KA_empty() },

  // Row 6
  { KA_empty(), KA_empty(), KA_chord(']', A), KA_chord('p', C), KA_chord('q', C), KA_chord('w', C),
    KA_empty(), KA_base('4'), KA_base('5'), KA_base('6'), KA_chord('t', A), KA_empty(), KA_empty(), KA_empty() },

  // Row 7
  { KA_empty(), KA_key(KEYPAD_ASTERIX), KA_empty(), KA_empty(), KA_empty(), KA_empty(), KA_empty(),
    KA_base('1'), KA_base('2'), KA_base('3'), KA_base('-'), KA_empty(), KA_empty(), KA_empty() },

  // Row 8
  { KA_empty(), KA_base(']'), KA_chord('\'', A), KA_chord('x', C), KA_chord('a', C), KA_chord('j', C),
    KA_empty(), KA_base('0'), KA_base('.'), KA_chord('8', A), KA_chord('2', A), KA_empty(), KA_key(KEY_ESC), KA_empty() },

  // Row 9
  { KA_empty(), KA_base('['), KA_chord(';', A), KA_chord('b', C), KA_empty(), KA_chord('s', A),
    KA_empty(), KA_mod(MOD_LSHIFT), KA_chord('/', C), KA_empty(), KA_key(KEY_ENTER), KA_empty(), KA_chord_key(KEY_BACKSPACE, A), KA_chord('y', A) }
};

// ================================
// Matrix state & debounce
// ================================

static bool rawState[NUM_ROWS][NUM_COLS];       // instant reads
static bool lastRaw[NUM_ROWS][NUM_COLS];        // previous raw for debounce timing
static bool debounced[NUM_ROWS][NUM_COLS];      // stable state
static uint32_t lastChange[NUM_ROWS][NUM_COLS]; // time of last raw change

// Modifier reference counts (to keep them held while any chord needs them)
static uint16_t refCtrl = 0;
static uint16_t refAlt  = 0;
static uint16_t refShift= 0;

// Count of currently pressed keys (for LED debug indication)
static uint16_t pressedCount = 0;

// ================================
// GPIO helpers
// ================================

static void unselectAllRows() {
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    pinMode(rowPins[r], INPUT); // Hi-Z when not selected
  }
}

static void selectRowLow(uint8_t row) {
  // Unselect others (Hi-Z)
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    if (r == row) continue;
    pinMode(rowPins[r], INPUT);
  }
  // Selected row actively driven LOW
  pinMode(rowPins[row], OUTPUT);
  digitalWrite(rowPins[row], LOW);
}

// ================================
// Modifier press/release helpers
// ================================

static void pressModifiers(ModMask m) {
  if (m & MOD_LCTRL) { if (refCtrl++ == 0) Keyboard.press(MODIFIERKEY_LEFT_CTRL); }
  if (m & MOD_LALT)  { if (refAlt++  == 0) Keyboard.press(MODIFIERKEY_LEFT_ALT); }
  if (m & MOD_LSHIFT){ if (refShift++== 0) Keyboard.press(MODIFIERKEY_LEFT_SHIFT); }
}

static void releaseModifiers(ModMask m) {
  if (m & MOD_LCTRL) { if (refCtrl > 0 && --refCtrl == 0) Keyboard.release(MODIFIERKEY_LEFT_CTRL); }
  if (m & MOD_LALT)  { if (refAlt  > 0 && --refAlt  == 0) Keyboard.release(MODIFIERKEY_LEFT_ALT); }
  if (m & MOD_LSHIFT){ if (refShift> 0 && --refShift== 0) Keyboard.release(MODIFIERKEY_LEFT_SHIFT); }
}

// ================================
// Handlers
// ================================

static void handleKeyPress(uint8_t r, uint8_t c) {
  const KeyAction &ka = keymap[r][c];
  if (!ka.valid) return;

  if (ka.modifierOnly) {
    // Physical modifier key (e.g., Left Shift)
    pressModifiers(ka.mods);
    debugPrintf("PRESS MOD r=%u c=%u mods=%u", r, c, (unsigned)ka.mods);
    if (++pressedCount == 1) digitalWrite(LED_PIN, HIGH);
    return;
  }

  // Press modifiers first, then base key
  if (ka.mods != MOD_NONE) pressModifiers(ka.mods);
  Keyboard.press(ka.baseKey);
  debugPrintf("PRESS r=%u c=%u key=%u mods=%u", r, c, (unsigned)ka.baseKey, (unsigned)ka.mods);
  if (++pressedCount == 1) digitalWrite(LED_PIN, HIGH);
}

static void handleKeyRelease(uint8_t r, uint8_t c) {
  const KeyAction &ka = keymap[r][c];
  if (!ka.valid) return;

  if (ka.modifierOnly) {
    releaseModifiers(ka.mods);
    debugPrintf("RELEASE MOD r=%u c=%u mods=%u", r, c, (unsigned)ka.mods);
    if (pressedCount > 0 && --pressedCount == 0) digitalWrite(LED_PIN, LOW);
    return;
  }

  // Release base key first, then modifiers if no other keys need them
  Keyboard.release(ka.baseKey);
  if (ka.mods != MOD_NONE) releaseModifiers(ka.mods);
  debugPrintf("RELEASE r=%u c=%u key=%u mods=%u", r, c, (unsigned)ka.baseKey, (unsigned)ka.mods);
  if (pressedCount > 0 && --pressedCount == 0) digitalWrite(LED_PIN, LOW);
}

void keyboardInit() {
  // Initialize USB keyboard
  Keyboard.begin();

  // Initialize columns as inputs with pullups (readers)
  for (uint8_t c = 0; c < NUM_COLS; ++c) {
    pinMode(colPins[c], INPUT_PULLUP);
  }
  // Unselect all rows (Hi-Z)
  unselectAllRows();

  // Initialize onboard LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize state
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    for (uint8_t c = 0; c < NUM_COLS; ++c) {
      rawState[r][c] = false;
      lastRaw[r][c] = false;
      debounced[r][c] = false;
      lastChange[r][c] = 0;
    }
  }

  debugPrint("Keyboard matrix initialized (Teensy 4.0, COL2ROW)");
}

void keyboardScan() {
  const uint32_t now = millis();

  // 1) Scan all rows (COL2ROW): select row low, read columns
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    selectRowLow(r);
    if (SELECT_SETTLE_US) delayMicroseconds(SELECT_SETTLE_US);
    for (uint8_t c = 0; c < NUM_COLS; ++c) {
      // Pressed if column reads LOW when this row is selected
      bool pressed = (digitalRead(colPins[c]) == LOW);
      rawState[r][c] = pressed;
    }
  }
  // Restore rows to Hi-Z
  unselectAllRows();

  // 2) Debounce and dispatch events on stable changes
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    for (uint8_t c = 0; c < NUM_COLS; ++c) {
      bool currRaw = rawState[r][c];
      if (currRaw != lastRaw[r][c]) {
        // Raw changed: reset timer
        lastRaw[r][c] = currRaw;
        lastChange[r][c] = now;
      }

      // If debounced differs from current raw and it's been stable long enough, commit
      if (debounced[r][c] != currRaw && (now - lastChange[r][c]) >= DEBOUNCE_MS) {
        debounced[r][c] = currRaw;
        if (currRaw) handleKeyPress(r, c);
        else handleKeyRelease(r, c);
      }
    }
  }

  // 3) Pace scanning
  if (SCAN_INTERVAL_MS) delay(SCAN_INTERVAL_MS);
}

void keyboardReleaseAll() {
  // Release any held base keys by walking the debounced matrix
  for (uint8_t r = 0; r < NUM_ROWS; ++r) {
    for (uint8_t c = 0; c < NUM_COLS; ++c) {
      if (debounced[r][c]) {
        handleKeyRelease(r, c);
        debounced[r][c] = false;
      }
    }
  }
  // Ensure all modifiers are released
  if (refCtrl) { Keyboard.release(MODIFIERKEY_LEFT_CTRL); refCtrl = 0; }
  if (refAlt)  { Keyboard.release(MODIFIERKEY_LEFT_ALT);  refAlt  = 0; }
  if (refShift){ Keyboard.release(MODIFIERKEY_LEFT_SHIFT);refShift= 0; }
  Keyboard.releaseAll();
  pressedCount = 0;
  digitalWrite(LED_PIN, LOW);
}
