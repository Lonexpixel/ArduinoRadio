#include <Arduino.h>
#include <BLE.h>
#include <Adafruit_NeoPixel.h>

#include <I2S.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BackgroundAudio.h>

// ================= WIFI =================
const char *ssid = "MUSTANG";

// ================= BLE UUIDS =================
const char *serviceUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac5";
const char *strDataUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac8";

// ================= AUDIO =================
#define STREAMBUFF 16384

I2S audio(OUTPUT, 26, 21);
BackgroundAudioMP3Class<RawDataBuffer<STREAMBUFF>> mp3(audio);

WiFiClientSecure *client = nullptr;
HTTPClient http;
uint8_t buff[512];

// ================= STREAMS =================
String urls[] = {
  "https://pureplay.cdnstream1.com/6021_128.mp3",
  "https://live.amperwave.net/direct/townsquare-ktrsfmmp3-ibc3.mp3",
  "https://uzic.ice.infomaniak.ch/uzic-128.mp3"
};

float gains[] = {1.0, 0.3, 0.3};
int urlIndex = 0;
String url = urls[0];

// ================= STATE =================
bool streamActive = false;
bool bleConnected = false;
unsigned long lastAudioData = 0;
bool pendingStreamSwitch = false;
bool muted = false;
float currentGain = 1.0;

// ================= WIFI =================
void ConnectWiFi() {
  Serial.print("Connecting WiFi...");
  WiFi.begin(ssid);

  while (!WiFi.isConnected()) {
    delay(100);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
}

// ================= STREAM =================
void startStream() {
  http.end();

  if (client) {
    delete client;
    client = nullptr;
  }

  client = new WiFiClientSecure();
  client->setInsecure();

  Serial.printf("Connecting stream: %s\n", url.c_str());

  http.begin(*client, url);
  http.setReuse(false);

  int code = http.GET();

  if (code != HTTP_CODE_OK) {
    Serial.printf("Stream failed (HTTP error %d)\n", code);
    streamActive = false;
    return;
  }

  // Seed gain from station default, respect mute state
  currentGain = gains[urlIndex];
  mp3.setGain(muted ? 0.0 : currentGain);

  streamActive = true;
  lastAudioData = millis();
  Serial.println("Stream started OK");
}

// ================= RADIO LOOP =================
void runRadio() {
  if (!WiFi.isConnected()) return;
  if (!streamActive) return;

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) return;

  int available = stream->available();
  if (available <= 0) return;

  size_t len = min((size_t)512, (size_t)available);

  int r = stream->read(buff, len);
  if (r <= 0) return;

  lastAudioData = millis();

  mp3.write(buff, r);

  if (mp3.available() < 1024) mp3.pause();
  else if (mp3.paused() && mp3.available() > STREAMBUFF / 2) mp3.unpause();
}

// ================= COMMAND HANDLER =================
// NOTE: This runs inside a BLE notify callback — do NOT call HTTPClient
// or WiFiClient here. Only set flags and update simple state.
void handleCommand(String val) {
  val.trim();

  Serial.println("BLE Command: " + val);

  if (val == "FRONT") {
    Serial.println("Switching station...");
    streamActive = false;
    urlIndex = (urlIndex + 1) % 3;
    url = urls[urlIndex];
    pendingStreamSwitch = true;
    return;
  }

  if (val == "BACK") {
    muted = !muted;
    mp3.setGain(muted ? 0.0 : currentGain);
    Serial.println(muted ? "MUTED" : "UNMUTED");
    return;
  }

  if (val == "LEFT") {
    currentGain = max(0.0f, currentGain - 0.25f);
    if (!muted) mp3.setGain(currentGain);
    Serial.printf("Volume down: %.2f\n", currentGain);
    return;
  }

  if (val == "RIGHT") {
    currentGain = min(1.0f, currentGain + 0.25f);
    if (!muted) mp3.setGain(currentGain);
    Serial.printf("Volume up: %.2f\n", currentGain);
    return;
  }

  if (val == "LOGIN") {
    Serial.println("User logged in");
    return;
  }
}

// ================= BLE CALLBACK =================
void notify(BLERemoteCharacteristic *c, const uint8_t *data, uint32_t len) {
  String val;

  for (uint32_t i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (ch == '\r' || ch == '\n') continue;
    val += ch;
  }

  handleCommand(val);
}

// ================= BLE STATE =================
unsigned long lastScan = 0;

void handleBLE() {
  if (BLE.client()->connected()) {
    bleConnected = true;
    return;
  }

  bleConnected = false;

  if (millis() - lastScan < 2000) return;
  lastScan = millis();

  auto report = BLE.scan(BLEUUID(serviceUUID), 3);
  if (!report || report->empty()) return;

  BLEAdvertising dev = report->front();

  if (!BLE.client()->connect(dev, 5)) {
    Serial.println("BLE connect failed");
    return;
  }

  auto svc = BLE.client()->service(BLEUUID(serviceUUID));
  if (!svc) {
    Serial.println("BLE service missing → disconnect");
    BLE.client()->disconnect();
    return;
  }

  auto ch = svc->characteristic(BLEUUID(strDataUUID));
  if (!ch) {
    Serial.println("BLE characteristic missing → disconnect");
    BLE.client()->disconnect();
    return;
  }

  ch->onNotify(notify);
  ch->enableNotifications();

  bleConnected = true;
  Serial.println("BLE FULLY CONNECTED (verified)");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  mp3.begin();
  mp3.setGain(1.0);

  ConnectWiFi();
  startStream();

  BLE.begin();

  Serial.println("System Ready (Audio + BLE active)");
}

// ================= LOOP =================
void loop() {
  // Handle deferred stream switch from BLE callback
  if (pendingStreamSwitch) {
    pendingStreamSwitch = false;
    startStream();
  }

  runRadio();
  handleBLE();
}