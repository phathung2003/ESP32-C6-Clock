#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <Wire.h>
#include "RTClib.h"
#include <DHT.h>
#include "FontSegment.h"  
#include "driver/ledc.h"
#include <WiFi.h>
#include <WiFiClient.h>
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

// ==================== RTC ====================
#define SDA_PIN 8
#define SCL_PIN 9
RTC_DS1307 rtc;

// ==================== BUTTON ====================
#define BTN_POWER   10
#define BTN_SETTING 11
#define BTN_UP      21
#define BTN_DOWN    22

// ==================== BUTTON ====================
#define BUZZER 15

// ==================== LED RGB ====================
#define POWER_LED_RED   12
#define WIFI_LED_RED    18
#define WIFI_LED_GREEN  13
// ==================== SYSTEM BEHAVIOR ====================
bool powerOn = true;                      // True: Hệ thống đang bật | False: Hệ thống đang tắt 
bool alarmEnabled = false;                // True: Bật báo thức | False: Tắt báo thức
bool settingMode = false;                 // True: Đang chỉnh giờ | False: Hiện đồng hồ

// ==================== VARIABLE ====================
#define WEATHER_INFO_DELAY_SECOND 10000   // Thời gian chờ giữa hiện nhiệt độ và độ ẩm
int settingStep = 0;                      // Bước cài đặt

int setH, setM, setS;                     // Giá trị đang chỉnh
int alarmH = 6, alarmM = 00;              // Giá trị báo thức
bool alarmRinging = false;                // True: Chuông đang kêu | False: Chuông đang tắt

bool showTemp = true;                     // True: Hiển thị nhiệu độ | False: Hiển thị độ ẩm 
unsigned long dhtTimer = 0;               // Thời gian chờ chuyển đổi hiện thông tin nhiệt độ và độ ẩm

bool buzzerOn = false;                    // True: Còi đang kêu | False: Còi đang tắt
unsigned long lastBeep = 0;               // Thời gian còi kêu lần cuối

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

  // Buzzer
  pinMode(BUZZER, OUTPUT);
  ledcAttach(BUZZER, 2000, 8);
  
  // Button
  pinMode(BTN_POWER, INPUT_PULLUP); 
  pinMode(BTN_SETTING, INPUT_PULLUP); 
  pinMode(BTN_UP, INPUT_PULLUP); 
  pinMode(BTN_DOWN, INPUT_PULLUP); 

  // LED RGB
  pinMode(POWER_LED_RED, OUTPUT);
  pinMode(WIFI_LED_RED, OUTPUT);
  pinMode(WIFI_LED_GREEN, OUTPUT);

  // Khởi tạo màn hình
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
  handlePowerButton();

  if (!powerOn){
    digitalWrite(POWER_LED_RED, LOW);
    digitalWrite(WIFI_LED_RED, LOW);
    digitalWrite(WIFI_LED_GREEN, LOW);
    return;
  }

  digitalWrite(POWER_LED_RED, HIGH);
  checkWifi();
  
  handleSetting();
  if (settingMode) {
     showSettingTime();
  }
  else{
     showTime();
  }

  showWeather();
  checkAlarm();
  alarmSound();
  
}

// ==================== SETTING ====================
void handleSetting() {
  static bool prevSettingBtn = false;
  static unsigned long pressStartTime = 0;
  static bool holding = false;

  bool btnState = digitalRead(BTN_SETTING) == LOW;

  // --- Khi bắt đầu nhấn ---
  if (btnState && !prevSettingBtn) {
    pressStartTime = millis();
    holding = true;

    if(alarmRinging){
      alarmRinging = false;
      ledcWriteTone(BUZZER, 0);
      holding = false;
      prevSettingBtn = btnState;
      return;
    }
  }

  // --- Khi đang nhấn giữ ---
  if (btnState && holding && !settingMode) {
    unsigned long pressDuration = millis() - pressStartTime;

    // Giữ >= 5s => vào chế độ chỉnh giờ
    if (pressDuration >= 5000) {
      settingMode = true;
      settingStep = 1;

      DateTime now = rtc.now();
      setH = now.hour();
      setM = now.minute();
      setS = now.second();

      holding = false;
    }
  }

  // --- Khi nhả nút ---
  if (!btnState && prevSettingBtn) {
    unsigned long pressDuration = millis() - pressStartTime;

    // Nếu báo thức đang kêu -> nhấn nhanh để tắt
    if (alarmRinging && pressDuration < 1000) {
      alarmRinging = false;
      digitalWrite(BUZZER, LOW);
      holding = false;
      prevSettingBtn = btnState;
      return;
    }

    // Nếu đang ở chế độ chỉnh => nhấn ngắn để chuyển bước
    if (settingMode && pressDuration < 1000) {
      settingStep++;

      // Bước 4: bật/tắt báo thức
      // Nếu báo thức đang tắt → bỏ qua bước chỉnh giờ báo thức
      if (settingStep == 5 && !alarmEnabled) {
        settingStep = 7; // nhảy qua bước lưu
      }

      // Bước 7 = lưu và thoát
      if (settingStep > 6) {
        DateTime now = rtc.now();
        rtc.adjust(DateTime(now.year(), now.month(), now.day(), setH, setM, setS));
        settingMode = false;
        settingStep = 0;
      }
      delay(200);
    }

    holding = false;
  }

  prevSettingBtn = btnState;

  // Nếu không đang chỉnh => Thoát
  if (!settingMode || settingStep == 0 || settingStep > 6) return;

  // --- UP / DOWN ---
  if (digitalRead(BTN_UP) == LOW) {
    switch (settingStep) {
      case 1: setH = (setH + 1) % 24; break;
      case 2: setM = (setM + 1) % 60; break;
      case 3: setS = (setS + 1) % 60; break;
      case 4: alarmEnabled = !alarmEnabled; break;
      case 5: alarmH = (alarmH + 1) % 24; break;
      case 6: alarmM = (alarmM + 1) % 60; break;
    }
    delay(150);
  }

  if (digitalRead(BTN_DOWN) == LOW) {
    switch (settingStep) {
      case 1: setH = (setH + 23) % 24; break;
      case 2: setM = (setM + 59) % 60; break;
      case 3: setS = (setS + 59) % 60; break;
      case 4: alarmEnabled = !alarmEnabled; break;
      case 5: alarmH = (alarmH + 23) % 24; break;
      case 6: alarmM = (alarmM + 59) % 60; break;
    }
    delay(150);
  }
}


// ==================== POWER ====================
void handlePowerButton() {
  static bool prevPowerBtn = false;
  static unsigned long pressStart = 0;
  static bool holding = false;

  bool btnState = digitalRead(BTN_POWER) == LOW;

  // Khi mới bắt đầu nhấn
  if (btnState && !prevPowerBtn) {
    pressStart = millis();
    holding = true;
  }

  // Khi đang giữ nút
  if (btnState && holding) {
    unsigned long duration = millis() - pressStart;

    // Đổi trang thái nguồn
    if (duration >= 3000) {
      powerOn = !powerOn;
      holding = false;
      delay(200);

      if (!powerOn) {
        // Khi tắt nguồn
        alarmRinging = false;
        ledcWriteTone(BUZZER, 0);
        matrixTime.displayClear();
        matrixDHT.displayClear();

      }
    }
  }

  // Khi nhả nút ra
  if (!btnState && prevPowerBtn) {
    holding = false;
  }

  prevPowerBtn = btnState;
}

// ==================== SHOW SETTING ====================
void showSettingTime() {
  static unsigned long lastBlink = 0;
  static bool blink = false;
  
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    blink = !blink;
  }

  char disp[12];

  // Chỉnh thời gian hệ thống
  if (settingStep >= 1 && settingStep <= 3) {
    char h[3], m[3], s[3];
    sprintf(h, "%02d", setH);
    sprintf(m, "%02d", setM);
    sprintf(s, "%02d", setS);

    if (blink) {
      if (settingStep == 1) sprintf(h, "  ");
      else if (settingStep == 2) sprintf(m, "  ");
      else if (settingStep == 3) sprintf(s, "  ");
    }

    sprintf(disp, "%s:%s:%s", h, m, s);
  }
  // Bật | Tắt báo thức
  else if (settingStep == 4) {
    sprintf(disp, "AL %s", alarmEnabled ? "ON " : "OFF");
  }
  // Chỉnh giờ báo thức (Nếu báo thức bật)
  else if (settingStep == 5 || settingStep == 6) {
    char h[3], m[3];
    sprintf(h, "%02d", alarmH);
    sprintf(m, "%02d", alarmM);

    if (blink) {
      if (settingStep == 5) sprintf(h, "  ");
      else sprintf(m, "  ");
    }

    sprintf(disp, "AL %s:%s", h, m);
  }

  matrixTime.displayText(disp, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  matrixTime.displayReset();
  matrixTime.displayAnimate();
}

// ==================== SHOW TIME ====================
void showTime() {
  DateTime now = rtc.now();
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  matrixTime.displayText(timeStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  matrixTime.displayReset();
  matrixTime.displayAnimate();
}

// ==================== SHOW WEATHER ====================
void showWeather() {

  if (millis() - dhtTimer < WEATHER_INFO_DELAY_SECOND) {
    return;
  }

  matrixDHT.setFont(FontSegment);
  dhtTimer = millis();
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  char dhtStr[20];

  // Không đọc được thông tin
  if (isnan(temperature) && isnan(humidity)) {
    sprintf(dhtStr, "Sensor Err");
  }
    
  // Hiển thị nhiệt độ
  if (showTemp) {
    sprintf(dhtStr, "%.1f%cC", temperature, 223);
  } 
  // Hiển thị độ ẩm (40%) 
  else {
    matrixDHT.setFont(nullptr);
    sprintf(dhtStr, "%.0f%%UR", humidity);
  }
    
  // Đổi thông tin hiển thị
  showTemp = !showTemp;
  
  // Hiển thị thông tin
  matrixDHT.displayClear();
  matrixDHT.displayText(dhtStr, PA_CENTER, 5000, WEATHER_INFO_DELAY_SECOND, PA_PRINT, PA_SCROLL_UP);
  matrixDHT.displayAnimate();
}

// ==================== CHECK WIFI ====================
void checkWifi(){
 if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    if (client.connect("www.google.com", 80)) {
      client.stop();
      digitalWrite(WIFI_LED_GREEN, HIGH);
      digitalWrite(WIFI_LED_RED, LOW);
    } else {
      digitalWrite(WIFI_LED_RED, HIGH);
      digitalWrite(WIFI_LED_GREEN, LOW);
    }
  } else {
    digitalWrite(WIFI_LED_RED, HIGH);
    digitalWrite(WIFI_LED_GREEN, LOW);
  }
}

// ==================== CHECK ALARM ====================
void checkAlarm() {
  // Tắt báo thức
  if (!alarmEnabled || alarmRinging) {
    return;
  }

  DateTime now = rtc.now();

  // Tới giờ báo thức
  if (now.hour() == alarmH && now.minute() == alarmM && now.second() == 0) {
    alarmRinging = true;
    buzzerOn = false;
  }
}

void alarmSound() {
  if (!alarmRinging) return;

  unsigned long now = millis();
  
  // Tạo tiếng bíp mỗi 500 ms
  if (now - lastBeep > 500) {
    lastBeep = now;
    buzzerOn = !buzzerOn;

    if (buzzerOn)
      ledcWriteTone(BUZZER, 1000);
    else
      ledcWriteTone(BUZZER, 0);
  }
}