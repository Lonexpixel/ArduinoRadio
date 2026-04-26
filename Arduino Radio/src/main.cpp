
 
#include <I2S.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <BackgroundAudio.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"
 
#define BUZZ_PIN 5
#define BIG_BTN  22
#define NEO_PIN 19
#define NEO_COUNT 5
#define STREAMBUFF (16 * 1024)
 
const char *ssid = "MUSTANG";
const char *pass = "";
 
I2S audio(OUTPUT, 26, 21);
BackgroundAudioMP3Class<RawDataBuffer<STREAMBUFF>> mp3(audio);
 
// *** FIX: Use a pointer instead of a global instance ***
// The old "WiFiClientSecure client;" is replaced with a pointer.
// A fresh instance is allocated each time we reconnect, which prevents
// stale TLS session state from breaking connections on channel switches.
WiFiClientSecure *client = nullptr;
 
String urls[] = {
  "https://pureplay.cdnstream1.com/6021_128.mp3",
  "https://live.amperwave.net/direct/townsquare-ktrsfmmp3-ibc3.mp3",
  "https://uzic.ice.infomaniak.ch/uzic-128.mp3"
};
float gains[] = {1.0, 0.3, 0.3, 0.3};
 
Adafruit_NeoPixel strip(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
uint32_t colors[] = {
  strip.Color(255,255,0),
  strip.Color(255,0,255),
  strip.Color(0,255,255),
  strip.Color(120,255,85)
};
 
Adafruit_AlphaNum4 alpha4 = Adafruit_AlphaNum4();
 
int nURLs = 3;
int urlIndex = 0;
String url = urls[urlIndex];
HTTPClient http;
uint8_t buff[512];
WebServer web(80);
 
int icyMetaInt = 0;
int icyDataLeft = 0;
int icyMetadataLeft = 0;
int gain = 100;
String status;
String title;
 
// --- LED display scrolling state ---
String displayTitle = "";
int scrollPos = 0;
uint32_t lastScrollTime = 0;
#define SCROLL_DELAY_MS 300
char displaybuffer[4] = {' ', ' ', ' ', ' '};
 
void updateDisplayScroll() {
  if (displayTitle.length() == 0) return;
  uint32_t now = millis();
  if (now - lastScrollTime < SCROLL_DELAY_MS) return;
  lastScrollTime = now;
 
  // Pad with spaces so the text scrolls cleanly on and off
  String padded = "    " + displayTitle + "    ";
  int len = padded.length();
 
  for (int i = 0; i < 4; i++) {
    int idx = (scrollPos + i) % len;
    alpha4.writeDigitAscii(i, padded[idx]);
  }
  alpha4.writeDisplay();
 
  scrollPos = (scrollPos + 1) % len;
}
 
void runDisplayStartup() {
  alpha4.writeDigitRaw(3, 0x0);
  alpha4.writeDigitRaw(0, 0xFFFF);
  alpha4.writeDisplay();
  delay(200);
  alpha4.writeDigitRaw(0, 0x0);
  alpha4.writeDigitRaw(1, 0xFFFF);
  alpha4.writeDisplay();
  delay(200);
  alpha4.writeDigitRaw(1, 0x0);
  alpha4.writeDigitRaw(2, 0xFFFF);
  alpha4.writeDisplay();
  delay(200);
  alpha4.writeDigitRaw(2, 0x0);
  alpha4.writeDigitRaw(3, 0xFFFF);
  alpha4.writeDisplay();
  delay(200);
  alpha4.clear();
  alpha4.writeDisplay();
}
 
void ConnectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid);
  while (!WiFi.isConnected()) {
    Serial.print("..");
    delay(100);
  }
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  web.begin();
}
 
void runRadio();
void updateDisplayScroll();
void runDisplayStartup();
void ConnectWiFi();
 
void setup() {
  Serial.begin(115200);
  pinMode(BUZZ_PIN, OUTPUT);
  pinMode(BIG_BTN, INPUT_PULLUP);
 
  strip.begin();
  strip.setPixelColor(0, strip.Color(255,0,0));
  strip.show();
 
  alpha4.begin(0x70);
  runDisplayStartup();
 
  delay(3000);
  Serial.println("Starting web radio demo...");
 
  // *** FIX: No longer calling client.setInsecure() here.
  // setInsecure() is now called on the freshly allocated client inside runRadio().
  mp3.begin();
 
  if (!WiFi.isConnected()) {
    ConnectWiFi();
  }
 
  strip.setPixelColor(0, strip.Color(0,255,0));
  strip.setPixelColor(1, strip.Color(255,0,0));
  strip.show();
 
  displayTitle = "READY";
  scrollPos = 0;
}
 
int mode = 0;
 
void loop() {
  static uint8_t lastBigBtn = LOW;
 
  uint8_t curBigBtn = digitalRead(BIG_BTN);
 
  if (!curBigBtn && (curBigBtn != lastBigBtn)) {
    mode = 1;
    delay(300);
    Serial.println("BIG button pressed");
    urlIndex = (urlIndex + 1) % nURLs;
    mp3.setGain(gains[urlIndex]);
    url = urls[urlIndex];
    strip.setPixelColor(2, colors[urlIndex]);
    strip.setPixelColor(1, strip.Color(0,255,0));
    strip.show();
 
    // End the HTTP connection. The fresh WiFiClientSecure will be
    // allocated in runRadio() on the next iteration.
    http.end();
 
    displayTitle = "LOADING...";
    scrollPos = 0;
  }
 
  if (mode == 1) {
    runRadio();
  }
 
  updateDisplayScroll();
 
  lastBigBtn = curBigBtn;
}
 
void runRadio() {
  if (!WiFi.isConnected()) {
    ConnectWiFi();
    return;
  }
 
  if (!http.connected()) {
    Serial.printf("(Re)connecting to '%s'...\n", url.c_str());
    http.end();
 
    // *** FIX: Always destroy the old TLS client and allocate a fresh one.
    // Reusing the same WiFiClientSecure instance across HTTPS connections
    // leaves stale mbedTLS session state that silently breaks reconnects.
    if (client != nullptr) {
      delete client;
      client = nullptr;
    }
    client = new WiFiClientSecure();
    client->setInsecure();
 
    http.begin(*client, url);
    // *** FIX: Disable connection reuse so each channel switch gets a clean
    // TCP + TLS handshake with no leftover socket state.
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    const char *icyHdrs[] = { "icy-metaint" };
    http.collectHeaders(icyHdrs, 1);
    http.addHeader("Icy-MetaData", "1");
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
      http.end();
      Serial.printf("Can't GET: '%s'\n", url.c_str());
      delay(1000);
      return;
    }
    if (http.hasHeader("icy-metaint")) {
      icyMetaInt = http.header("icy-metaint").toInt();
      icyDataLeft = icyMetaInt;
    } else {
      icyMetaInt = 0;
    }
  }
 
  WiFiClient *stream = http.getStreamPtr();
  do {
    size_t httpavail = stream->available();
    httpavail = std::min(sizeof(buff), httpavail);
    size_t mp3avail = mp3.availableForWrite();
    if (!httpavail || !mp3avail) {
      break;
    }
    size_t toRead = std::min(mp3avail, httpavail);
    if (icyMetaInt) {
      toRead = std::min(toRead, (size_t)icyDataLeft);
    }
    int read = stream->read(buff, toRead);
    if (read < 0) {
      return;
    }
    mp3.write(buff, read);
 
    if (mp3.available() < 1024) {
      mp3.pause();
    } else if (mp3.paused() && mp3.available() > (STREAMBUFF / 2)) {
      mp3.unpause();
    }
 
    icyDataLeft -= read;
    if (icyMetaInt && !icyDataLeft) {
      while (!stream->available() && stream->connected()) {
        delay(1);
      }
      if (!stream->connected()) {
        return;
      }
      int totalCnt = stream->read() * 16;
      int cnt = totalCnt;
 
      int buffCnt = std::min(sizeof(buff), (size_t)cnt);
      uint8_t *p = buff;
      while (buffCnt && stream->connected()) {
        read = stream->read(p, buffCnt);
        p += read;
        buffCnt -= read;
        cnt -= read;
      }
 
      while (cnt && stream->connected()) {
        stream->read();
        cnt--;
      }
 
      if (totalCnt) {
        buff[std::min(sizeof(buff) - 1, (size_t)totalCnt)] = 0;
        Serial.printf("md: '%s'\n", buff);
        char *titlestr = strstr((const char *)buff, "StreamTitle='");
        if (titlestr) {
          titlestr += 13;
          char *end = strchr(titlestr, ';');
          if (end) {
            *(end - 1) = 0;
          }
          title = titlestr;
 
          // Update the LED display with the new track title
          displayTitle = title;
          scrollPos = 0;
        }
      }
      icyDataLeft = icyMetaInt;
    }
  } while (true);
}