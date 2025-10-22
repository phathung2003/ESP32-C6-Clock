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

// Thông tin cấu hình Blynk 
#if __has_include("env.h")
  #define BLYNK_SETUP true
  #include "env.h"
#else
  #define BLYNK_SETUP false
  #define BLYNK_TEMPLATE_ID   ""
  #define BLYNK_TEMPLATE_NAME   ""
#endif

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

// ==================== PHOTORESISTOR (Cảm biến ánh sáng) ====================
#define PHOTORESISTOR_AO 3

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

// ==================== BLYNK SETTING ====================
#define SEND_TIMEOUT_MILISECOND  1000
bool blynkSetup = BLYNK_SETUP;
#include <BlynkSimpleEsp32.h>
BlynkTimer timer;

// ==================== SYSTEM BEHAVIOR ====================
bool powerOn = false;                      // True: Hệ thống đang bật | False: Hệ thống đang tắt 
bool alarmEnabled = false;                // True: Bật báo thức | False: Tắt báo thức
bool settingMode = false;                 // True: Đang chỉnh giờ | False: Hiện đồng hồ
bool wifiStatus = false;

// ==================== VARIABLE ====================
#define WEATHER_INFO_DELAY_SECOND 10000   // Thời gian chờ giữa hiện nhiệt độ và độ ẩm
int settingStep = 0;                      // Bước cài đặt

int setH, setM, setS;                     // Giá trị đang chỉnh
int alarmH = 20, alarmM = 23;               // Giá trị báo thức
bool alarmRinging = false;                // True: Chuông đang kêu | False: Chuông đang tắt

bool showTemp = true;                     // True: Hiển thị nhiệu độ | False: Hiển thị độ ẩm 
unsigned long dhtTimer = 0;               // Thời gian chờ chuyển đổi hiện thông tin nhiệt độ và độ ẩm

bool buzzerOn = false;                    // True: Còi đang kêu | False: Còi đang tắt
unsigned long lastBeep = 0;               // Thời gian còi kêu lần cuối

float temperature = 0, humidity = 0;

bool prevAlarmEnabled = false; 
bool prevPowerOn = false; // lưu trạng thái trước
float prevTemperature = 0, prevHuminity = 0;
int prevAlarmH = 6, preAlarmM = 0;
bool firstUpdate = true;
// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);
  Wire.begin(SDA_PIN, SCL_PIN);

  // RTCplatformio run
  if (!rtc.begin()) {
    Serial.println("Không tìm thấy DS1307!");
    while (1);
  }
  if (!rtc.isrunning()) {
    Serial.println("DS1307 chưa chạy, đặt thời gian mới...");
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

  // Blynk
  // WiFi.begin("Wokwi-GUEST", "");
  if(blynkSetup && BLYNK_AUTH_TOKEN != nullptr && strlen(BLYNK_AUTH_TOKEN) > 0){
    Blynk.begin(BLYNK_AUTH_TOKEN,"Wokwi-GUEST", "");
  }
  else{
    blynkSetup = false;
  }

  // timer.setInterval((long)(SEND_TIMEOUT_MILISECOND), sendBlynkData);

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
  checkWifi();
  if(wifiStatus && blynkSetup){
    Blynk.run();  
    timer.run();
    sendBlynkData();
  }
  if (!powerOn){
    digitalWrite(POWER_LED_RED, LOW);
    digitalWrite(WIFI_LED_RED, LOW);
    digitalWrite(WIFI_LED_GREEN, LOW);
    alarmRinging = false;
    ledcWriteTone(BUZZER, 0);
    matrixTime.displayClear();
    matrixDHT.displayClear();
  }
  else{

    digitalWrite(POWER_LED_RED, HIGH);

    changeBrightness();
    showWeather();

    handleSetting();
    if (settingMode) {
      showSettingTime();
    }
    else{
      showTime();
    }
  
    checkAlarm();
    alarmSound();
  }
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

      if(settingStep == 4){
        DateTime now = rtc.now();
        rtc.adjust(DateTime(now.year(), now.month(), now.day(), setH, setM, setS));
      }

      // Bước 4: bật/tắt báo thức
      if (settingStep == 5 && !alarmEnabled) {
        settingStep = 7; // nhảy qua bước lưu
      }

      // Bước 7 = lưu và thoát
      if (settingStep > 6) {
        if(prevAlarmEnabled != alarmEnabled){
          Blynk.virtualWrite(V4, alarmEnabled ? 1 : 0);
          prevAlarmEnabled = alarmEnabled;
        }
        if(prevAlarmH != alarmH){
          Blynk.virtualWrite(V3, alarmH);
          prevAlarmH = alarmH;
        }

        if(preAlarmM != alarmM){
          Blynk.virtualWrite(V4, alarmM);
          preAlarmM = alarmM;
        }
        settingMode = false;
        settingStep = 0;
      }
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

    if(alarmRinging){
      alarmRinging = false;
      ledcWriteTone(BUZZER, 0);
      holding = false;
      prevPowerBtn = btnState;
      return;
    }
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

  if(prevPowerOn != powerOn){
    Blynk.virtualWrite(V0, powerOn ? 1 : 0);
    Blynk.virtualWrite(V1, powerOn ? 1 : 0);
    prevPowerOn = powerOn;
  }
}

// ==================== POWER ====================
void changeBrightness() {
  int ldrValue = analogRead(PHOTORESISTOR_AO);        // Đọc cảm biến ánh sáng (0 [Sáng] - 4095 [Tối])
  int brightness = map(ldrValue, 0, 4095, 15, 0);     // Trời càng tối → Độ sáng càng nhỏ 
  brightness = constrain(brightness, 0, 15);          // Giữ trong khoảng hợp lệ
  
  // Chỉnh độ sáng màn hình
  matrixTime.setIntensity(brightness);
  matrixDHT.setIntensity(brightness);
  
  // Giá trị sensor
  // Serial.print("LDR: ");
  // Serial.print(ldrValue);
  // Serial.print(" | Brightness: ");
  // Serial.println(brightness);
  // delay(1000);
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

    sprintf(disp, "AL%s:%s", h, m);
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
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();

  if(prevTemperature != temperature){
      Blynk.virtualWrite(V5, temperature);
      prevTemperature = temperature;
  }

  if(prevHuminity != humidity){
    Blynk.virtualWrite(V6, humidity);
    prevHuminity = humidity;
  }
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
      if(powerOn){
        digitalWrite(WIFI_LED_GREEN, HIGH);
        digitalWrite(WIFI_LED_RED, LOW);
      }
      else{
        digitalWrite(WIFI_LED_GREEN, LOW);
        digitalWrite(WIFI_LED_RED, LOW);
      }
      wifiStatus = true;
      return;
    }
  } 
  if(powerOn){
      digitalWrite(WIFI_LED_RED, HIGH);
      digitalWrite(WIFI_LED_GREEN, LOW);
  }
  else{
    digitalWrite(WIFI_LED_GREEN, LOW);
    digitalWrite(WIFI_LED_RED, LOW);
  }
  wifiStatus = false;
  return;
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
    buzzerOn = true;
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

// ==================== BLYNK ====================
void sendBlynkData() {
  if(!firstUpdate){
    return;
  }

  Blynk.virtualWrite(V0, powerOn ? 1 : 0);
  Blynk.virtualWrite(V1, powerOn ? 1 : 0);
  Blynk.virtualWrite(V2, alarmEnabled ? 1 : 0);

  Blynk.virtualWrite(V3, alarmH);
  Blynk.virtualWrite(V4, alarmM);
  firstUpdate = false;
  return;
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V0, V1, V2, V3, V4, V5, V6);
}

// Switch bật/tắt báo thức
BLYNK_WRITE(V0) {
  powerOn = param.asInt();  // 1 = Bật, 0 = Tắt
  // Gửi trạng thái ra widget khác (ví dụ Label trên V1)
  Blynk.virtualWrite(V1, powerOn ? 1 : 0);
  prevPowerOn = powerOn;
}

// Switch bật/tắt báo thức
BLYNK_WRITE(V2) {
  alarmEnabled = param.asInt();
  prevAlarmEnabled = alarmEnabled;
  Blynk.virtualWrite(V4, alarmEnabled ? 1 : 0);
}

// Nhập giờ
BLYNK_WRITE(V3) {
  alarmH = param.asInt();
  prevAlarmH = alarmH;
}

// Nhập phút
BLYNK_WRITE(V4) {
  alarmM = param.asInt();
  preAlarmM = alarmM;
}