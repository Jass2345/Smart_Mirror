#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ==========================
// 핀 설정
// ==========================
#define PIR_PIN 4
#define LED_PIN 6   // WS2812 대신 테스트용 (단순 LED)

// ==========================
// LCD (예시 주소 0x27)
// ==========================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================
// 상태 변수
// ==========================
String currentStatus = "IDLE";

// ==========================
// SETUP
// ==========================
void setup() {
  Serial.begin(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Smart Mirror");
}

// ==========================
// LOOP
// ==========================
void loop() {

  // 1. PIR 감지 → ESP32 전송
  if (digitalRead(PIR_PIN) == HIGH) {
    Serial.println("PIR|DETECTED");
    delay(1000); // 중복 방지
  }

  // 2. ESP32 → UNO 데이터 수신
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    handleMessage(line);
  }
}

// ==========================
// 메시지 처리
// ==========================
void handleMessage(String msg) {

  int sep = msg.indexOf('|');
  if (sep == -1) return;

  String tag = msg.substring(0, sep);
  String value = msg.substring(sep + 1);

  if (tag == "STATUS") {
    currentStatus = value;
    updateLED(value);
    updateLCD(value);
  }

  else if (tag == "TIME") {
    lcd.setCursor(0, 1);
    lcd.print("Time: " + value + "   ");
  }

  else if (tag == "WEATHER") {
    lcd.setCursor(0, 0);
    lcd.print(value + "   ");
  }

  else if (tag == "GEMINI") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("AI:");
    lcd.setCursor(0, 1);
    lcd.print(value.substring(0, 16));
  }
}

// ==========================
// LED 상태 표시
// ==========================
void updateLED(String status) {
  if (status == "GEMINI_WAITING") {
    digitalWrite(LED_PIN, LOW);
  }
  else if (status == "GEMINI_LISTENING") {
    digitalWrite(LED_PIN, HIGH);
  }
  else if (status == "GEMINI_THINKING") {
    analogWrite(LED_PIN, 128);
  }
  else if (status == "GEMINI_SPEAKING") {
    analogWrite(LED_PIN, 255);
  }
  else if (status == "SLEEP") {
    digitalWrite(LED_PIN, LOW);
  }
}

// ==========================
// LCD 상태 표시
// ==========================
void updateLCD(String status) {
  lcd.setCursor(0, 0);

  if (status == "GEMINI_WAITING") {
    lcd.print("Ready          ");
  }
  else if (status == "GEMINI_LISTENING") {
    lcd.print("Listening...   ");
  }
  else if (status == "GEMINI_THINKING") {
    lcd.print("Thinking...    ");
  }
  else if (status == "GEMINI_SPEAKING") {
    lcd.print("Speaking...    ");
  }
  else if (status == "SLEEP") {
    lcd.print("Sleep          ");
  }
}