#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_NeoPixel.h>
#include "SparkFun_Qwiic_Keypad_Arduino_Library.h"

#define NEO_PIN 28
#define NEO_COUNT 1

Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LSM6DSOX imu;
KEYPAD keypad1;

// -------------------- USERS --------------------
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

// -------------------- LOGIN STATE --------------------
char userEntry[4] = {0};
byte userEntryIndex = 0;
int currentUser = -1;
byte starCount = 0;

// -------------------- IMU --------------------
enum Orientation {
  ORIENT_LEFT,
  ORIENT_RIGHT,
  ORIENT_FRONT,
  ORIENT_BACK,
  ORIENT_UNKNOWN
};

Orientation lastOrientation = ORIENT_UNKNOWN;
const float THRESHOLD = 2.0;

// -------------------- COLORS --------------------
uint32_t GREEN  = strip.Color(0,255,0);
uint32_t BLUE   = strip.Color(0,0,255);
uint32_t RED    = strip.Color(255,0,0);
uint32_t YELLOW = strip.Color(255,255,50);
uint32_t PURPLE = strip.Color(128,0,128);
uint32_t ORANGE = strip.Color(180,50,0);
uint32_t OFF    = strip.Color(0,0,0);

// -------------------- USER PATTERNS --------------------
uint32_t userPatterns[4][4] = {
  {ORANGE, BLUE, ORANGE, BLUE},     // Mr.Spice (6767)
  {YELLOW, PURPLE, YELLOW, PURPLE}, // Mr.Riggs (6969)
  {RED, BLUE, RED, BLUE},           // Mr.Cin (0420)
  {PURPLE, GREEN, PURPLE, GREEN}    // Mr.1 (1111)
};

// -------------------- FUNCTIONS --------------------
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

void setOrientationColor(Orientation o) {
  if (o == ORIENT_LEFT)       setPixel(BLUE);
  else if (o == ORIENT_RIGHT) setPixel(YELLOW);
  else if (o == ORIENT_FRONT) setPixel(PURPLE);
  else if (o == ORIENT_BACK)  setPixel(ORANGE);
  else                        setPixel(OFF);
}

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

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // 🔥 FIX: Initialize I2C FIRST
  Wire.begin();
  delay(100);

  Serial.println("Starting system...");

  // NeoPixel
  strip.begin();
  strip.setBrightness(50);
  setPixel(OFF);

  // IMU
  if (!imu.begin_I2C(0x6B)) {
    Serial.println("IMU NOT FOUND");
    while (1);
  }
  Serial.println("IMU OK");

  // Keypad
  if (!keypad1.begin()) {
    Serial.println("Keypad INIT FAILED");
  } else {
    Serial.println("Keypad INIT SUCCESS");
  }

  Serial.println("System Ready - Awaiting Login");
}

// -------------------- LOOP --------------------
void loop() {

  // ----------- KEYPAD -----------
  keypad1.updateFIFO();
  char button = keypad1.getButton();

  if (button != 0 && button != -1) {

    Serial.print("Pressed: ");
    Serial.println(button);

    if (button == '*') {
      starCount++;

      if (starCount == 3) {
        if (currentUser != -1) {
          Serial.print("Logging out ");
          Serial.println(userNames[currentUser]);
          currentUser = -1;
          setPixel(OFF);
        }
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

    if (button == '#') {
      if (currentUser == -1 && userEntryIndex == 4) {
        int user = checkEntry();
        if (user != -1) {
          currentUser = user;

          Serial.print("Welcome ");
          Serial.println(userNames[user]);

          flashUserPattern(user); // 🎨 login effect
        } else {
          Serial.println("Wrong Code");
        }
      }
      clearEntry();
      userEntryIndex = 0;
      return;
    }

    if (currentUser == -1 && userEntryIndex < 4) {
      userEntry[userEntryIndex++] = button;
    }
  }

  // ----------- IMU (ONLY IF LOGGED IN) -----------
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

    else
      current = ORIENT_UNKNOWN;

    if (current != lastOrientation) {
      setOrientationColor(current);
      lastOrientation = current;
    }
  }

  delay(100);
}