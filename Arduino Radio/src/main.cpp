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

// ================= CONNECTION STATE =================
bool isConnected = false;

// ✅ BLE CALLBACKS (THIS FIXES CONNECTION STATUS)
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    isConnected = true;
    Serial.println("\n✅ CONNECTED");
    strip.setPixelColor(0, strip.Color(0,255,0)); // GREEN
    strip.show();
  }

  void onDisconnect(BLEServer* pServer) {
    isConnected = false;
    Serial.println("\n❌ DISCONNECTED");
    strip.setPixelColor(0, strip.Color(255,0,0)); // RED
    strip.show();

    BLE.startAdvertising(); // 🔥 IMPORTANT: allow reconnect
  }
};

// ================= USERS =================
const char keyCodes[4][4] = {
  {'6','7','6','7'},
  {'6','9','6','9'},
  {'0','4','2','0'},
  {'1','1','1','1'}
};

const char* userNames[4] = {
  "Mr.Spice","Mr.Riggs","Mr.Cin","Mr.1"
};

char userEntry[4] = {0};
byte userEntryIndex = 0;
int currentUser = -1;
byte starCount = 0;

// ================= COLORS =================
uint32_t GREEN, BLUE, RED, YELLOW, PURPLE, ORANGE, OFF;

// ================= IMU =================
enum Orientation {
  ORIENT_LEFT, ORIENT_RIGHT, ORIENT_FRONT, ORIENT_BACK, ORIENT_UNKNOWN
};

Orientation lastOrientation = ORIENT_UNKNOWN;
const float THRESHOLD = 2.0;

unsigned long lastSendTime = 0;
const unsigned long SEND_COOLDOWN = 500;

// ================= HELPERS =================
void setPixel(uint32_t color) {
  strip.setPixelColor(0, color);
  strip.show();
}

void sendDirection(const char* dir) {
  strData.setValue(dir);
  Serial.print("📡 Sent: ");
  Serial.println(dir);
}

void setOrientationColor(Orientation o) {
  if (o == ORIENT_LEFT)  { setPixel(BLUE);   sendDirection("LEFT"); }
  if (o == ORIENT_RIGHT) { setPixel(YELLOW); sendDirection("RIGHT"); }
  if (o == ORIENT_FRONT) { setPixel(PURPLE); sendDirection("FRONT"); }
  if (o == ORIENT_BACK)  { setPixel(ORANGE); sendDirection("BACK"); }
}

// ================= LOGIN =================
int checkEntry() {
  for (int u = 0; u < 4; u++) {
    bool match = true;
    for (int i = 0; i < 4; i++) {
      if (userEntry[i] != keyCodes[u][i]) match = false;
    }
    if (match) return u;
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

  strip.begin();
  strip.setBrightness(50);

  GREEN  = strip.Color(0,255,0);
  RED    = strip.Color(255,0,0);
  BLUE   = strip.Color(0,0,255);
  YELLOW = strip.Color(255,255,0);
  PURPLE = strip.Color(128,0,128);
  ORANGE = strip.Color(180,50,0);
  OFF    = strip.Color(0,0,0);

  setPixel(RED); // waiting

  if (!imu.begin_I2C(0x6B)) {
    Serial.println("IMU FAIL");
    while(1);
  }

  keypad1.begin();

  BLE.setSecurity(BLESecurityJustWorks);
  BLE.begin("Dr.Spice");

  BLE.server()->setCallbacks(new MyServerCallbacks());

  service.addCharacteristic(&strData);
  BLE.server()->addService(&service);

  strData.setValue("IDLE");

  BLE.startAdvertising();

  Serial.println("🔍 Waiting for connection...");
}

// ================= LOOP =================
void loop() {

  // ---------- KEYPAD ----------
  keypad1.updateFIFO();
  char button = keypad1.getButton();

  if (button != 0 && button != -1) {

    Serial.print("Pressed: ");
    Serial.println(button);

    // LOGOUT ***
    if (button == '*') {
      starCount++;

      if (starCount == 3) {
        Serial.println("🚪 LOGGING OUT");
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
      Serial.println("🔄 Entry Cleared");
      return;
    } else {
      starCount = 0;
    }

    // SUBMIT #
    if (button == '#') {

      Serial.print("Entered Code: ");
      for (int i = 0; i < userEntryIndex; i++) {
        Serial.print(userEntry[i]);
      }
      Serial.println();

      if (currentUser == -1 && userEntryIndex == 4) {
        int user = checkEntry();

        if (user != -1) {
          currentUser = user;
          Serial.print("✅ LOGIN SUCCESS: ");
          Serial.println(userNames[user]);

          strData.setValue("LOGIN");
          setPixel(GREEN);
        } else {
          Serial.println("❌ WRONG CODE");
        }
      }

      clearEntry();
      userEntryIndex = 0;
      return;
    }

    // STORE DIGITS
    if (currentUser == -1 && userEntryIndex < 4) {
      userEntry[userEntryIndex++] = button;

      Serial.print("Typing: ");
      for (int i = 0; i < userEntryIndex; i++) {
        Serial.print("*"); // hide actual digits
      }
      Serial.println();
    }
  }

  // ---------- IMU ----------
  if (currentUser != -1 && isConnected) {

    sensors_event_t accel, gyro, temp;
    imu.getEvent(&accel, &gyro, &temp);

    float ax = accel.acceleration.x;
    float ay = accel.acceleration.y;

    Orientation current = ORIENT_UNKNOWN;

    if (abs(ax) > abs(ay) + THRESHOLD)
      current = (ax > 0) ? ORIENT_RIGHT : ORIENT_LEFT;
    else if (abs(ay) > abs(ax) + THRESHOLD)
      current = (ay > 0) ? ORIENT_FRONT : ORIENT_BACK;

    unsigned long now = millis();

    if (current != ORIENT_UNKNOWN &&
        current != lastOrientation &&
        now - lastSendTime > SEND_COOLDOWN) {

      setOrientationColor(current);
      lastOrientation = current;
      lastSendTime = now;
    }
  }

  delay(50);
}