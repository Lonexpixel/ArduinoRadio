#include <Arduino.h>
#include <Wire.h>
#include <BLE.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LSM6DSOX.h>
#include <Adafruit_NeoPixel.h>
#include "SparkFun_Qwiic_Keypad_Arduino_Library.h"

#define NEO_PIN 28
#define NEO_COUNT 1

Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_LSM6DSOX imu;
KEYPAD keypad1;

BLEService service(BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac5")));

BLECharacteristic strData(
  BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac8")),
  BLERead | BLENotify,
  "Direction Data"
);

const char keyCodes[4][4] = {
  {'6','9','6','9'},
  {'1','1','1','1'},
  {'0','4','2','0'},
  {'6','7','6','7'}
};

const char* userNames[4] = {
  "Dylan","Rocky","Nicholas","John"
};

char userEntry[4] = {0};
byte entryIndex = 0;
int currentUser = -1;
int starPress = 0;

uint32_t GREEN, BLUE, RED, YELLOW, PURPLE, ORANGE, OFF;

bool bleConnected = false;

unsigned long lastSendTime = 0;
const unsigned long SEND_COOLDOWN = 3000;

enum Orientation { LEFT, RIGHT, FRONT, BACK, NONE };
const float THRESHOLD = 7.5;

void setPixel(uint32_t c) {
  strip.setPixelColor(0, c);
  strip.show();
}

void sendDirection(const char* dir) {
  if (!bleConnected) return;
  strData.setValue(dir);
  Serial.print("Sent: ");
  Serial.println(dir);
}

int checkUser() {
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
  entryIndex = 0;
}

class MyCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleConnected = true;
    Serial.println("CONNECTED");
    setPixel(GREEN);
  }

  void onDisconnect(BLEServer* pServer) {
    bleConnected = false;
    Serial.println("DISCONNECTED");
    BLE.startAdvertising();
    setPixel(OFF);
  }
};

void setup() {
  Serial.begin(115200);
  delay(1500);

  Wire.begin();

  strip.begin();
  strip.setBrightness(60);

  GREEN  = strip.Color(0,255,0);
  BLUE   = strip.Color(0,0,255);
  RED    = strip.Color(255,0,0);
  YELLOW = strip.Color(255,255,0);
  PURPLE = strip.Color(160,0,160);
  ORANGE = strip.Color(255,120,0);
  OFF    = strip.Color(0,0,0);

  setPixel(RED);

  if (!imu.begin_I2C(0x6B)) {
    Serial.println("IMU FAIL");
    while (1);
  }

  keypad1.begin();

  BLE.setSecurity(BLESecurityJustWorks);
  BLE.begin("SenderBoard");

  service.addCharacteristic(&strData);
  BLE.server()->addService(&service);

  strData.setValue("READY");

  BLE.server()->setCallbacks(new MyCallbacks());

  BLE.startAdvertising();

  Serial.println("WAITING");
}

void loop() {
  keypad1.updateFIFO();
  char key = keypad1.getButton();

  if (key) {

    // Completely hide PIN input
    if (currentUser == -1 && key != '#' && key != '*') {
      Serial.println("*");
    }

    if (key == '*') {
      starPress++;
      if (starPress >= 3) {
        currentUser = -1;
        clearEntry();
        setPixel(OFF);
        sendDirection("LOGOUT");
        starPress = 0;
        return;
      }
      clearEntry();
      return;
    } else {
      starPress = 0;
    }

    if (key == '#') {

      if (currentUser == -1) {

        if (entryIndex < 4) {
          Serial.println("PIN TOO SHORT");
          return;
        }

        int user = checkUser();

        if (user != -1) {
          currentUser = user;
          Serial.println(userNames[user]);

          setPixel(GREEN);
          sendDirection("LOGIN");

          for (int i = 0; i < 2; i++) {
            setPixel(YELLOW); delay(150);
            setPixel(PURPLE); delay(150);
            setPixel(YELLOW); delay(150);
            setPixel(PURPLE); delay(150);
          }

        } else {
          Serial.println("WRONG PIN");
          setPixel(RED);
          clearEntry();
        }
      }

      return;
    }

    if (currentUser == -1 && entryIndex < 4) {
      userEntry[entryIndex++] = key;
    }
  }

  if (currentUser != -1 && bleConnected) {
    sensors_event_t accel, gyro, temp;
    imu.getEvent(&accel, &gyro, &temp);

    float ax = accel.acceleration.x;
    float ay = accel.acceleration.y;

    Orientation current = NONE;

    if (abs(ax) > abs(ay) + THRESHOLD)
      current = (ax > 0) ? RIGHT : LEFT;
    else if (abs(ay) > abs(ax) + THRESHOLD)
      current = (ay > 0) ? FRONT : BACK;

    unsigned long now = millis();

    if (current != NONE && (now - lastSendTime > SEND_COOLDOWN)) {

      if (current == LEFT)  { setPixel(BLUE);   sendDirection("LEFT"); }
      if (current == RIGHT) { setPixel(YELLOW); sendDirection("RIGHT"); }
      if (current == FRONT) { setPixel(PURPLE); sendDirection("FRONT"); }
      if (current == BACK)  { setPixel(ORANGE); sendDirection("BACK"); }

      lastSendTime = now;
    }
  }

  delay(20);
}