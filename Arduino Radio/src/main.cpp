#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <BLE.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_NeoPixel.h>
#include "SparkFun_Qwiic_Keypad_Arduino_Library.h"

// ================= CONFIG =================
#define NEO_PIN 28
#define NEO_COUNT 1

Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LSM6DSOX imu;
KEYPAD keypad1;

// ================= BLE =================
BLEService service(BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac5")));

BLECharacteristic strData(
  BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac8")),
  BLERead | BLENotify,
  "Direction Data"
);

// ================= USERS =================
const char keyCodes[4][4] = {
  {'6','7','6','7'}, // Mr.Spice
  {'6','9','6','9'}, // Mr.Riggs
  {'0','4','2','0'}, // Mr.Cin
  {'1','1','1','1'}  // Mr.1
};

const char* userNames[4] = {
  "Mr.Spice",
  "Mr.Riggs",
  "Mr.Cin",
  "Mr.1"
};

// ================= LOGIN STATE =================
char userEntry[4] = {0};
byte userEntryIndex = 0;
int currentUser = -1;
byte starCount = 0;

// ================= COLORS =================
uint32_t GREEN, BLUE, RED, YELLOW, PURPLE, ORANGE, OFF;

// ================= PATTERNS =================
uint32_t userPatterns[4][4];

// ================= IMU =================
enum Orientation {
  ORIENT_LEFT,
  ORIENT_RIGHT,
  ORIENT_FRONT,
  ORIENT_BACK,
  ORIENT_UNKNOWN
};

Orientation lastOrientation = ORIENT_UNKNOWN;
const float THRESHOLD = 2.0;

// ✅ NEW: debounce timing
unsigned long lastSendTime = 0;
const unsigned long SEND_COOLDOWN = 500;

// ================= HELPERS =================
void setPixel(uint32_t color) {
  strip.setPixelColor(0, color);
  strip.show();
}

void flashUserPattern(int user) {
  for (int i = 0; i < 4; i++) {
    setPixel(userPatterns[user][i]);
    delay(250);
  }
  setPixel(OFF);
}

void sendDirection(const char* dir) {
  strData.setValue(dir);
  Serial.print("Sent: ");
  Serial.println(dir);
}

void setOrientationColor(Orientation o) {
  if (o == ORIENT_LEFT) {
    setPixel(BLUE);
    sendDirection("LEFT");
  }
  else if (o == ORIENT_RIGHT) {
    setPixel(YELLOW);
    sendDirection("RIGHT");
  }
  else if (o == ORIENT_FRONT) {
    setPixel(PURPLE);
    sendDirection("FRONT");
  }
  else if (o == ORIENT_BACK) {
    setPixel(ORANGE);
    sendDirection("BACK");
  }
  else {
    setPixel(OFF);
  }
}

// ================= LOGIN =================
int checkEntry() {
  for (int user = 0; user < 4; user++) {
    bool match = true;
    for (int i = 0; i < 4; i++) {
      if (userEntry[i] != keyCodes[user][i]) {
        match = false;
        break;
      }
    }
    if (match) return user;
  }
  return -1;
}

void clearEntry() {
  for (int i = 0; i < 4; i++) userEntry[i] = 0;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin();

  // NeoPixel
  strip.begin();
  strip.setBrightness(50);

  // Colors
  GREEN  = strip.Color(0,255,0);
  BLUE   = strip.Color(0,0,255);
  RED    = strip.Color(255,0,0);
  YELLOW = strip.Color(255,255,50);
  PURPLE = strip.Color(128,0,128);
  ORANGE = strip.Color(180,50,0);
  OFF    = strip.Color(0,0,0);

  // Patterns
  uint32_t temp[4][4] = {
    {ORANGE, BLUE, ORANGE, BLUE},
    {YELLOW, PURPLE, YELLOW, PURPLE},
    {RED, BLUE, RED, BLUE},
    {PURPLE, GREEN, PURPLE, GREEN}
  };
  memcpy(userPatterns, temp, sizeof(temp));

  setPixel(OFF);

  // IMU
  if (!imu.begin_I2C(0x6B)) {
    Serial.println("IMU NOT FOUND");
    while (1);
  }

  // Keypad
  keypad1.begin();

  // BLE
  BLE.setSecurity(BLESecurityJustWorks);
  BLE.begin("Dr.Spice");

  service.addCharacteristic(&strData);
  BLE.server()->addService(&service);

  strData.setValue("IDLE");

  BLE.startAdvertising();

  Serial.println("System Ready - Login Required");
}

// ================= LOOP =================
void loop() {

  // ---------- KEYPAD ----------
  keypad1.updateFIFO();
  char button = keypad1.getButton();

  if (button != 0 && button != -1) {

    Serial.print("Pressed: ");
    Serial.println(button);

    // LOGOUT (***)
    if (button == '*') {
      starCount++;

      if (starCount == 3) {
        Serial.println("Logging out");
        currentUser = -1;
        setPixel(OFF);
        strData.setValue("LOGOUT");

        clearEntry();
        userEntryIndex = 0;
        starCount = 0;
        return;
      }

      clearEntry();
      userEntryIndex = 0;
      return;
    } else {
      starCount = 0;
    }

    // SUBMIT (#)
    if (button == '#') {
      if (currentUser == -1 && userEntryIndex == 4) {
        int user = checkEntry();

        if (user != -1) {
          currentUser = user;

          Serial.print("Welcome ");
          Serial.println(userNames[user]);

          flashUserPattern(user);
          strData.setValue("LOGIN");
        } else {
          Serial.println("Wrong Code");
        }
      }

      clearEntry();
      userEntryIndex = 0;
      return;
    }

    // STORE DIGITS
    if (currentUser == -1 && userEntryIndex < 4) {
      userEntry[userEntryIndex++] = button;
    }
  }

  // ---------- IMU ----------
  if (currentUser != -1) {

    sensors_event_t accel, gyro, temp;
    imu.getEvent(&accel, &gyro, &temp);

    float ax = accel.acceleration.x;
    float ay = accel.acceleration.y;

    float absX = abs(ax);
    float absY = abs(ay);

    Orientation current = ORIENT_UNKNOWN;

    if (absX > absY + THRESHOLD)
      current = (ax > 0) ? ORIENT_RIGHT : ORIENT_LEFT;

    else if (absY > absX + THRESHOLD)
      current = (ay > 0) ? ORIENT_FRONT : ORIENT_BACK;

    unsigned long now = millis();

    // ✅ FIXED: debounce + ignore UNKNOWN
    if (current != ORIENT_UNKNOWN &&
        current != lastOrientation &&
        (now - lastSendTime > SEND_COOLDOWN)) {

      setOrientationColor(current);
      lastOrientation = current;
      lastSendTime = now;
    }
  }

  delay(100);
}