#include <BackgroundAudioSpeech.h>
#include <libespeak-ng/voice/en_029.h>
#include <libespeak-ng/voice/en_gb_scotland.h>
#include <libespeak-ng/voice/en_gb_x_gbclan.h>
#include <libespeak-ng/voice/en_gb_x_gbcwmd.h>
#include <libespeak-ng/voice/en_gb_x_rp.h>
#include <libespeak-ng/voice/en.h>
#include <libespeak-ng/voice/en_shaw.h>
#include <libespeak-ng/voice/en_us.h>
#include <libespeak-ng/voice/en_us_nyc.h>
BackgroundAudioVoice v[] = {
  voice_en_029,
  voice_en_gb_scotland,
  voice_en_gb_x_gbclan,
  voice_en_gb_x_gbcwmd,
  voice_en,
  voice_en_shaw,
  voice_en_us,
  voice_en_us_nyc
};
 
#include <I2S.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BackgroundAudio.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>

// ================= UUIDs (FROM YOUR FIRST FILE) =================
const char *serviceUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac5";
const char *strDataUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac8";

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

// ================= COMMAND HANDLER =================
void handleCommand(String val) {
  Serial.print("Received: ");
  Serial.println(val);

  if (val == lastCommand) return;
  lastCommand = val;

  if (val == "LOGIN") {
    loggedIn = true;
    Serial.println("User Logged In");
    setAll(GREEN);
    return;
  }

  if (val == "LOGOUT") {
    loggedIn = false;
    Serial.println("User Logged Out");
    setAll(OFF);
    return;
  }

  if (!loggedIn) return;

  if (val == "LEFT") setAll(BLUE);
  else if (val == "RIGHT") setAll(YELLOW);
  else if (val == "FRONT") setAll(PURPLE);
  else if (val == "BACK") setAll(ORANGE);
}

// ================= NOTIFY CALLBACK =================
void notify(BLERemoteCharacteristic *c, const uint8_t *data, uint32_t len) {
  String val = "";
  for (uint32_t i = 0; i < len; i++) {
    val += (char)data[i];
  }

  handleCommand(val);
}

uint32_t cnt = 1;

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println("Client Receiver Starting...");

  // NeoPixel init
  strip.begin();
  strip.setBrightness(50);

  BLUE   = strip.Color(0,0,255);
  YELLOW = strip.Color(255,255,0);
  PURPLE = strip.Color(128,0,128);
  ORANGE = strip.Color(255,140,0);
  GREEN  = strip.Color(0,255,0);
  OFF    = strip.Color(0,0,0);

  setAll(OFF);

  // BLE as CLIENT
  BLE.begin();
  Serial.println("BLE Client started");
}

void loop() {
  Serial.printf("Scanning %d...\n", cnt++);

  auto report = BLE.scan(BLEUUID(serviceUUID), 5);

  if (report->empty()) {
    Serial.println("No devices found, retrying...");
    delay(3000);
    return;
  }

  BLEAdvertising device = report->front();

  Serial.print("Connecting to: ");
  Serial.println(device.toString());

  if (!BLE.client()->connect(device, 10)) {
    Serial.println("Connection failed");
    delay(3000);
    return;
  }

  Serial.println("CONNECTED");

  auto svc = BLE.client()->service(BLEUUID(serviceUUID));
  if (!svc) {
    Serial.println("Service not found");
    BLE.client()->disconnect();
    return;
  }

  auto strData = svc->characteristic(BLEUUID(strDataUUID));
  if (!strData) {
    Serial.println("Characteristic not found");
    BLE.client()->disconnect();
    return;
  }

  // Enable notifications
  Serial.println("Enabling notifications...");
  strData->onNotify(notify);
  strData->enableNotifications();

  // Initial read (optional)
  String initial = strData->getString();
  if (initial.length()) {
    handleCommand(initial);
  }

  // Stay connected and receive data
  while (BLE.client()->connected()) {
    delay(100);
  }

  Serial.println("DISCONNECTED");
  delay(2000);
}