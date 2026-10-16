/*
 * Joystick mouse — Waveshare 2" capacitive LCD on a Lonely Binary
 * ESP32-S3 gold board, Lego expansion, one stick.
 *
 * Notes are in README.md.
 * Not a Nomad status screen. Flash only MAC 14:c1:9f:da:3e:28
 * via the CH340, DIO 40 MHz, 16 MB. Native USB is HID after boot.
 *
 * The I2S amp (MAX98357) is removed. Those pins are the joystick.
 * Touch I2C stays on GPIO 15 / 7. Do not Wire.begin() on GPIO 8.
 *
 *   VRx  -> GPIO 4    analog, 3.3 V stick only
 *   VRy  -> GPIO 5
 *   SW   -> GPIO 8    switch to GND, internal pull-up
 *   VCC  -> 3.3 V
 *   GND  -> GND
 *
 * USB mouse plus keyboard macros. Calibration and the six slots
 * are stored in NVS (joymouse). The SD slot is not used.
 * Axes stay swapped. Invert X / Y are the direction controls.
 *
 * Library: Arduino_GFX (moononournation) 1.4.9.
 */

#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>

// Linux drops vertical motion when the stock Arduino mouse report includes
// the extra horizontal-scroll (AC Pan) byte. This report is buttons + X + Y only.
#define MOUSE_LEFT 0x01
#define MOUSE_RIGHT 0x02

static const uint8_t kJoyMouseDesc[] = {
  HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
  HID_USAGE(HID_USAGE_DESKTOP_MOUSE),
  HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(HID_REPORT_ID_MOUSE)
    HID_USAGE(HID_USAGE_DESKTOP_POINTER),
    HID_COLLECTION(HID_COLLECTION_PHYSICAL),
      HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
        HID_USAGE_MIN(1),
        HID_USAGE_MAX(3),
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX(1),
        HID_REPORT_COUNT(3),
        HID_REPORT_SIZE(1),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
        HID_REPORT_COUNT(1),
        HID_REPORT_SIZE(5),
        HID_INPUT(HID_CONSTANT),
      HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        HID_USAGE(HID_USAGE_DESKTOP_X),
        HID_USAGE(HID_USAGE_DESKTOP_Y),
        HID_LOGICAL_MIN(0x81),
        HID_LOGICAL_MAX(0x7f),
        HID_REPORT_COUNT(2),
        HID_REPORT_SIZE(8),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
    HID_COLLECTION_END,
  HID_COLLECTION_END,

  // Separate report so Linux keeps vertical mouse movement.
  // The pointer report stays buttons + X + Y. This one is wheel + sideways pan.
  HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
  HID_USAGE(HID_USAGE_DESKTOP_MOUSE),
  HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(7)
    HID_USAGE(HID_USAGE_DESKTOP_WHEEL),
    HID_LOGICAL_MIN(0x81),
    HID_LOGICAL_MAX(0x7f),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
    0x05, 0x0C,
    0x0A, 0x38, 0x02,
    HID_LOGICAL_MIN(0x81),
    HID_LOGICAL_MAX(0x7f),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(8),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
  HID_COLLECTION_END,

  // Volume up / volume down. Kept off the pointer report.
  0x05, 0x0C,
  0x09, 0x01,
  HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(8)
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    0x09, 0xE9,
    0x09, 0xEA,
    0x09, 0xCD,
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(3),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_REPORT_SIZE(5),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_CONSTANT),
  HID_COLLECTION_END
};

class JoyHidMouse : public USBHIDDevice {
 public:
  JoyHidMouse() {
    static bool once = false;
    if (!once) {
      once = true;
      hid.addDevice(this, sizeof(kJoyMouseDesc));
    }
  }
  uint16_t _onGetDescriptor(uint8_t* dst) override {
    memcpy(dst, kJoyMouseDesc, sizeof(kJoyMouseDesc));
    return sizeof(kJoyMouseDesc);
  }
  void begin() { hid.begin(); }
  void move(int8_t x, int8_t y) {
    uint8_t report[3] = {buttons, (uint8_t)x, (uint8_t)y};
    hid.SendReport(HID_REPORT_ID_MOUSE, report, sizeof(report));
  }
  void press(uint8_t b) {
    buttons = (uint8_t)(buttons | b);
    move(0, 0);
  }
  void scroll(int8_t wheel, int8_t pan) {
    uint8_t report[2] = {(uint8_t)wheel, (uint8_t)pan};
    hid.SendReport(7, report, sizeof(report));
  }
  void volume(uint8_t bits) {
    hid.SendReport(8, &bits, 1);
  }
  void release(uint8_t b) {
    buttons = (uint8_t)(buttons & ~b);
    move(0, 0);
  }

 private:
  USBHID hid;
  uint8_t buttons = 0;
};

#include "cst816d.h"

#define SPI_MISO 42
#define SPI_MOSI 2
#define SPI_SCLK 1
#define LCD_CS 39
#define LCD_DC 41
#define LCD_RST 40
#define LCD_BL 6
#define TP_INT 17
#define TP_RST 16
#define TP_SDA 15
#define TP_SCL 7
#define LCD_HOR_RES 240
#define LCD_VER_RES 320

// Former MAX98357 I2S pins.
#define JOY_X 4
#define JOY_Y 5
#define JOY_SW 8

// WS2812 on GPIO48 (RGB_BUILTIN). It was only an SD status lamp.
// Written off once at boot and left off.

// Same palette as the 3.5 inch Nomad panel.
#define COL_TAN    0xCDAF  // #CAB47E
#define COL_OLIVE  0x39E3  // #3B3E1A
#define COL_RUST   0xCAC7  // #C85A3C
#define COL_ORANGE 0xFC40  // #FF8800
#define COL_PLOT   0xE3C0  // a little darker than the button orange
#define COL_BLACK  0x0000
#define COL_BG     COL_TAN
#define COL_INK    0x8C50  // warm gray: darker than the near-white labels, not black
#define COL_TEXT   COL_INK
#define COL_DIM    COL_RUST
#define COL_BOX    COL_ORANGE
#define COL_DOT    COL_ORANGE
#define COL_OK     COL_ORANGE
#define COL_BTN    COL_OLIVE
#define COL_CLICK  0x9F0E  // light green while the stick is clicked
#define COL_WARN   COL_RUST

#define MOUSE_MAX 16
#define MACRO_SLOTS 6
#define BTN_MAP_VER 3

JoyHidMouse Mouse;
USBHIDKeyboard Keyboard;
Preferences prefs;

enum ScreenId { SCREEN_HOME, SCREEN_SETTINGS, SCREEN_MACROS, SCREEN_KEYS };
ScreenId screen = SCREEN_HOME;
bool savedOnChip = false;

// Catalog order is the cycle order. None is a dead slot.
enum MacroId : uint8_t {
  MACRO_NONE = 0,
  MACRO_DICTATE,
  MACRO_PALETTE,
  MACRO_COPY,
  MACRO_PASTE,
  MACRO_UNDO,
  MACRO_SAVE,
  MACRO_SELECT,
  MACRO_DELETE,
  MACRO_ENTER,
  MACRO_ESC,
  MACRO_ALTTAB,
  MACRO_BACKSPACE,
  MACRO_COUNT
};

static const char* const kMacroName[] = {
  "None",
  "Dictate",
  "Palette",
  "Copy",
  "Paste",
  "Undo",
  "Save",
  "Select all",
  "Delete",
  "Enter",
  "Escape",
  "Alt+Tab",
  "Backspace",
};
// Shown under the name. Dictate is Cursor's voice shortcut, not Ctrl+Space.
static const char* const kMacroHint[] = {
  "",
  "Ctrl+Shift+Space",
  "Ctrl+Shift+P",
  "Ctrl+C",
  "Ctrl+V",
  "Ctrl+Z",
  "Ctrl+S",
  "Ctrl+A",
  "Delete",
  "Enter",
  "Escape",
  "Alt+Tab",
  "Backspace",
};
static_assert(sizeof(kMacroName) / sizeof(kMacroName[0]) == MACRO_COUNT, "macro names");
static_assert(sizeof(kMacroHint) / sizeof(kMacroHint[0]) == MACRO_COUNT, "macro hints");

// Shown on the Macros page. Home still has six tiles; turn one off to free a tile.
#define TOGGLE_COUNT 12
static const uint8_t kToggleId[TOGGLE_COUNT] = {
  MACRO_DICTATE, MACRO_PALETTE, MACRO_COPY, MACRO_PASTE,
  MACRO_SELECT, MACRO_DELETE, MACRO_ENTER, MACRO_BACKSPACE,
  MACRO_ESC, MACRO_UNDO, MACRO_SAVE, MACRO_ALTTAB
};
#define TOGGLE_LIST_Y 32
#define TOGGLE_ROW_H 30
#define TOGGLE_ROW_STEP 34
#define TOGGLE_LIST_H (7 * TOGGLE_ROW_STEP - 4)

static const uint8_t kDefaultSlot[MACRO_SLOTS] = {
  MACRO_DICTATE, MACRO_PALETTE, MACRO_COPY, MACRO_PASTE, MACRO_SELECT, MACRO_DELETE
};
static const char* const kSlotKey[MACRO_SLOTS] = {"b0", "b1", "b2", "b3", "b4", "b5"};

uint8_t slot[MACRO_SLOTS] = {
  MACRO_DICTATE, MACRO_PALETTE, MACRO_COPY, MACRO_PASTE, MACRO_SELECT, MACRO_DELETE
};
uint32_t sentUntil[MACRO_SLOTS] = {};

Arduino_DataBus* bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, SPI_SCLK, SPI_MOSI, SPI_MISO, FSPI);
Arduino_GFX* gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, LCD_HOR_RES, LCD_VER_RES);

struct UiRect {
  int16_t x, y, w, h;
  const char* label;
};

UiRect btnMacro[MACRO_SLOTS] = {
  {8, 92, 108, 56, ""},
  {124, 92, 108, 56, ""},
  {8, 152, 108, 56, ""},
  {124, 152, 108, 56, ""},
  {8, 212, 108, 56, ""},
  {124, 212, 108, 56, ""},
};
UiRect btnSettings = {8, 8, 68, 72, "Settings"};
UiRect btnPlay = {80, 8, 68, 22, "Play"};
UiRect btnVolUp = {80, 33, 68, 22, "Vol+"};
UiRect btnVolDn = {80, 58, 68, 22, "Vol-"};
UiRect btnKeys = {152, 8, 80, 72, "Keys"};
UiRect btnLeft = {4, 280, 70, 36, "Left"};
UiRect btnScroll = {78, 280, 84, 36, "Scroll"};
UiRect btnRight = {166, 280, 70, 36, "Right"};
UiRect btnCenter = {8, 70, 224, 24, "Set center"};
UiRect btnDzMinus = {8, 100, 110, 36, "DZ -"};
UiRect btnDzPlus = {122, 100, 110, 36, "DZ +"};
UiRect btnSpdMinus = {8, 142, 110, 36, "Spd -"};
UiRect btnSpdPlus = {122, 142, 110, 36, "Spd +"};
UiRect btnSlowMinus = {8, 184, 110, 36, "Slow -"};
UiRect btnSlowPlus = {122, 184, 110, 36, "Slow +"};
UiRect btnInvX = {8, 226, 110, 36, "Inv X"};
UiRect btnInvY = {122, 226, 110, 36, "Inv Y"};
UiRect btnMacros = {8, 270, 108, 42, "Macros"};
UiRect btnSetBack = {124, 270, 108, 42, "Back"};
UiRect btnBack = {8, 272, 224, 40, "Back"};

int centerX = 2048;
int centerY = 2048;
int deadzone = 50;
int speedPct = 100;
int selectPct = 25;
int peakX = 280;
int peakY = 280;
bool invX = false;
bool invY = false;
bool haveSavedCal = false;

int rawX = 0;
int rawY = 0;
int8_t moveX = 0;
int8_t moveY = 0;
bool clickDown = false;
bool leftHeld = false;
bool rightHeld = false;
bool scrollHeld = false;
bool scrollLock = false;
uint32_t scrollTapAt = 0;
uint8_t volBits = 0;
uint8_t volPhase = 0;
uint32_t volAt = 0;
bool touchHeld = false;
static int dragFrom = -1;
static int dragOver = -1;
static bool dragging = false;

static void saveCal();
static void saveButtons();
static void drawCalLine();
static void drawSaveLine();
static void drawHome();
static void drawSettings();
static void drawMacros();

static bool pointInRect(int16_t px, int16_t py, const UiRect& r) {
  return px >= r.x && px < (r.x + r.w) && py >= r.y && py < (r.y + r.h);
}

static const UiRect* findTouch(int16_t x, int16_t y, const UiRect* const* rs, int n) {
  for (int i = 0; i < n; i++) {
    if (pointInRect(x, y, *rs[i])) return rs[i];
  }
  return nullptr;
}

static void saveCal();
static void saveButtons();

static void loadCal() {
  prefs.begin("joymouse", true);
  haveSavedCal = prefs.getBool("ready", false);
  uint8_t calv = 0;
  if (haveSavedCal) {
    centerX = prefs.getUShort("cx", 2048);
    centerY = prefs.getUShort("cy", 2048);
    deadzone = prefs.getUShort("dz", 50);
    invX = prefs.getBool("invx", false);
    invY = prefs.getBool("invy", false);
    calv = prefs.getUChar("calv", 0);
  }
  bool hasBtn = prefs.isKey("btnv");
  uint8_t btnv = prefs.getUChar("btnv", 0);
  uint8_t loaded[MACRO_SLOTS];
  for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
    loaded[i] = prefs.getUChar(kSlotKey[i], kDefaultSlot[i]);
  }
  selectPct = prefs.getUChar("sel", 25);
  speedPct = prefs.getUChar("spd", 100);
  prefs.end();

  // v1 saved a 220-count deadzone that ate a short vertical throw.
  // Only rewrite a calibration that is already on the chip.
  if (haveSavedCal && calv < 2) {
    deadzone = 50;
    saveCal();
  }
  if (deadzone < 20) deadzone = 20;
  if (deadzone > 400) deadzone = 400;
  if (selectPct < 10) selectPct = 10;
  if (selectPct > 100) selectPct = 100;
  if (speedPct < 20) speedPct = 20;
  if (speedPct > 200) speedPct = 200;

  if (!hasBtn || btnv < BTN_MAP_VER) {
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) slot[i] = kDefaultSlot[i];
    saveButtons();
  } else {
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
      slot[i] = loaded[i] < MACRO_COUNT ? loaded[i] : (uint8_t)MACRO_NONE;
    }
  }
}

static void saveCal() {
  prefs.begin("joymouse", false);
  prefs.putBool("ready", true);
  prefs.putUShort("cx", (uint16_t)centerX);
  prefs.putUShort("cy", (uint16_t)centerY);
  prefs.putUShort("dz", (uint16_t)deadzone);
  prefs.putBool("invx", invX);
  prefs.putBool("invy", invY);
  prefs.putUChar("sel", (uint8_t)selectPct);
  prefs.putUChar("spd", (uint8_t)speedPct);
  prefs.putUChar("calv", 2);
  prefs.end();
  savedOnChip = true;
  if (screen == SCREEN_SETTINGS) drawSaveLine();
}

static void saveButtons() {
  prefs.begin("joymouse", false);
  for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
    prefs.putUChar(kSlotKey[i], slot[i]);
  }
  prefs.putUChar("btnv", BTN_MAP_VER);
  prefs.end();
  savedOnChip = true;
}

// press() never transmits a modifier by itself (it drops the report when the
// key code is 0). Shortcuts need the modifiers down first, then the key.
// HID usage codes, not the ASCII Keyboard.press() values.
static void tapChord(uint8_t mods, uint8_t key) {
  if (mods & 0x01) Keyboard.pressRaw(0xE0);  // left ctrl
  if (mods & 0x02) Keyboard.pressRaw(0xE1);  // left shift
  if (mods & 0x04) Keyboard.pressRaw(0xE2);  // left alt
  delay(30);
  if (key) Keyboard.pressRaw(key);
  delay(90);
  Keyboard.releaseAll();
}

static void sendMacro(uint8_t id) {
  switch (id) {
    case MACRO_DICTATE: tapChord(0x03, 0x2C); break;  // Ctrl+Shift+Space
    case MACRO_PALETTE: tapChord(0x03, 0x13); break;  // Ctrl+Shift+P
    case MACRO_COPY:    tapChord(0x01, 0x06); break;  // Ctrl+C
    case MACRO_PASTE:   tapChord(0x01, 0x19); break;  // Ctrl+V
    case MACRO_UNDO:    tapChord(0x01, 0x1D); break;  // Ctrl+Z
    case MACRO_SAVE:    tapChord(0x01, 0x16); break;  // Ctrl+S
    case MACRO_SELECT:  tapChord(0x01, 0x04); break;  // Ctrl+A
    case MACRO_DELETE:  tapChord(0x00, 0x4C); break;  // Delete
    case MACRO_BACKSPACE: tapChord(0x00, 0x2A); break;  // Backspace
    case MACRO_ENTER:   tapChord(0x00, 0x28); break;
    case MACRO_ESC:     tapChord(0x00, 0x29); break;
    case MACRO_ALTTAB:  tapChord(0x04, 0x2B); break;
    default:
      return;
  }
}

static int readAxis(int pin) {
  analogRead(pin);
  delayMicroseconds(80);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    sum += analogRead(pin);
  }
  return (int)(sum / 8);
}

static void captureCenter() {
  uint32_t sx = 0;
  uint32_t sy = 0;
  for (uint8_t i = 0; i < 24; i++) {
    sx += analogRead(JOY_X);
    sy += analogRead(JOY_Y);
    delay(2);
  }
  centerX = (int)(sx / 24);
  centerY = (int)(sy / 24);
  saveCal();
}

// Scale against the biggest throw seen, so a short vertical pot still reaches full speed.
static int8_t scaleAxis(int raw, int center, bool invert, int& peak) {
  int delta = raw - center;
  if (invert) delta = -delta;
  int ad = abs(delta);
  if (ad > peak) peak = ad;
  if (ad < deadzone) return 0;
  int room = peak - deadzone;
  if (room < 60) room = 60;
  int past = ad - deadzone;
  if (past > room) past = room;
  int cap = (MOUSE_MAX * speedPct) / 100;
  if (cap < 1) cap = 1;
  if (cap > 40) cap = 40;
  int mag = (past * cap) / room;
  if (mag < 1) mag = 1;
  if (speedPct >= 100 && mag < 2) mag = 2;
  if (mag > cap) mag = cap;
  return (int8_t)(delta > 0 ? mag : -mag);
}

static void syncLeftButton() {
  if (clickDown || leftHeld) Mouse.press(MOUSE_LEFT);
  else Mouse.release(MOUSE_LEFT);
}

static void readStick() {
  rawX = readAxis(JOY_X);
  rawY = readAxis(JOY_Y);
  // Axes stay swapped. The settings button for this is gone.
  int8_t ax = scaleAxis(rawX, centerX, invX, peakX);
  int8_t ay = scaleAxis(rawY, centerY, invY, peakY);
  moveX = ay;
  moveY = ax;
  // Keys turns the picture clockwise. Rotate the pointer with it so
  // the stick's up matches the top of the letters. Mouse mode stays put.
  if (screen == SCREEN_KEYS) {
    int8_t turned = moveX;
    moveX = moveY;
    moveY = (int8_t)(-turned);
  }

  bool down = digitalRead(JOY_SW) == LOW;
  if (down != clickDown) {
    clickDown = down;
    syncLeftButton();
  }

  // The stick click is a normal-speed left click. The on-screen Left
  // button is the slow drag. Scroll runs while its button is held, or
  // after a double-tap locks it, unless a click button is holding the pointer.
  bool dragSlow = leftHeld;
  static int accX = 0;
  static int accY = 0;
  static int scrAccX = 0;
  static int scrAccY = 0;
  if ((scrollHeld || scrollLock) && !clickDown && !dragSlow && !rightHeld) {
    accX = 0;
    accY = 0;
    scrAccX += moveX;
    scrAccY += moveY;
    int pan = 0;
    int wheel = 0;
    const int step = 18;
    if (scrAccX >= step) { pan = 1; scrAccX = 0; }
    else if (scrAccX <= -step) { pan = -1; scrAccX = 0; }
    // Pointer +Y is down. Wheel + is scroll up, so the stick's up direction scrolls up.
    if (scrAccY >= step) { wheel = -1; scrAccY = 0; }
    else if (scrAccY <= -step) { wheel = 1; scrAccY = 0; }
    if (pan != 0 || wheel != 0) Mouse.scroll((int8_t)wheel, (int8_t)pan);
    return;
  }
  scrAccX = 0;
  scrAccY = 0;

  int outX = moveX;
  int outY = moveY;
  if (dragSlow) {
    int pct = selectPct;
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    accX += moveX * pct;
    accY += moveY * pct;
    outX = accX / 100;
    outY = accY / 100;
    accX -= outX * 100;
    accY -= outY * 100;
  } else {
    accX = 0;
    accY = 0;
  }
  if (outX > 127) outX = 127;
  if (outX < -127) outX = -127;
  if (outY > 127) outY = 127;
  if (outY < -127) outY = -127;
  if (outX != 0 || outY != 0) {
    Mouse.move((int8_t)outX, (int8_t)outY);
  }
}

// 1px warm-gray frame just outside the button. Same as
// drawRoundRect(r.x-1, r.y-1, r.w+2, r.h+2, 4, COL_INK) when that
// outset stays on the panel. A side that would leave the 240x320
// panel sits on the button's outer pixel instead (the Scroll key).
static void drawButtonFrame(const UiRect& r) {
  int16_t left = (int16_t)(r.x - 1);
  int16_t top = (int16_t)(r.y - 1);
  int16_t right = (int16_t)(r.x + r.w);
  int16_t bottom = (int16_t)(r.y + r.h);
  if (left < 0) left = r.x;
  if (top < 0) top = r.y;
  if (right >= gfx->width()) right = (int16_t)(r.x + r.w - 1);
  if (bottom >= gfx->height()) bottom = (int16_t)(r.y + r.h - 1);
  if (right <= left || bottom <= top) return;
  gfx->drawRoundRect(left, top, (int16_t)(right - left + 1),
                     (int16_t)(bottom - top + 1), 4, COL_INK);
}

static void drawCenteredLabel(const UiRect& r, const char* label, uint16_t color) {
  size_t n = strlen(label);
  const uint8_t sz = 2;
  const int16_t cw = 12;
  gfx->setTextSize(sz);
  gfx->setTextColor(color);
  int16_t tw = (int16_t)n * cw;
  int16_t th = (int16_t)sz * 8;
  gfx->setCursor(r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2);
  gfx->print(label);
}

static void drawButton(const UiRect& r, bool on) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, on ? COL_ORANGE : COL_BTN);
  drawCenteredLabel(r, r.label, COL_INK);
  drawButtonFrame(r);
}

static void drawMacroButton(uint8_t i) {
  const UiRect& r = btnMacro[i];
  bool sent = sentUntil[i] != 0 && millis() < sentUntil[i];
  bool moving = dragging && (int)i == dragFrom;
  bool drop = dragging && (int)i == dragOver && (int)i != dragFrom;
  uint8_t id = slot[i] < MACRO_COUNT ? slot[i] : (uint8_t)MACRO_NONE;
  bool none = id == MACRO_NONE;
  if (none) {
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, COL_BG);
    if (dragging) {
      gfx->drawRoundRect(r.x, r.y, r.w, r.h, 4, drop ? COL_RUST : COL_ORANGE);
    }
    return;
  }
  uint16_t fill = moving ? COL_ORANGE : drop ? COL_ORANGE : sent ? COL_RUST : COL_BTN;
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, fill);
  drawCenteredLabel(r, moving ? "Move" : sent ? "Sent" : kMacroName[id], COL_INK);
  drawButtonFrame(r);
}

static void drawVolButton(const UiRect& r, bool held) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, held ? COL_ORANGE : COL_BTN);
  drawCenteredLabel(r, r.label, COL_INK);
  drawButtonFrame(r);
}

static void drawVolButtons() {
  drawVolButton(btnVolUp, volBits == 0x01);
  drawVolButton(btnPlay, volBits == 0x04);
  drawVolButton(btnVolDn, volBits == 0x02);
}

static void serviceVolume() {
  if (!volBits) {
    if (volPhase != 0) {
      Mouse.volume(0);
      volPhase = 0;
    }
    return;
  }
  uint32_t now = millis();
  // Play/Pause toggles on each press, so it fires once instead of repeating.
  if (volBits == 0x04) {
    if (volPhase == 0) {
      Mouse.volume(0x04);
      volPhase = 1;
      volAt = now;
    } else if (volPhase == 1 && now - volAt >= 40) {
      Mouse.volume(0);
      volPhase = 3;
    }
    return;
  }
  if (volPhase == 0 || (volPhase == 2 && now - volAt >= 140)) {
    Mouse.volume(volBits);
    volPhase = 1;
    volAt = now;
  } else if (volPhase == 1 && now - volAt >= 40) {
    Mouse.volume(0);
    volPhase = 2;
    volAt = now;
  }
}

// 2px warm-gray edge on the 240x320 panel. Drawn after a full screen so it
// sits on top of the bottom row.
static void drawPanelFrame() {
  int16_t w = gfx->width();
  int16_t h = gfx->height();
  gfx->drawRect(0, 0, w, h, COL_INK);
  gfx->drawRect(1, 1, w - 2, h - 2, COL_INK);
}

static void drawClickButton(const UiRect& r, bool held) {
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, held ? COL_ORANGE : COL_BTN);
  drawCenteredLabel(r, r.label, COL_INK);
  drawButtonFrame(r);
}

static void drawScrollButton() {
  bool hot = scrollHeld || scrollLock;
  const char* label = scrollLock ? "Locked" : "Scroll";
  gfx->fillRoundRect(btnScroll.x, btnScroll.y, btnScroll.w, btnScroll.h, 4,
                     hot ? COL_ORANGE : COL_BTN);
  drawCenteredLabel(btnScroll, label, COL_INK);
  drawButtonFrame(btnScroll);
}

static void drawSettingsButton() {
  const UiRect& r = btnSettings;
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, COL_BTN);
  int cx = r.x + r.w / 2;
  int cy = r.y + r.h / 2;
  static const int8_t tooth[8][2] = {
    {0, -16}, {11, -11}, {16, 0}, {11, 11},
    {0, 16}, {-11, 11}, {-16, 0}, {-11, -11}
  };
  for (uint8_t i = 0; i < 8; i++) {
    gfx->fillCircle(cx + tooth[i][0], cy + tooth[i][1], 4, COL_INK);
  }
  gfx->fillCircle(cx, cy, 11, COL_INK);
  gfx->fillCircle(cx, cy, 5, COL_BTN);
  drawButtonFrame(r);
}

static void drawHome() {
  gfx->fillScreen(COL_BG);

  for (uint8_t i = 0; i < MACRO_SLOTS; i++) drawMacroButton(i);
  drawSettingsButton();
  drawButton(btnKeys, false);
  drawVolButtons();
  drawClickButton(btnLeft, leftHeld);
  drawScrollButton();
  drawClickButton(btnRight, rightHeld);
  drawPanelFrame();
  drawButtonFrame(btnSettings);
  drawButtonFrame(btnKeys);
  drawButtonFrame(btnVolUp);
  drawButtonFrame(btnPlay);
  drawButtonFrame(btnVolDn);
  for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
    uint8_t id = slot[i] < MACRO_COUNT ? slot[i] : (uint8_t)MACRO_NONE;
    if (id != MACRO_NONE) drawButtonFrame(btnMacro[i]);
  }
  drawButtonFrame(btnLeft);
  drawButtonFrame(btnScroll);
  drawButtonFrame(btnRight);
}

// Olive plate behind the four settings readouts. Bottom is y=66, four pixels
// above Set center (y=70), so the bar does not cover that button.
#define STATUS_BAR_X 4
#define STATUS_BAR_Y 2
#define STATUS_BAR_W 232
#define STATUS_BAR_H 64

static void drawStatusBar() {
  gfx->fillRect(STATUS_BAR_X, STATUS_BAR_Y, STATUS_BAR_W, STATUS_BAR_H, COL_OLIVE);
}

static void drawSaveLine() {
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT, COL_OLIVE);
  gfx->setCursor(8, 4);
  gfx->print(savedOnChip ? "Saved on chip" : "Not saved yet");
}

static void drawSettings() {
  gfx->fillScreen(COL_BG);
  drawStatusBar();
  drawSaveLine();
  drawCalLine();

  drawButton(btnCenter, false);
  drawButton(btnDzMinus, false);
  drawButton(btnDzPlus, false);
  drawButton(btnSpdMinus, false);
  drawButton(btnSpdPlus, false);
  drawButton(btnSlowMinus, false);
  drawButton(btnSlowPlus, false);
  drawButton(btnInvX, invX);
  drawButton(btnInvY, invY);
  drawButton(btnMacros, false);
  drawButton(btnSetBack, false);
  drawPanelFrame();
  drawButtonFrame(btnCenter);
  drawButtonFrame(btnDzMinus);
  drawButtonFrame(btnDzPlus);
  drawButtonFrame(btnSpdMinus);
  drawButtonFrame(btnSpdPlus);
  drawButtonFrame(btnSlowMinus);
  drawButtonFrame(btnSlowPlus);
  drawButtonFrame(btnInvX);
  drawButtonFrame(btnInvY);
  drawButtonFrame(btnMacros);
  drawButtonFrame(btnSetBack);
}

static int findSlot(uint8_t id) {
  for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
    if (slot[i] == id) return (int)i;
  }
  return -1;
}

static void setMacroEnabled(uint8_t id, bool on) {
  int at = findSlot(id);
  if (on) {
    if (at >= 0) return;
    int used = 0;
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
      if (slot[i] != MACRO_NONE && slot[i] < MACRO_COUNT) used++;
    }
    if (used >= MACRO_SLOTS) return;
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
      if (slot[i] == MACRO_NONE) {
        slot[i] = id;
        break;
      }
    }
  } else if (at >= 0) {
    slot[at] = MACRO_NONE;
  } else {
    return;
  }
  saveButtons();
}

static int macroScroll = 0;

static int toggleMaxScroll() {
  int content = (TOGGLE_COUNT - 1) * TOGGLE_ROW_STEP + TOGGLE_ROW_H;
  int extra = content - TOGGLE_LIST_H;
  return extra > 0 ? extra : 0;
}

static int toggleAt(int16_t x, int16_t y) {
  if (x < 8 || x >= 232) return -1;
  if (y < TOGGLE_LIST_Y || y >= TOGGLE_LIST_Y + TOGGLE_LIST_H) return -1;
  int rel = y - TOGGLE_LIST_Y + macroScroll;
  int i = rel / TOGGLE_ROW_STEP;
  if (i < 0 || i >= TOGGLE_COUNT) return -1;
  if ((rel % TOGGLE_ROW_STEP) >= TOGGLE_ROW_H) return -1;
  return i;
}

static void drawMacroToggle(uint8_t i) {
  int y = TOGGLE_LIST_Y + (int)i * TOGGLE_ROW_STEP - macroScroll;
  if (y < TOGGLE_LIST_Y || y + TOGGLE_ROW_H > TOGGLE_LIST_Y + TOGGLE_LIST_H) return;
  UiRect r = {8, (int16_t)y, 224, TOGGLE_ROW_H, ""};
  uint8_t id = kToggleId[i];
  bool on = findSlot(id) >= 0;
  if (on) {
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, COL_BTN);
  } else {
    gfx->fillRoundRect(r.x, r.y, r.w, r.h, 4, COL_BG);
    gfx->drawRoundRect(r.x, r.y, r.w, r.h, 4, COL_OLIVE);
  }
  gfx->setTextSize(2);
  gfx->setTextColor(COL_INK, on ? COL_BTN : COL_BG);
  gfx->setCursor(r.x + 10, r.y + (r.h - 16) / 2);
  gfx->print(kMacroName[id]);
  const char* state = on ? "ON" : "OFF";
  gfx->setTextColor(COL_INK, on ? COL_BTN : COL_BG);
  gfx->setCursor(r.x + r.w - 10 - (int16_t)strlen(state) * 12, r.y + (r.h - 16) / 2);
  gfx->print(state);
  drawButtonFrame(r);
}

static bool macroDirty = false;
static uint32_t macroPaintAt = 0;

static void drawMacroList() {
  // Leave the 2px side frame. A full-width clear would wipe it on each scroll.
  // One pixel above and below the list clears row frames that sit outside the rows.
  gfx->fillRect(2, TOGGLE_LIST_Y - 1, LCD_HOR_RES - 4, TOGGLE_LIST_H + 2, COL_BG);
  for (uint8_t i = 0; i < TOGGLE_COUNT; i++) drawMacroToggle(i);
}

static void drawMacros() {
  gfx->fillScreen(COL_BG);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(8, 8);
  gfx->print("Macros");
  for (uint8_t i = 0; i < TOGGLE_COUNT; i++) drawMacroToggle(i);
  drawButton(btnBack, false);
  drawPanelFrame();
  for (uint8_t i = 0; i < TOGGLE_COUNT; i++) {
    int y = TOGGLE_LIST_Y + (int)i * TOGGLE_ROW_STEP - macroScroll;
    if (y < TOGGLE_LIST_Y || y + TOGGLE_ROW_H > TOGGLE_LIST_Y + TOGGLE_LIST_H) continue;
    UiRect row = {8, (int16_t)y, 224, TOGGLE_ROW_H, ""};
    drawButtonFrame(row);
  }
  drawButtonFrame(btnBack);
}

static void drawCalLine() {
  char deadBuf[12];
  char spdBuf[12];
  char slowBuf[12];
  snprintf(deadBuf, sizeof(deadBuf), "Dead %3d", deadzone);
  snprintf(spdBuf, sizeof(spdBuf), "Spd %3d%%", speedPct);
  snprintf(slowBuf, sizeof(slowBuf), "Slow %3d%%", selectPct);
  gfx->setTextSize(2);
  gfx->setTextColor(COL_TEXT, COL_OLIVE);
  gfx->setCursor(8, 26);
  gfx->print(deadBuf);
  gfx->setCursor(128, 26);
  gfx->print(spdBuf);
  gfx->setCursor(8, 46);
  gfx->print(slowBuf);
}

static void drawLive() {
  if (screen != SCREEN_SETTINGS) return;
  drawCalLine();
}

// Landscape keyboard. Rotation 1 turns the picture clockwise, so the
// right edge of the board is the top of the keys. Touch is still the
// portrait reading from the glass, mapped into that landscape.
enum { KB_TYPE = 0, KB_SHIFT = 1, KB_LAYER = 2, KB_MOUSE = 3, KB_DICT = 4, KB_MAX = 48 };

struct KbKey {
  int16_t x, y, w, h;
  const char* label;
  uint8_t code;
  uint8_t mods;
  uint8_t act;
};

static KbKey kb[KB_MAX];
static int kbN = 0;
static bool kbShift = false;
static bool kbNum = false;
static int kbHeld = -1;
static bool waitRelease = false;

static void kbAdd(int x, int y, int w, int h, const char* label, uint8_t code, uint8_t mods, uint8_t act) {
  if (kbN >= KB_MAX) return;
  kb[kbN++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, label, code, mods, act};
}

static void drawKbKey(int i, bool hot) {
  const KbKey& k = kb[i];
  bool armed = (k.act == KB_SHIFT && kbShift);
  uint16_t fill = (hot || armed) ? COL_ORANGE : COL_BTN;
  const char* label = k.label;
  uint8_t sz = strlen(label) == 1 ? 3 : 2;
  int16_t cw = sz == 3 ? 18 : 12;
  int16_t tw = (int16_t)strlen(label) * cw;
  int16_t th = (int16_t)sz * 8;
  gfx->fillRoundRect(k.x, k.y, k.w, k.h, 4, fill);
  gfx->setTextSize(sz);
  gfx->setTextColor(COL_TAN, fill);
  gfx->setCursor(k.x + (k.w - tw) / 2, k.y + (k.h - th) / 2);
  gfx->print(label);
  UiRect r = {k.x, k.y, k.w, k.h, label};
  drawButtonFrame(r);
}

static int kbRowY(int row) {
  return 4 + row * (54 + 4);
}

static void kbBottom(int y) {
  kbAdd(2, y, 44, 54, kbNum ? "ABC" : "123", 0, 0, KB_LAYER);
  kbAdd(49, y, 56, 54, "Dict", 0, 0, KB_DICT);
  kbAdd(108, y, 68, 54, "Space", 0x2C, 0, KB_TYPE);
  kbAdd(179, y, 68, 54, "Enter", 0x28, 0, KB_TYPE);
  kbAdd(250, y, 68, 54, "Mouse", 0, 0, KB_MOUSE);
}

static void layoutKeyboard() {
  kbN = 0;
  const int y0 = kbRowY(0);
  const int y1 = kbRowY(1);
  const int y2 = kbRowY(2);
  const int y3 = kbRowY(3);
  if (!kbNum) {
    static const char* const lab0[] = {"Q","W","E","R","T","Y","U","I","O","P"};
    static const uint8_t cod0[] = {0x14,0x1A,0x08,0x15,0x17,0x1C,0x18,0x0C,0x12,0x13};
    static const char* const lab1[] = {"A","S","D","F","G","H","J","K","L"};
    static const uint8_t cod1[] = {0x04,0x16,0x07,0x09,0x0A,0x0B,0x0D,0x0E,0x0F};
    static const char* const lab2[] = {"Z","X","C","V","B","N","M"};
    static const uint8_t cod2[] = {0x1D,0x1B,0x06,0x19,0x05,0x11,0x10};
    for (int i = 0; i < 10; i++) kbAdd(2 + i * 32, y0, 30, 54, lab0[i], cod0[i], 0, KB_TYPE);
    for (int i = 0; i < 9; i++) kbAdd(17 + i * 32, y1, 30, 54, lab1[i], cod1[i], 0, KB_TYPE);
    kbAdd(2, y2, 46, 54, "Sft", 0, 0, KB_SHIFT);
    for (int i = 0; i < 7; i++) kbAdd(50 + i * 32, y2, 30, 54, lab2[i], cod2[i], 0, KB_TYPE);
    kbAdd(274, y2, 44, 54, "BS", 0x2A, 0, KB_TYPE);
  } else {
    static const char* const lab0[] = {"1","2","3","4","5","6","7","8","9","0"};
    static const uint8_t cod0[] = {0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27};
    static const char* const sym1[] = {"!","@","#","$","%","^","&","*","(",")"};
    static const char* const sym2[] = {"-","_","+","=","[","]","{","}","|","\\"};
    static const uint8_t cod2[] = {0x2D,0x2D,0x2E,0x2E,0x2F,0x30,0x2F,0x30,0x31,0x31};
    static const uint8_t mod2[] = {0,0x02,0x02,0,0,0,0x02,0x02,0x02,0};
    static const char* const sym3[] = {"/","?", "`","~","'","\"",",",".",":"};
    static const uint8_t cod3[] = {0x38,0x38,0x35,0x35,0x34,0x34,0x36,0x37,0x33};
    static const uint8_t mod3[] = {0,0x02,0,0x02,0,0x02,0,0,0x02};
    const int r1 = 4;
    const int r2 = 47;
    const int r3 = 90;
    const int r4 = 133;
    for (int i = 0; i < 10; i++) kbAdd(2 + i * 32, r1, 30, 40, lab0[i], cod0[i], 0, KB_TYPE);
    for (int i = 0; i < 10; i++) kbAdd(2 + i * 32, r2, 30, 40, sym1[i], cod0[i], 0x02, KB_TYPE);
    for (int i = 0; i < 10; i++) kbAdd(2 + i * 32, r3, 30, 40, sym2[i], cod2[i], mod2[i], KB_TYPE);
    for (int i = 0; i < 9; i++) kbAdd(2 + i * 32, r4, 30, 40, sym3[i], cod3[i], mod3[i], KB_TYPE);
    kbAdd(2 + 9 * 32, r4, 30, 40, "BS", 0x2A, 0, KB_TYPE);
  }
  kbBottom(y3);
}

static void drawKeyboard() {
  layoutKeyboard();
  gfx->fillScreen(COL_BG);
  for (int i = 0; i < kbN; i++) drawKbKey(i, false);
  drawPanelFrame();
  for (int i = 0; i < kbN; i++) {
    UiRect r = {kb[i].x, kb[i].y, kb[i].w, kb[i].h, ""};
    drawButtonFrame(r);
  }
}

static int keyAt(int16_t x, int16_t y) {
  for (int i = kbN - 1; i >= 0; i--) {
    if (x >= kb[i].x && x < kb[i].x + kb[i].w && y >= kb[i].y && y < kb[i].y + kb[i].h) return i;
  }
  return -1;
}

static void closeKeyboard() {
  kbHeld = -1;
  kbShift = false;
  screen = SCREEN_HOME;
  gfx->setRotation(0);
  waitRelease = true;
  drawHome();
}

static void openKeyboard() {
  kbNum = false;
  kbShift = false;
  kbHeld = -1;
  screen = SCREEN_KEYS;
  gfx->setRotation(1);
  drawKeyboard();
}

static void handleKeysTouch(bool down, int16_t x, int16_t y) {
  if (!down) {
    if (kbHeld >= 0 && screen == SCREEN_KEYS) {
      int was = kbHeld;
      kbHeld = -1;
      drawKbKey(was, false);
    }
    return;
  }
  if (kbHeld >= 0 || screen != SCREEN_KEYS) return;
  int hit = keyAt(x, y);
  if (hit < 0) return;
  kbHeld = hit;
  KbKey& k = kb[hit];
  if (k.act == KB_SHIFT) {
    kbShift = !kbShift;
    drawKbKey(hit, true);
    return;
  }
  if (k.act == KB_LAYER) {
    kbNum = !kbNum;
    kbShift = false;
    kbHeld = -1;
    drawKeyboard();
    for (int i = 0; i < kbN; i++) {
      if (kb[i].act == KB_LAYER) {
        kbHeld = i;
        drawKbKey(i, true);
        break;
      }
    }
    return;
  }
  if (k.act == KB_MOUSE) {
    closeKeyboard();
    return;
  }
  if (k.act == KB_DICT) {
    drawKbKey(hit, true);
    tapChord(0x03, 0x2C);
    return;
  }
  uint8_t mods = k.mods;
  if (kbShift) mods = (uint8_t)(mods | 0x02);
  bool clearShift = kbShift;
  kbShift = false;
  drawKbKey(hit, true);
  if (clearShift) {
    for (int i = 0; i < kbN; i++) {
      if (kb[i].act == KB_SHIFT) drawKbKey(i, false);
    }
  }
  tapChord(mods, k.code);
}

#define SCROLL_HIT (MACRO_SLOTS + 1)
#define VOL_UP_HIT (MACRO_SLOTS + 2)
#define VOL_DN_HIT (MACRO_SLOTS + 3)
#define PLAY_HIT (MACRO_SLOTS + 4)
#define LEFT_HIT (MACRO_SLOTS + 5)
#define RIGHT_HIT (MACRO_SLOTS + 6)
#define KEYS_HIT (MACRO_SLOTS + 7)

static int homeHitAt(int16_t x, int16_t y) {
  if (pointInRect(x, y, btnVolUp)) return VOL_UP_HIT;
  if (pointInRect(x, y, btnPlay)) return PLAY_HIT;
  if (pointInRect(x, y, btnVolDn)) return VOL_DN_HIT;
  if (pointInRect(x, y, btnLeft)) return LEFT_HIT;
  if (pointInRect(x, y, btnScroll)) return SCROLL_HIT;
  if (pointInRect(x, y, btnRight)) return RIGHT_HIT;
  if (pointInRect(x, y, btnKeys)) return KEYS_HIT;
  for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
    if (pointInRect(x, y, btnMacro[i])) return (int)i;
  }
  if (pointInRect(x, y, btnSettings)) return (int)MACRO_SLOTS;
  return -1;
}

static bool fingerDown = false;
static uint32_t fingerAt = 0;
static uint32_t fingerLost = 0;
static int gestureStart = -1;

static void endHomeGesture() {
  bool wasDrag = dragging;
  int start = gestureStart;
  int from = dragFrom;
  int over = dragOver;
  dragging = false;
  dragFrom = -1;
  dragOver = -1;
  gestureStart = -1;
  bool wasVol = volBits != 0 || start == VOL_UP_HIT || start == VOL_DN_HIT || start == PLAY_HIT;
  volBits = 0;
  bool wasScroll = scrollHeld || start == SCROLL_HIT;
  scrollHeld = false;
  bool wasLeft = leftHeld || start == LEFT_HIT;
  bool wasRight = rightHeld || start == RIGHT_HIT;
  leftHeld = false;
  rightHeld = false;
  if (wasLeft) syncLeftButton();
  if (wasRight) Mouse.release(MOUSE_RIGHT);
  if (wasDrag) {
    if (from >= 0 && over >= 0 && over != from) {
      uint8_t tmp = slot[from];
      slot[from] = slot[over];
      slot[over] = tmp;
      saveButtons();
    }
    drawHome();
    return;
  }
  if (wasVol) {
    drawVolButtons();
    return;
  }
  if (wasScroll) {
    uint32_t heldMs = millis() - fingerAt;
    if (heldMs < 280) {
      if (scrollLock) {
        scrollLock = false;
        scrollTapAt = 0;
      } else if (scrollTapAt != 0 && millis() - scrollTapAt <= 450) {
        scrollLock = true;
        scrollTapAt = 0;
      } else {
        scrollTapAt = millis();
      }
    } else {
      scrollTapAt = 0;
    }
    drawScrollButton();
    return;
  }
  if (wasLeft || wasRight) {
    if (wasLeft) drawClickButton(btnLeft, false);
    if (wasRight) drawClickButton(btnRight, false);
    return;
  }
  if (start == (int)MACRO_SLOTS) {
    screen = SCREEN_SETTINGS;
    drawSettings();
    return;
  }
  if (start == KEYS_HIT) {
    openKeyboard();
    return;
  }
  if (start < 0 || start >= (int)MACRO_SLOTS) return;
  if (slot[start] == MACRO_NONE || slot[start] >= MACRO_COUNT) return;
  sendMacro(slot[start]);
  sentUntil[start] = millis() + 350;
  drawMacroButton((uint8_t)start);
}

static void handleHomeTouch(bool down, int16_t x, int16_t y) {
  if (!down) {
    if (!fingerDown) return;
    if (fingerLost == 0) fingerLost = millis();
    if (millis() - fingerLost < 60) return;
    endHomeGesture();
    fingerDown = false;
    fingerLost = 0;
    return;
  }
  fingerLost = 0;
  if (!fingerDown) {
    fingerDown = true;
    fingerAt = millis();
    gestureStart = homeHitAt(x, y);
    dragging = false;
    dragFrom = -1;
    dragOver = -1;
    if (gestureStart == SCROLL_HIT) {
      scrollHeld = true;
      drawScrollButton();
    } else if (gestureStart == LEFT_HIT) {
      leftHeld = true;
      syncLeftButton();
      drawClickButton(btnLeft, true);
    } else if (gestureStart == RIGHT_HIT) {
      rightHeld = true;
      Mouse.press(MOUSE_RIGHT);
      drawClickButton(btnRight, true);
    } else if (gestureStart == VOL_UP_HIT || gestureStart == VOL_DN_HIT || gestureStart == PLAY_HIT) {
      volBits = gestureStart == VOL_UP_HIT ? 0x01 : gestureStart == VOL_DN_HIT ? 0x02 : 0x04;
      volPhase = 0;
      drawVolButtons();
    }
    return;
  }
  if (volBits == 0x04) return;
  if (volBits) {
    int hit = homeHitAt(x, y);
    uint8_t want = 0;
    if (hit == VOL_UP_HIT) want = 0x01;
    else if (hit == VOL_DN_HIT) want = 0x02;
    if (want != volBits) {
      volBits = want;
      volPhase = 0;
      drawVolButtons();
    }
    return;
  }
  if (!dragging && gestureStart >= 0 && gestureStart < (int)MACRO_SLOTS &&
      slot[gestureStart] != MACRO_NONE && slot[gestureStart] < MACRO_COUNT &&
      millis() - fingerAt >= 420) {
    dragging = true;
    dragFrom = gestureStart;
    dragOver = -1;
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) drawMacroButton(i);
  }
  if (!dragging) return;
  int over = homeHitAt(x, y);
  if (over < 0 || over >= (int)MACRO_SLOTS || over == dragFrom) over = -1;
  if (over == dragOver) return;
  int prev = dragOver;
  dragOver = over;
  if (prev >= 0) drawMacroButton((uint8_t)prev);
  if (dragOver >= 0) drawMacroButton((uint8_t)dragOver);
  if (dragFrom >= 0) drawMacroButton((uint8_t)dragFrom);
}

static int16_t macroX = 0;
static int16_t macroY = 0;
static int macroY0 = 0;
static int macroScroll0 = 0;
static bool macroMoved = false;

static void handleMacrosTouch(bool down, int16_t x, int16_t y) {
  if (!down) {
    if (!fingerDown) return;
    if (fingerLost == 0) fingerLost = millis();
    if (millis() - fingerLost < 60) return;
    fingerDown = false;
    fingerLost = 0;
    if (macroMoved) return;
    if (pointInRect(macroX, macroY, btnBack)) {
      screen = SCREEN_SETTINGS;
      drawSettings();
      return;
    }
    int i = toggleAt(macroX, macroY);
    if (i < 0) return;
    uint8_t id = kToggleId[i];
    setMacroEnabled(id, findSlot(id) < 0);
    drawMacros();
    return;
  }
  fingerLost = 0;
  macroX = x;
  macroY = y;
  if (!fingerDown) {
    fingerDown = true;
    macroMoved = false;
    macroY0 = y;
    macroScroll0 = macroScroll;
    return;
  }
  int dy = macroY0 - y;
  if (dy > 8 || dy < -8) macroMoved = true;
  if (!macroMoved) return;
  if (dy > 160) dy = 160;
  if (dy < -160) dy = -160;
  int next = macroScroll0 + dy;
  if (next < 0) next = 0;
  int limit = toggleMaxScroll();
  if (next > limit) next = limit;
  if (next == macroScroll) return;
  macroScroll = next;
  macroDirty = true;
}

static void handleTouch() {
  toucht_coords_t p;
  bool down = get_touch_coords(&p);
  if (waitRelease) {
    if (!down) waitRelease = false;
    return;
  }
  if (screen == SCREEN_KEYS) {
    if (!down) {
      handleKeysTouch(false, 0, 0);
      return;
    }
    int16_t x = (int16_t)p.x;
    int16_t y = (int16_t)p.y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > 239) x = 239;
    if (y > 319) y = 319;
    handleKeysTouch(true, y, (int16_t)(239 - x));
    return;
  }
  if (screen == SCREEN_HOME) {
    handleHomeTouch(down, (int16_t)p.x, (int16_t)p.y);
    return;
  }
  if (screen == SCREEN_MACROS) {
    handleMacrosTouch(down, (int16_t)p.x, (int16_t)p.y);
    return;
  }
  fingerDown = false;
  dragging = false;
  dragFrom = -1;
  dragOver = -1;

  if (!down) {
    touchHeld = false;
    return;
  }
  if (touchHeld) return;
  touchHeld = true;

  const UiRect* setRs[] = {
    &btnSetBack, &btnCenter, &btnDzMinus, &btnDzPlus, &btnSpdMinus, &btnSpdPlus,
    &btnSlowMinus, &btnSlowPlus, &btnInvX, &btnInvY, &btnMacros
  };
  const UiRect* hit = findTouch(p.x, p.y, setRs, 11);
  if (hit == &btnSetBack) {
    screen = SCREEN_HOME;
    drawHome();
    return;
  }
  if (hit == &btnMacros) {
    screen = SCREEN_MACROS;
    drawMacros();
    return;
  }
  if (hit == &btnCenter) {
    captureCenter();
    drawSettings();
    return;
  }
  if (hit == &btnDzMinus) {
    deadzone -= 20;
    if (deadzone < 20) deadzone = 20;
    saveCal();
    return;
  }
  if (hit == &btnDzPlus) {
    deadzone += 20;
    if (deadzone > 400) deadzone = 400;
    saveCal();
    return;
  }
  if (hit == &btnSpdMinus) {
    speedPct -= 10;
    if (speedPct < 20) speedPct = 20;
    saveCal();
    return;
  }
  if (hit == &btnSpdPlus) {
    speedPct += 10;
    if (speedPct > 200) speedPct = 200;
    saveCal();
    return;
  }
  if (hit == &btnSlowMinus) {
    selectPct -= 10;
    if (selectPct < 10) selectPct = 10;
    saveCal();
    return;
  }
  if (hit == &btnSlowPlus) {
    selectPct += 10;
    if (selectPct > 100) selectPct = 100;
    saveCal();
    return;
  }
  if (hit == &btnInvX) {
    invX = !invX;
    saveCal();
    drawButton(btnInvX, invX);
    return;
  }
  if (hit == &btnInvY) {
    invY = !invY;
    saveCal();
    drawButton(btnInvY, invY);
    return;
  }
}

void setup() {
  Serial.begin(115200);
  Serial0.begin(115200);
  pinMode(JOY_SW, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);

  loadCal();

  neopixelWrite(RGB_BUILTIN, 0, 0, 0);

  gfx->begin();
  gfx->setRotation(0);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->fillScreen(COL_TAN);
  touch_init(TP_SDA, TP_SCL, TP_RST, TP_INT);
  drawHome();

  if (!haveSavedCal) {
    // First boot: leave the stick resting in the center. NVS only.
    delay(200);
    captureCenter();
  }

  Mouse.begin();
  Keyboard.begin();
  USB.begin();

  Serial.printf("Joystick mouse  center %d,%d  dead %d  slots %u %u %u %u\n",
                centerX, centerY, deadzone, slot[0], slot[1], slot[2], slot[3]);
}

void loop() {
  readStick();
  handleTouch();
  serviceVolume();

  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if (screen == SCREEN_HOME) {
    for (uint8_t i = 0; i < MACRO_SLOTS; i++) {
      if (sentUntil[i] != 0 && now > sentUntil[i]) {
        sentUntil[i] = 0;
        drawMacroButton(i);
      }
    }
  }
  if (screen == SCREEN_MACROS && macroDirty && now - macroPaintAt >= 40) {
    macroDirty = false;
    macroPaintAt = now;
    drawMacroList();
  }
  if (now - lastDraw >= 80) {
    lastDraw = now;
    drawLive();
    Serial0.printf("X %d Y %d dx %+d dy %+d sw %d\n",
                   rawX, rawY, moveX, moveY, clickDown ? 1 : 0);
  }
  delay(12);
}
