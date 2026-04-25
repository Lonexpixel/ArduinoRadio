#include <Arduino.h>
#include <BLE.h>
#include <Adafruit_NeoPixel.h>

// ================= BLE UUIDs (MATCH SENDER) =================
BLEService service(BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac5")));

BLECharacteristic strData(
  BLEUUID(String("b44eb0b6-da3c-4ebf-a680-01a487661ac8")),
  BLERead | BLEWrite | BLENotify,
  "Direction Data"
);

// ================= HARDWARE =================
#define NEO_PIN 19
#define NEO_COUNT 5

Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// ================= STATE =================
bool loggedIn = false;
String lastCommand = "";

// ================= COLORS =================
uint32_t BLUE, YELLOW, PURPLE, ORANGE, GREEN, OFF;

// ================= HELPERS =================
void setAll(uint32_t color) {
  for (int i = 0; i < NEO_COUNT; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

// ================= BLE CALLBACK =================
void handleBLEWrite(BLECharacteristic *c) {
  String val = c->getString();

  Serial.print("Received: ");
  Serial.println(val);

  // Prevent spam duplicates
  if (val == lastCommand) return;
  lastCommand = val;

  // ---------- LOGIN ----------
  if (val == "LOGIN") {
    loggedIn = true;
    Serial.println("User Logged In");
    setAll(GREEN);
    return;
  }

  // ---------- LOGOUT ----------
  if (val == "LOGOUT") {
    loggedIn = false;
    Serial.println("User Logged Out");
    setAll(OFF);
    return;
  }

  // Ignore if not logged in
  if (!loggedIn) return;

  // ---------- DIRECTIONS ----------
  if (val == "LEFT") {
    setAll(BLUE);
  }
  else if (val == "RIGHT") {
    setAll(YELLOW);
  }
  else if (val == "FRONT") {
    setAll(PURPLE);
  }
  else if (val == "BACK") {
    setAll(ORANGE);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("Receiver Starting...");

  // NeoPixel
  strip.begin();
  strip.setBrightness(50);

  BLUE   = strip.Color(0,0,255);
  YELLOW = strip.Color(255,255,0);
  PURPLE = strip.Color(128,0,128);
  ORANGE = strip.Color(255,140,0);
  GREEN  = strip.Color(0,255,0);
  OFF    = strip.Color(0,0,0);

  setAll(OFF);

  // BLE INIT (PICO CORRECT WAY)
  BLE.setSecurity(BLESecurityJustWorks);
  BLE.begin("ReceiverBoard");

  service.addCharacteristic(&strData);
  BLE.server()->addService(&service);

  strData.setValue("READY");

  strData.onWrite(handleBLEWrite);

  BLE.startAdvertising();

  Serial.println("BLE Ready - Waiting for sender...");
}

// ================= LOOP =================
void loop() {
  delay(10);
}