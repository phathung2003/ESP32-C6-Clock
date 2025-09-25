#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <Wire.h>
#include "RTClib.h"
#include <DHT.h>
#include "FontSegment.h"  
// ==================== MATRIX LED ====================
#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define MAX_DEVICES   5 

#define DATA_PIN  4
#define CLK_PIN   6

// Màn hình 1 - Hiển thị thời gian
#define CS_PIN_1 5
MD_Parola matrixTime = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN_1, MAX_DEVICES);

// Màn hình 2 - Hiển thị nhiệt độ, độ ẩm
#define CS_PIN_2 7
MD_Parola matrixDHT = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN_2, MAX_DEVICES);

// ==================== DHT22 ====================
#define DHTPIN 23
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
unsigned long dhtTimer = 0;
float t = 0, h = 0;
#define TIMEDHT 5000  // cập nhật 5 giây

// ==================== RTC ====================
#define SDA_PIN 8
#define SCL_PIN 9
RTC_DS1307 rtc;
char timeStr[9];
bool showColon = true;
unsigned long lastUpdate = 0;
bool showTemp = true;

// ==================== BUTTON ====================
#define BTN_POWER   10
#define BTN_SETTING 11
#define BTN_UP      21
#define BTN_DOWN    22

const int NUM_BUTTONS = 4;
const int buttonPins[NUM_BUTTONS] = {BTN_POWER, BTN_SETTING, BTN_UP, BTN_DOWN}; 
unsigned long lastPressTime[NUM_BUTTONS] = {0, 0, 0, 0};
bool isPressed[NUM_BUTTONS] = {false, false, false, false};
const unsigned long debounceDelay = 100;

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);
  Wire.begin(SDA_PIN, SCL_PIN);

  // RTC
  if (!rtc.begin()) {
    Serial.println("Không tìm thấy DS1307!");
    while (1);
  }
  if (!rtc.isrunning()) {
    Serial.println("DS1307 chưa chạy, set thời gian mới...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // DHT22
  dht.begin();

  // Button
  pinMode(BTN_SETTING, INPUT_PULLUP); 
  pinMode(BTN_UP, INPUT_PULLUP); 
  pinMode(BTN_DOWN, INPUT_PULLUP); 

  // Khởi tạo Matrix
  matrixTime.begin();
  matrixTime.setIntensity(5);
  matrixTime.displayClear();
  matrixTime.setFont(FontSegment);

  matrixDHT.begin();
  matrixDHT.setIntensity(5);
  matrixDHT.displayClear();
  matrixDHT.setFont(FontSegment);

}


// ==================== LOOP ====================
void loop() {

  checkButtons();
  // ================= HIỂN THỊ GIỜ =================
if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    DateTime now = rtc.now();

    // Print raw to Serial for debugging (with seconds)

    // Format time (hh:mm:ss) với leading zero và blinking colon ở giữa
    sprintf(timeStr, "%02d%c%02d%c%02d", 
            now.hour(), showColon ? ':' : ' ', 
            now.minute(), showColon ? ':' : ' ', 
            now.second());

    // Update display
    matrixTime.displayText(timeStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
    matrixTime.displayReset();
    // Ký hiệu độ C (°)
    uint8_t degC[] = { 6, 3, 3, 56, 68, 68, 68 };
    matrixDHT.addChar('$', degC);

    // Toggle colon blink
    showColon = !showColon;
}

  showWeather();
  // ================= ANIMATE =================
  matrixTime.displayAnimate();
  matrixDHT.displayAnimate();
}



void checkButtons() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool buttonState = digitalRead(buttonPins[i]);

    if (buttonState == LOW && !isPressed[i] && (millis() - lastPressTime[i] > debounceDelay)) {
      Serial.print("Button ");
      Serial.print(i);
      Serial.println(" pressed!");
      isPressed[i] = true;
      lastPressTime[i] = millis();
    }

    // Reset khi thả nút
    if (buttonState == HIGH) {
      isPressed[i] = false;
    }
  }
}

void showWeather(){
  if (millis() - dhtTimer >= TIMEDHT) {
    dhtTimer = millis();
    t = dht.readTemperature();
    h = dht.readHumidity();

    char dhtStr[20];
    if (!isnan(t) && !isnan(h)) {
      if (showTemp) {
        // Hiển thị nhiệt độ (20°C)
        sprintf(dhtStr, "%.1f$", t, 223); // 223 = ký hiệu °
      } 
      else {
        // Hiển thị độ ẩm (40%) 
        // chuyển sang font mặc định để % hiển thị được
        matrixDHT.setFont(nullptr);
        sprintf(dhtStr, "%.0f%%UR", h);
      }

      Serial.print("Display DHT -> ");
      Serial.println(dhtStr);

      matrixDHT.displayClear();
      matrixDHT.displayText(dhtStr, PA_CENTER, 2000, TIMEDHT, PA_PRINT, PA_SCROLL_UP);
      matrixDHT.displayAnimate();
    } 
    else {
      sprintf(dhtStr, "Sensor Err");
      Serial.println("DHT22: Error reading sensor!");
    }
    showTemp = !showTemp;
  }
}