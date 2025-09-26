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

// ==================== RTC ====================
#define SDA_PIN 8
#define SCL_PIN 9
RTC_DS1307 rtc;

// ==================== BUTTON ====================
#define BTN_POWER   10
#define BTN_SETTING 11
#define BTN_UP      21
#define BTN_DOWN    22

// ==================== VARIABLE ====================
#define WEATHER_INFO_DELAY_SECOND 10000   // Thời gian chờ giữa hiện nhiệt độ và độ ẩm
bool settingMode = false;                 // True: Đang chỉnh giờ | False: Hiện đồng hồ
int settingStep = 0;                      // Bước cài đặt
int setH, setM, setS;                     // Giá trị đang chỉnh
bool showTemp = true;                     // True: Hiển thị nhiệu độ | False: Hiển thị độ ẩm 
unsigned long dhtTimer = 0;               // Thời gian chờ chuyển đổi hiện thông tin nhiệt độ và độ ẩm

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
  handleSetting();
  showTime();
  showWeather();
}

// ==================== SETTING ====================
void handleSetting() {
  static bool prevSettingBtn = false;
  bool settingBtn = digitalRead(BTN_SETTING) == LOW;

  // Nhấn SET để chuyển bước
  if (settingBtn && !prevSettingBtn) {
    settingMode = true;
    settingStep++;
    
    // Bước 3 = lưu và thoát
    if (settingStep > 3) {
      DateTime now = rtc.now(); // Lấy ngày hiện tại
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), setH, setM, setS));
      settingMode = false;
      settingStep = 0;
        Serial.println("Time updated!");
    } 
    // Bắt đầu chỉnh giờ, copy giờ hiện tại
    else if (settingStep == 1) {
      DateTime now = rtc.now();
      setH = now.hour();
      setM = now.minute();
      setS = now.second();
    }
    delay(200);
  }

  prevSettingBtn = settingBtn;

  // Nếu không đang chỉnh hoặc bước không hợp lệ, bỏ qua
  if (!settingMode || settingStep == 0 || settingStep > 3) return;

  // Nút UP tăng giá trị
  if (digitalRead(BTN_UP) == LOW) {
    switch (settingStep) {
      case 1: setH = (setH + 1) % 24; break;
      case 2: setM = (setM + 1) % 60; break;
      case 3: setS = (setS + 1) % 60; break;
    }
    delay(150);
  }

  // Nút DOWN giảm giá trị
  if (digitalRead(BTN_DOWN) == LOW) {
    switch (settingStep) {
        case 1: setH = (setH + 23) % 24; break;  // +23 mod 24 = -1
        case 2: setM = (setM + 59) % 60; break;  // +59 mod 60 = -1
        case 3: setS = (setS + 59) % 60; break;  // +59 mod 60 = -1
      }
    delay(150);
  }
}


// ==================== SHOW TIME ====================
void showTime() {
  static unsigned long lastBlink = 0;
  static bool blink = false;
  char timeStr[9];
  bool showColon = true;
  
  // Toggle blink every 500ms
  if (millis() - lastBlink >= 500) {
    lastBlink = millis();
    blink = !blink;
  }

  int displayH, displayM, displayS;

  if (settingMode && settingStep >= 1 && settingStep <= 3) {
    displayH = setH;
    displayM = setM;
    displayS = setS;
  } 
  else {
    DateTime now = rtc.now();
    displayH = now.hour();
    displayM = now.minute();
    displayS = now.second();
  }

  char hourStr[3], minStr[3], secStr[3];
  // Sao chép số đang chỉnh
  sprintf(hourStr,"%02d",displayH);
  sprintf(minStr,"%02d",displayM);
  sprintf(secStr,"%02d",displayS);

  // nếu đang nháy, dùng ký tự ' ' nhưng vẽ bằng intensity 0
  if(settingMode && blink){
    switch(settingStep){
        case 1: // Nháy giờ
            sprintf(hourStr, "  "); // 2 khoảng trắng
            break;
        case 2: // Nháy phút
            sprintf(minStr, "  ");
            break;
        case 3: // Nháy giây
            sprintf(secStr, "  ");
            break;
    }
  }

  // Ghép thành hh:mm:ss
  sprintf(timeStr, "%s:%s:%s", hourStr, minStr, secStr);

  // Hiển thị lên matrix
  matrixTime.displayText(timeStr, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  matrixTime.displayReset();

  // Nếu không chỉnh, toggle colon
  if (!settingMode) showColon = !showColon;

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
