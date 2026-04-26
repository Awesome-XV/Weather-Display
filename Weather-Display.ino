#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <math.h>

const char* SSID = "SSID";
const char* PASSWORD = "Pass";
const char* OPENWEATHER_KEY = "KEY";
const char* CITY = "City";
const char* UNITS = "imperial";

#define TFT_CS 0 //GPIO 0
#define TFT_DC 1 // GPIO 1
#define TFT_SCLK 6 // GPIO 6
#define TFT_MOSI 7 // GPIO 7
#define TFT_RST 10 // GPIO 10

#define BTN_UP 3
#define BTN_MID 4
#define BTN_DN 5

// Screen Stuff
#define W 160
#define H 128
#define NAVY 0x0841
#define PANEL 0x10A2
#define CYAN 0x07FF
#define WHT 0xFFFF
#define DIM 0x8410
#define YLW 0xFFE0
#define ORNG 0xFB20
#define COLD 0x041F
#define GRNE 0x07E0
#define RED 0xF800

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

int scr = 0;
unsigned long btnTime = 0;
unsigned long lastPull = 0;

float temp, feels, tLow, tHigh;
int humidity, pressure, clouds;
float wind;
int windDeg;
int wxID = 800;
String cityName = "";
String wxDesc = "";
bool gotData = false;

uint16_t tempClr(float t) {
  if (t >= 90) return ORNG;
  if (t >= 70) return YLW;
  if (t >= 50) return GRNE;
  return COLD;
}

String compassDir(int deg) {
  const char* pts[] = {"N","NE","E","SE","S","SW","W","NW"};
  return pts[(int)((deg + 22.5f) / 45.0f) % 8];
}

void iconSun(int cx, int cy) {
  for (int a = 0; a < 360; a += 45) {
    float r = a * PI / 180.0f;
    tft.drawLine(cx + cos(r)*12, cy + sin(r)*12, cx + cos(r)*17, cy + sin(r)*17, YLW);
  }
  tft.fillCircle(cx, cy, 9, YLW);
  tft.fillCircle(cx, cy, 6, ORNG);
}

void iconCloud(int cx, int cy, uint16_t col) {
  tft.fillCircle(cx-6, cy, 7, col);
  tft.fillCircle(cx+6, cy, 7, col);
  tft.fillCircle(cx, cy-5, 7, col);
  tft.fillRect(cx-12, cy, 25, 8, col);
}

void iconRain(int cx, int cy) {
  iconCloud(cx, cy-6, DIM);
  tft.drawLine(cx-7, cy+6, cx-9, cy+14, COLD);
  tft.drawLine(cx, cy+6, cx-2, cy+14, COLD);
  tft.drawLine(cx+7, cy+6, cx+5, cy+14, COLD);
}

void iconSnow(int cx, int cy) {
  iconCloud(cx, cy-6, DIM);
  tft.fillCircle(cx-7, cy+10, 2, WHT);
  tft.fillCircle(cx, cy+12, 2, WHT);
  tft.fillCircle(cx+7, cy+10, 2, WHT);
}

void iconStorm(int cx, int cy) {
  iconCloud(cx, cy-6, DIM);
  tft.drawLine(cx+2, cy+3, cx-5, cy+11, YLW);
  tft.drawLine(cx-5, cy+11, cx+2, cy+11, YLW);
  tft.drawLine(cx+2, cy+11, cx-5, cy+20, YLW);
}

void iconFog(int cx, int cy) {
  for (int i = 0; i < 4; i++)
    tft.drawFastHLine(cx-14, cy-6+i*6, 28, DIM);
}

void iconPartly(int cx, int cy) {
  tft.fillCircle(cx+7, cy-6, 8, YLW);
  tft.fillCircle(cx+7, cy-6, 5, ORNG);
  iconCloud(cx-2, cy+2, DIM);
}

void pickIcon(int cx, int cy) {
  if (wxID >= 200 && wxID < 300) {
    iconStorm(cx, cy);
  }
  else if (wxID >= 300 && wxID < 600) {
    iconRain(cx, cy);
  }
  else if (wxID >= 600 && wxID < 700) {
    iconSnow(cx, cy);
  }
  else if (wxID >= 700 && wxID < 800) {
    iconFog(cx, cy);
  }
  else if (wxID == 800) {
    iconSun(cx, cy);
  }
  else if (wxID <= 802) {
    iconPartly(cx, cy);
  }
  else {
    iconCloud(cx, cy, DIM);
  }
}

void topBar() {
  tft.fillRect(0, 0, W, 14, PANEL);
  tft.drawFastHLine(0, 14, W, CYAN);
  tft.setTextSize(1);
  tft.setTextColor(WHT);
  tft.setCursor(3, 3);
  tft.print(cityName.length() ? cityName : "Weather");

  for (int i = 0; i < 3; i++) {
    int x = W - 20 + i*7;
    if (i == scr) tft.fillCircle(x, 7, 2, CYAN);
    else tft.drawCircle(x, 7, 2, DIM);
  }
}

void miniBar(int x, int y, int bw, int val, int maxv, uint16_t col, const char* lbl) {
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(x, y);
  tft.print(lbl);
  tft.drawRect(x, y+9, bw, 7, DIM);
  int fill = map(val, 0, maxv, 0, bw-2);
  tft.fillRect(x+1, y+10, fill, 5, col);
  tft.setTextColor(WHT);
  tft.setCursor(x+bw+3, y+9);
  tft.print(val); tft.print("%");
}

void drawCompass(int cx, int cy, int r, int deg) {
  tft.drawCircle(cx, cy, r, DIM);
  tft.setTextSize(1); 
  tft.setTextColor(DIM);
  tft.setCursor(cx-3, cy-r+2); tft.print("N");
  tft.setCursor(cx-3, cy+r-9); tft.print("S");
  tft.setCursor(cx+r-7, cy-3); tft.print("E");
  tft.setCursor(cx-r+2, cy-3); tft.print("W");
  float rad = (deg - 90) * PI / 180.0f;
  int ax = cx + cos(rad) * (r-5);
  int ay = cy + sin(rad) * (r-5);
  tft.drawLine(cx, cy, ax, ay, CYAN);
  tft.fillCircle(ax, ay, 2, CYAN);
  tft.fillCircle(cx, cy, 2, WHT);
}

void noDataYet() {
  tft.setTextColor(DIM); tft.setTextSize(1);
  tft.setCursor(22, 56); tft.print("Fetching data...");
}

void drawScr0() {
  tft.fillScreen(NAVY);
  topBar();
  if (!gotData) {
    noDataYet();
    return;
  }
  pickIcon(26, 68);

  // Temp
  tft.setTextColor(tempClr(temp));
  tft.setTextSize(3);
  tft.setCursor(62, 24);
  tft.print((int)temp);
  tft.setTextSize(2);
  tft.print(strcmp(UNITS,"imperial")==0 ? "F" : "C");

  // Feels Like
  tft.setTextSize(1);
  tft.setTextColor(DIM);
  tft.setCursor(62, 58);
  tft.print("feels ");
  tft.setTextColor(WHT);
  tft.print((int)feels);
  tft.print(strcmp(UNITS,"imperial")==0 ? "F" : "C");

  tft.drawFastHLine(4, 83, W-8, PANEL);

  // High and Low
  tft.setTextColor(ORNG); tft.setCursor(4, 89);
  tft.print("H:"); tft.print((int)tHigh);
  tft.setTextColor(COLD); tft.setCursor(52, 89);
  tft.print("L:"); tft.print((int)tLow);

  String d = wxDesc;
  if (d.length()) d[0] = toupper(d[0]);
  tft.setTextColor(DIM);
  tft.setCursor(4, 103);
  tft.print(d.substring(0, 22));
}

// Screen 1, Wind Compass, Speed, Pressure Bar
void drawScr1() {
  tft.fillScreen(NAVY);
  topBar();
  if (!gotData) { 
    noDataYet();
    return;
  }

  drawCompass(38, 75, 30, windDeg);

  tft.setTextSize(1); tft.setTextColor(DIM);
  tft.setCursor(84, 24); tft.print("WIND");

  // wind speed
  uint16_t wc = wind > 25 ? RED : wind > 15 ? ORNG : GRNE;
  tft.setTextSize(2); tft.setTextColor(wc);
  tft.setCursor(84, 36);
  tft.print((int)wind);
  tft.setTextSize(1); tft.setTextColor(DIM);
  tft.print(strcmp(UNITS,"imperial")==0 ? " mph" : " m/s");

  tft.setCursor(84, 60);
  tft.setTextColor(CYAN);
  tft.print(compassDir(windDeg));
  tft.setTextColor(DIM);
  tft.print(" ("); tft.print(windDeg); tft.print((char)247); tft.print(")");

  tft.drawFastHLine(4, 83, W-8, PANEL);

  tft.setTextColor(DIM); tft.setCursor(4, 91);
  tft.print("PRESSURE  ");
  tft.setTextColor(WHT);
  tft.print(pressure); tft.print(" hPa");

  // pressure bar
  tft.drawRect(4, 103, W-8, 7, DIM);
  int px = constrain(map(pressure, 950, 1050, 0, W-10), 0, W-10);
  uint16_t pc = pressure < 1000 ? COLD : pressure > 1020 ? ORNG : GRNE;
  tft.fillRect(5, 104, px, 5, pc);
}

// Screen 2, Humidity, Cloud Cover, Icon stuff
void drawScr2() {
  tft.fillScreen(NAVY);
  topBar();
  if (!gotData) {
    noDataYet();
    return;
  }
  tft.setTextSize(1); tft.setTextColor(CYAN);
  tft.setCursor(4, 21); tft.print("HUMIDITY & CLOUDS");

  uint16_t hc = humidity > 80 ? COLD : humidity > 50 ? GRNE : YLW;
  miniBar(4, 35, 110, humidity, 100, hc, "HUMIDITY");
  miniBar(4, 59, 110, clouds, 100, DIM, "CLOUDS");

  tft.drawFastHLine(4, 83, W-8, PANEL);

  pickIcon(W-26, 104);

  tft.setTextSize(1);
  tft.setTextColor(DIM); tft.setCursor(4, 91);
  tft.print("Humidity:  ");
  tft.setTextColor(hc);
  tft.print(humidity); tft.print("%");

  tft.setTextColor(DIM); tft.setCursor(4, 105);
  tft.print("Clouds:    ");
  tft.setTextColor(WHT);
  tft.print(clouds); tft.print("%");
}

void redraw() {
  if (scr == 0) {
    drawScr0();
  }
  else if (scr == 1) {
    drawScr1();
  }
  else {
    drawScr2();
  }
}

void splash(const char* l1, String l2, uint16_t col) {
  tft.fillScreen(NAVY);
  tft.drawRect(4, 4, W-8, H-8, CYAN);
  tft.setTextSize(1); tft.setTextColor(col);
  tft.setCursor(10, 48); tft.print(l1);
  tft.setTextColor(DIM);
  tft.setCursor(10, 64); tft.print(l2);
}

void pullWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  splash("Refreshing...", "Hold On", CYAN);

  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=";
  url += CITY;
  url += "&units="; url += UNITS;
  url += "&appid="; url += OPENWEATHER_KEY;

  http.begin(url);
  int code = http.GET();

  if(code == HTTP_CODE_OK) {
    String body = http.getString();
    DynamicJsonDocument doc(3072);
    if (!deserializeJson(doc, body)) {
      temp = doc["main"]["temp"];
      feels = doc["main"]["feels_like"];
      tLow = doc["main"]["temp_min"];
      tHigh = doc["main"]["temp_max"];
      humidity = doc["main"]["humidity"];
      pressure = doc["main"]["pressure"];
      wind = doc["wind"]["speed"];
      windDeg = doc["wind"]["deg"] | 0;
      clouds = doc["clouds"]["all"] | 0;
      wxID = doc["weather"][0]["id"] | 800;
      cityName = doc["name"].as<String>();
      wxDesc = doc["weather"][0]["description"].as<String>();
      gotData = true;
    }
  }

  http.end();
  lastPull = millis();
  redraw();
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_MID, INPUT_PULLUP);
  pinMode(BTN_DN, INPUT_PULLUP);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(NAVY);

  tft.drawRect(4, 4, W-8, H-8, CYAN);
  tft.setTextColor(WHT); tft.setTextSize(2);
  tft.setCursor(18, 28); tft.print("Weather");
  tft.setCursor(28, 48); tft.print("Station");
  tft.setTextColor(DIM); tft.setTextSize(1);
  tft.setCursor(34, 72); tft.print("C3 Mini v2.1");
  delay(1200);

  splash("Connecting...", SSID, YLW);
  WiFi.begin(SSID, PASSWORD);
  int d = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    tft.setTextColor(CYAN);
    tft.setCursor(10 + (d % 14)*10, 84);
    tft.print("."); d++;
  }
  splash("Connected!", WiFi.localIP().toString(), GRNE);
  delay(900);

  pullWeather();
}

void loop() {
  unsigned long t = millis();

  if (t - btnTime > 200) {
    if (digitalRead(BTN_UP) == LOW) {
      scr = (scr - 1 + 3) % 3;
      btnTime = t;
      redraw();
    }
    if (digitalRead(BTN_DN) == LOW) {
      scr = (scr + 1) % 3;
      btnTime = t;
      redraw();
    }
    if (digitalRead(BTN_MID) == LOW) {
      btnTime = t;
      pullWeather();
    }
  }

  if (t - lastPull > 300000UL) {
    pullWeather();
  }
}