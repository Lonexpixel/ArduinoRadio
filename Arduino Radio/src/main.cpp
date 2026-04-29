#include <Arduino.h>
#include <BLE.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>

#include <I2S.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <BackgroundAudio.h>


const char *ssid = "MUSTANG";


const char *serviceUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac5";
const char *strDataUUID = "b44eb0b6-da3c-4ebf-a680-01a487661ac8";


#define STREAMBUFF 16384

I2S audio(OUTPUT, 26, 21);
BackgroundAudioMP3Class<RawDataBuffer<STREAMBUFF>> mp3(audio);

WiFiClientSecure *client = nullptr;
HTTPClient http;
uint8_t buff[512];


Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();


String urls[] = {
  "https://pureplay.cdnstream1.com/6021_128.mp3",
  "https://live.amperwave.net/direct/townsquare-ktrsfmmp3-ibc3.mp3",
  "https://uzic.ice.infomaniak.ch/uzic-128.mp3"
};

float gains[] = {1.0, 0.3, 0.3};
int urlIndex = 0;
String url = urls[0];

bool streamActive = false;
bool bleConnected = false;
unsigned long lastAudioData = 0;
bool pendingStreamSwitch = false;
bool muted = false;
float currentGain = 1.0;


enum DisplayMode { DISP_LOADING, DISP_SONG, DISP_MUTED, DISP_UNMUTED, DISP_VOLUME };
DisplayMode displayMode = DISP_LOADING;

String scrollText = "LOADING";
int scrollPos = 0;                   
unsigned long lastScrollTime = 0;
unsigned long tempDisplayUntil = 0;  
bool loadingBlink = false;
unsigned long lastBlinkTime = 0;


String currentSong = "";
String currentArtist = "";
unsigned long lastMetaFetch = 0;
#define META_INTERVAL 10000  


void writeDisplay4(const char *s) {
  char buf[4] = {' ', ' ', ' ', ' '};
  for (int i = 0; i < 4 && s[i] != '\0'; i++) buf[i] = s[i];
  for (int i = 0; i < 4; i++) alpha4.writeDigitAscii(i, buf[i]);
  alpha4.writeDisplay();
}

void tickScroll(const String &text) {
  String padded = "    " + text + "    ";
  int len = padded.length();

  if (scrollPos >= len - 4) scrollPos = 0;

  char buf[5];
  for (int i = 0; i < 4; i++) {
    buf[i] = (scrollPos + i < len) ? padded[scrollPos + i] : ' ';
  }
  buf[4] = '\0';

  writeDisplay4(buf);
  scrollPos++;
}

void setScrollText(const String &text) {
  scrollText = text;
  scrollPos = 0;
}

void fetchMetadata() {
  if (!WiFi.isConnected()) return;

  WiFiClientSecure metaClient;
  metaClient.setInsecure();
  metaClient.setTimeout(5);


  String u = urls[urlIndex];
  String host, path;
  int port = 443;

  String stripped = u;
  if (stripped.startsWith("https://")) stripped = stripped.substring(8);
  else if (stripped.startsWith("http://")) { stripped = stripped.substring(7); port = 80; }

  int slash = stripped.indexOf('/');
  if (slash < 0) {
    host = stripped;
    path = "/";
  } else {
    host = stripped.substring(0, slash);
    path = stripped.substring(slash);
  }


  int colon = host.indexOf(':');
  if (colon >= 0) {
    port = host.substring(colon + 1).toInt();
    host = host.substring(0, colon);
  }

  if (!metaClient.connect(host.c_str(), port)) {
    Serial.println("Metadata connect failed");
    return;
  }


  metaClient.printf("GET %s HTTP/1.0\r\nHost: %s\r\nIcy-MetaData: 1\r\nUser-Agent: Arduino\r\nConnection: close\r\n\r\n",
                    path.c_str(), host.c_str());

  
  unsigned long timeout = millis() + 5000;
  int icyMetaInt = 0;
  String icyLine = "";

  while (metaClient.connected() && millis() < timeout) {
    if (!metaClient.available()) { delay(1); continue; }
    char c = metaClient.read();
    if (c == '\n') {
      icyLine.trim();
      if (icyLine.startsWith("icy-metaint:")) {
        icyMetaInt = icyLine.substring(12).toInt();
      }
      if (icyLine.length() == 0) break;
      icyLine = "";
    } else if (c != '\r') {
      icyLine += c;
    }
  }

  if (icyMetaInt <= 0) {
    Serial.println("No icy-metaint in headers");
    metaClient.stop();
    return;
  }

  
  int skipped = 0;
  timeout = millis() + 8000;
  while (skipped < icyMetaInt && millis() < timeout) {
    if (metaClient.available()) { metaClient.read(); skipped++; }
    else delay(1);
  }

  timeout = millis() + 3000;
  while (!metaClient.available() && millis() < timeout) delay(1);
  if (!metaClient.available()) { metaClient.stop(); return; }

  int metaLen = metaClient.read() * 16;
  if (metaLen == 0) { metaClient.stop(); return; }

  String metaBlock = "";
  timeout = millis() + 3000;
  int read = 0;
  while (read < metaLen && millis() < timeout) {
    if (metaClient.available()) { metaBlock += (char)metaClient.read(); read++; }
    else delay(1);
  }

  metaClient.stop();

  Serial.println("Metadata block: " + metaBlock);

  int start = metaBlock.indexOf("StreamTitle='");
  if (start < 0) return;
  start += 13;
  int end = metaBlock.indexOf("';", start);
  if (end < 0) return;

  String title = metaBlock.substring(start, end);
  title.trim();

  Serial.println("Stream title: " + title);

  int dash = title.indexOf(" - ");
  if (dash >= 0) {
    currentArtist = title.substring(0, dash);
    currentSong = title.substring(dash + 3);
  } else {
    currentArtist = "";
    currentSong = title;
  }

  String full = (currentArtist.length() > 0)
    ? (currentArtist + " - " + currentSong)
    : currentSong;

  full.toUpperCase();
  setScrollText(full);

  if (displayMode == DISP_LOADING || displayMode == DISP_SONG) {
    displayMode = DISP_SONG;
  }
}

void tickDisplay() {
  unsigned long now = millis();

  if ((displayMode == DISP_UNMUTED || displayMode == DISP_VOLUME) && now >= tempDisplayUntil) {
    displayMode = (scrollText.length() > 0) ? DISP_SONG : DISP_LOADING;
    scrollPos = 0;
  }

  switch (displayMode) {

    case DISP_LOADING:
      if (now - lastBlinkTime >= 500) {
        lastBlinkTime = now;
        loadingBlink = !loadingBlink;
        if (loadingBlink) writeDisplay4("LOAD");
        else {
          alpha4.clear();
          alpha4.writeDisplay();
        }
      }
      break;

    case DISP_SONG:
      if (now - lastScrollTime >= 250) {
        lastScrollTime = now;
        tickScroll(scrollText);
      }
      break;

    case DISP_MUTED:
      writeDisplay4("MUTE");
      break;

    case DISP_UNMUTED: {
      writeDisplay4("UNMT");
      break;
    }

    case DISP_VOLUME: {
      int pct = (int)(currentGain * 100.0f);
      char buf[5];
      snprintf(buf, sizeof(buf), "%3d%%", pct);
      writeDisplay4(buf);
      break;
    }
  }
}

void ConnectWiFi() {
  Serial.print("Connecting WiFi...");
  WiFi.begin(ssid);
  while (!WiFi.isConnected()) {
    delay(100);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

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

  currentGain = gains[urlIndex];
  mp3.setGain(muted ? 0.0 : currentGain);

  streamActive = true;
  lastAudioData = millis();
  Serial.println("Stream started OK");

  currentSong = "";
  currentArtist = "";
  setScrollText("LOADING");
  displayMode = DISP_LOADING;
  lastMetaFetch = 0;  // force immediate fetch on next loop
}

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
    if (muted) {
      displayMode = DISP_MUTED;
    } else {
      displayMode = DISP_UNMUTED;
      tempDisplayUntil = millis() + 2000;
    }
    Serial.println(muted ? "MUTED" : "UNMUTED");
    return;
  }

  if (val == "LEFT") {
    currentGain = max(0.0f, currentGain - 0.25f);
    if (!muted) mp3.setGain(currentGain);
    displayMode = DISP_VOLUME;
    tempDisplayUntil = millis() + 2000;
    Serial.printf("Volume down: %.2f\n", currentGain);
    return;
  }

  if (val == "RIGHT") {
    currentGain = min(1.0f, currentGain + 0.25f);
    if (!muted) mp3.setGain(currentGain);
    displayMode = DISP_VOLUME;
    tempDisplayUntil = millis() + 2000;
    Serial.printf("Volume up: %.2f\n", currentGain);
    return;
  }

  if (val == "LOGIN") {
    Serial.println("User logged in");
    return;
  }
}

void notify(BLERemoteCharacteristic *c, const uint8_t *data, uint32_t len) {
  String val;
  for (uint32_t i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (ch == '\r' || ch == '\n') continue;
    val += ch;
  }
  handleCommand(val);
}

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

void setup() {
  Serial.begin(115200);

  Wire.begin();
  alpha4.begin(0x70);
  alpha4.clear();
  alpha4.writeDisplay();

  mp3.begin();
  mp3.setGain(1.0);

  ConnectWiFi();
  startStream();

  BLE.begin();

  Serial.println("System Ready (Audio + BLE + Display active)");
}

void loop() {
  if (pendingStreamSwitch) {
    pendingStreamSwitch = false;
    startStream();
  }

  if (streamActive && (millis() - lastMetaFetch >= META_INTERVAL)) {
    lastMetaFetch = millis();
    fetchMetadata();
  }

  runRadio();
  handleBLE();
  tickDisplay();
}