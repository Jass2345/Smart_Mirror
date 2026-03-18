/*
 * Project: HolyCow Smart Mirror - FINAL PRODUCTION (TTS Final Stability)
 * Hardware: ESP32-S3 N16R8 (OPI PSRAM / QIO Flash 80MHz)
 * Board Lib: 2.0.17 | Audio Lib: 2.0.0
 * Model: Gemini 2.5 Flash (Explicit Model Name) + Whisper
 *
 * [⚡ 최종 수정 사항]
 * 1. TTS 안정화: audio.connecttospeech() 호출 후 2초간 강제 대기 유지.
 * 2. 429 Retry FIX: 안전한 반복문 로직 유지.
 * 3. TTS 재생 루프 내 WDT 피딩 (feedWDT()) 및 delay(1) 추가 유지.
 * 4. [⭐ 최종 TTS 버퍼링] connectionTimeout을 (5000, 30000)으로 대폭 확장.
 * 5. [⭐ 최종 보강] Gemini API 완료 후, TTS 스트림 시작 전에 1초 딜레이(delay(1000))를 추가하여 TCP/SSL 자원 정리 시간을 확보.
 * 6. [🔧 5초 끊김 해결] Gemini 프롬프트에 길이 제한 추가 (Google TTS 글자수 제한 회피) 및 볼륨 12로 하향 조정 (전력 피크 완화).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "Audio.h"      
#include <driver/i2s.h> 
#include "time.h"

// [필수] 저전압 감지기 해제
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// WDT 관련 헤더
#include "esp_task_wdt.h" 

// ==========================================
// 1. CONFIGURATION
// ==========================================
const char* ssid     = "***";
const char* password = "***";

// API Keys (사용자 제공값 유지)
const char* geminiKey  = "***"; 
const char* weatherKey = "***"; 
const char* sttKey     = "***"; 

const char* city    = "Seoul"; 
const char* country = "KR";

// 음성 감지 민감도 (오프셋 제거 후 1000~1500 권장)
#define VOICE_THRESHOLD 1500 

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
// LCD
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_SDA  13 
#define TFT_SCL  14 

// Mic (I2S_NUM_1)
#define I2S_MIC_SCK 40
#define I2S_MIC_SD  41
#define I2S_MIC_WS  42
#define I2S_MIC_PORT I2S_NUM_1 

// Speaker (I2S_NUM_0)
#define I2S_SPK_BCLK  5
#define I2S_SPK_LRC   4  
#define I2S_SPK_DIN   7  // ★ 7번 (안전)

// UART
#define UART_RX  16
#define UART_TX  17

// ==========================================
// 3. SYSTEM SETTINGS
// ==========================================
#define SAMPLE_RATE 16000
#define RECORD_TIME 4        // 5초 -> 4초로 단축 (STT 부하 감소)
#define ACTIVE_TIMEOUT 300000 // 5분

Adafruit_GC9A01A tft = Adafruit_GC9A01A(TFT_CS, TFT_DC, TFT_RST);
Audio audio; 

enum State { SLEEP, IDLE, LISTENING, THINKING, SPEAKING, ERROR_STATE };
State currentState = IDLE;

unsigned long lastTimeUpdate = 0;
unsigned long lastWeatherUpdate = 0;
unsigned long lastActivityTime = 0; 
String currentScreenMsg = ""; 

// 날씨 정보 전역 변수
String globalWeather = "맑음";
String globalTemp = "20.0";

// 마이크 상태
bool isMicOn = false;

// Colors
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_WARM   0xFFE0 
#define C_ORANGE 0xFD20 
#define C_YELLOW 0xFFE0 
#define C_BLUE   0x001F 
#define C_GREEN  0x07E0 
#define C_RED    0xF800 

// 함수 선언
void initWiFi();
void initTime();
void getRealWeather();
void sendUART(String tag, String value);
void drawEye(uint16_t color, String msg, bool force = false);
void processConversation();
void setupMic();
void uninstallMic();
bool recordAudio(int16_t **buffer, size_t *size);
String sendToDeepgram(int16_t *buffer, size_t size);
String askGemini(String text);
void checkActivityTimer();
void wakeUpSystem();
float detectSoundLevel(); 

// WDT 피딩 함수 추가
void feedWDT() {
  esp_task_wdt_reset();
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  setCpuFrequencyMhz(240); 

  Serial.begin(115200);
  
  // ★ [수정] 시리얼 모니터 연결 대기 (초기 로그 확보)
  unsigned long waitStart = millis();
  while (!Serial && (millis() - waitStart < 6000)) { // 6초 대기
    delay(10);
  }
  Serial.println("\n\n=================================================");
  Serial.println(">> System Starting (TTS Stability Fix)...");
  Serial.println("=================================================");
  
  Serial1.begin(9600, SERIAL_8N1, UART_RX, UART_TX);

  delay(1000); 

  // LCD Init
  SPI.begin(TFT_SCL, -1, TFT_SDA, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  
  tft.sendCommand(0x11); delay(150); 
  tft.sendCommand(0x29); delay(50);
  uint8_t param = 0xFF; tft.sendCommand(0x51, &param, 1); 
  param = 0x2C; tft.sendCommand(0x53, &param, 1); 
  param = 0x00; tft.sendCommand(0x55, &param, 1); 

  tft.fillScreen(C_BLACK); 
  drawEye(C_BLUE, "Booting...", true);
  
  delay(1000); 

  initWiFi();
  initTime();
  
  // Audio Init
  audio.setPinout(I2S_SPK_BCLK, I2S_SPK_LRC, I2S_SPK_DIN);
  
  // ★ [수정] 볼륨 하향: 15 -> 12 (전력 피크 완화)
  audio.setVolume(12); 
  
  // ⭐ [수정] TTS 버퍼링 타임아웃 대폭 확장 (5초, 30초)
  audio.setConnectionTimeout(5000, 30000); 
  
  delay(500);
  audio.connecttospeech("시스템 준비 완료.", "ko"); 

  getRealWeather();
  
  lastActivityTime = millis();
  currentState = IDLE;
  sendUART("STATUS", "GEMINI_WAITING");
  drawEye(C_WARM, "Ready", true);
  
  setupMic(); 
  
  Serial.println(">> Listening for 'Mirror'...");
}

// ==========================================
// 5. MAIN LOOP
// ==========================================
void loop() {
  audio.loop(); 

  unsigned long now = millis();

  checkActivityTimer();

  if (currentState == SLEEP) {
    if (Serial1.available()) {
      String line = Serial1.readStringUntil('\n');
      line.trim();
      if (line == "PIR|DETECTED") wakeUpSystem();
    }
    return; 
  }

  // --- Active Mode ---

  if (now - lastTimeUpdate > 1000) {
    lastTimeUpdate = now;
    struct tm t;
    if(getLocalTime(&t)) {
      char buf[16];
      strftime(buf, 16, "%H:%M:%S", &t);
      sendUART("TIME", String(buf));
    }
  }

  if (now - lastWeatherUpdate > 600000) {
    lastWeatherUpdate = now;
    getRealWeather();
  }
  
  if (Serial1.available()) {
    String line = Serial1.readStringUntil('\n');
    line.trim();
    if (line == "PIR|DETECTED") {
      lastActivityTime = millis(); 
      if (currentScreenMsg == "OFF") wakeUpSystem();
    }
  }

  // 음성 호출 감지
  if (!audio.isRunning() && currentState == IDLE) {
    float soundLevel = detectSoundLevel();
    
    if (soundLevel > VOICE_THRESHOLD) {
      Serial.printf(">> Sound Detected! Level: %.1f\n", soundLevel);
      processConversation(); 
      delay(1000); // 쿨다운
    }
  }
}

// ==========================================
// 6. Logic Functions
// ==========================================

void wakeUpSystem() {
  Serial.println(">> Wake Up!");
  lastActivityTime = millis();
  currentState = IDLE;
  
  tft.sendCommand(0x11); delay(120); 
  tft.sendCommand(0x29); 
  
  drawEye(C_WARM, "Hello");
  sendUART("STATUS", "GEMINI_WAITING");
  audio.connecttospeech("반가워요", "ko");
  
  setupMic(); 
  getRealWeather();
}

void checkActivityTimer() {
  if (currentState == SLEEP) return;
  if (currentState != IDLE) {
    lastActivityTime = millis();
    return;
  }

  if (millis() - lastActivityTime > ACTIVE_TIMEOUT) {
    Serial.println(">> 5min Timeout. Sleep.");
    currentState = SLEEP;
    
    uninstallMic(); 
    tft.fillScreen(C_BLACK);
    tft.sendCommand(0x10); 
    delay(120);
    
    sendUART("STATUS", "SLEEP");
    currentScreenMsg = "OFF";
  }
}

// DC 오프셋(노이즈) 제거 알고리즘
float detectSoundLevel() {
  int32_t sample[32];
  size_t bytesRead = 0;
  i2s_read(I2S_MIC_PORT, sample, sizeof(sample), &bytesRead, 10);
  
  if (bytesRead == 0) return 0;

  int samples = bytesRead / 4;
  double mean = 0;
  for (int i=0; i<samples; i++) mean += (sample[i] >> 14);
  mean /= samples;

  double sum = 0;
  for (int i=0; i<samples; i++) {
    double val = (sample[i] >> 14) - mean; 
    sum += val * val;
  }
  return sqrt(sum / samples);
}

void processConversation() {
  currentState = LISTENING;
  sendUART("STATUS", "GEMINI_LISTENING");
  drawEye(C_ORANGE, "Listening...", true);
  
  if(audio.isRunning()) audio.stopSong();
  setupMic(); 
  
  int16_t *recBuffer = nullptr;
  size_t recSize = 0;
  
  feedWDT(); 
  bool success = recordAudio(&recBuffer, &recSize);
  
  uninstallMic(); 
  
  sendUART("STATUS", "GEMINI_RECORDING"); 
  drawEye(C_YELLOW, "Processing...", true);

  String userText = "";
  if (success) {
    feedWDT();
    userText = sendToDeepgram(recBuffer, recSize);
    free(recBuffer); 
  } else {
    userText = "ERROR";
  }
  
  Serial.println(">> Recognized: [" + userText + "]"); 

  if (userText == "ERROR" || userText == "") {
    drawEye(C_RED, "No Speech", true);
    delay(2000);
    
    currentState = IDLE;
    sendUART("STATUS", "GEMINI_WAITING");
    drawEye(C_WARM, "Ready", true);
    setupMic(); 
    return;
  }

  // 호출어 필터링 (기존 로직 유지)
  bool isWakeWord = false;
  String cleanText = userText;
  cleanText.replace(" ", ""); 

  if (cleanText.indexOf("미러") != -1 || 
      cleanText.indexOf("밀어") != -1 ||
      cleanText.indexOf("미럼") != -1 ||
      cleanText.indexOf("미렁") != -1 ||
      cleanText.indexOf("미리") != -1 ||
      cleanText.indexOf("미려") != -1 ||
      cleanText.indexOf("미르") != -1 ||
      cleanText.indexOf("이러") != -1 || 
      cleanText.indexOf("일어") != -1 ||
      cleanText.indexOf("위러") != -1 ||
      cleanText.indexOf("비러") != -1 || 
      cleanText.indexOf("빌어") != -1 ||
      cleanText.indexOf("피러") != -1 || 
      cleanText.indexOf("필어") != -1 ||
      cleanText.indexOf("거울") != -1 ||
      cleanText.indexOf("Gemini") != -1) {
    isWakeWord = true;
  }

  if (!isWakeWord) {
    Serial.println(">> Not a command. (Call 'Mirror')");
    drawEye(C_ORANGE, "Call 'Mirror'", true); 
    delay(2000);

    currentState = IDLE;
    sendUART("STATUS", "GEMINI_WAITING");
    drawEye(C_WARM, "Ready", true);
    setupMic(); 
    return;
  }
  // (호출어 필터링 끝)

  lastActivityTime = millis(); 

  currentState = THINKING;
  sendUART("STATUS", "GEMINI_THINKING");
  drawEye(C_BLUE, "Thinking...", true);
  
  feedWDT();
  String answer = askGemini(userText);
  
  if (answer != "ERROR") {
    
    // ⭐ [최종 보강] 네트워크 자원 정리 시간 확보 (1초)
    Serial.println(">> Gemini response received. Waiting 1s for TCP stack cleanup before TTS.");
    delay(1000); 

    currentState = SPEAKING;
    sendUART("STATUS", "GEMINI_SPEAKING");
    sendUART("GEMINI", answer); 
    drawEye(C_GREEN, "Speaking...", true);
    
    Serial.println(">> AI Answer: " + answer);
    audio.connecttospeech(answer.c_str(), "ko");
    
    // TTS가 오디오 스트림을 확보할 시간을 벌어줌 (기존 로직 유지)
    delay(5000); 
    
    unsigned long speakStart = millis();
    // 90초 대기
    while(millis() - speakStart < 90000) { 
        audio.loop(); 
        
        // [핵심 수정] TTS 재생 중 WDT 피딩 및 Delay 추가
        feedWDT();
        delay(1); // CPU 독점 완화 및 오디오 스트리밍 안정화에 도움
        
        // [디버깅 로그] TTS 재생 상태 확인 (5초마다)
        if (millis() % 5000 == 0) Serial.printf("TTS Playing: %s\n", audio.isRunning() ? "true" : "false");
        
        // 2초 대기 후에도 isRunning이 false면, 재생 시작 자체가 실패한 것으로 간주하고 루프 탈출
        if (millis() - speakStart > 2500 && !audio.isRunning()) {
             Serial.println("!! Audio Lib Failed to Start Playback (TTS Bug) !!");
             break;
        }

        if(!audio.isRunning()) break; 
    }
  } else {
    drawEye(C_RED, "Error", true);
    sendUART("STATUS", "GEMINI_ERROR");
    audio.connecttospeech("오류가 발생했습니다.", "ko");
    delay(2000);
  }

  currentState = IDLE;
  sendUART("STATUS", "GEMINI_WAITING");
  drawEye(C_WARM, "Ready", true);
  
  setupMic();
}

// ==========================================
// 7. API Functions (반복문 안전 로직 포함)
// ==========================================
String sendToDeepgram(int16_t *buffer, size_t size) {
  if(WiFi.status() != WL_CONNECTED) return "ERROR";
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  
  sendUART("STATUS", "GEMINI_SENDING"); 

  String url = "https://api.deepgram.com/v1/listen?encoding=linear16&sample_rate=16000&language=ko&model=whisper&smart_format=true";
  
  http.begin(client, url);
  http.addHeader("Authorization", "Token " + String(sttKey));
  http.addHeader("Content-Type", "application/octet-stream");

  feedWDT(); 
  int httpCode = http.POST((uint8_t*)buffer, size);
  
  String result = "ERROR";
  if (httpCode == 200) {
    feedWDT();
    String payload = http.getString();
    feedWDT();
    // 메모리 설정은 그대로 유지
    DynamicJsonDocument doc(10000); 
    deserializeJson(doc, payload);
    if (doc["results"]["channels"][0]["alternatives"][0]["transcript"]) {
      result = String((const char*)doc["results"]["channels"][0]["alternatives"][0]["transcript"]);
    }
  } else {
    Serial.printf("Deepgram Error: %d\n", httpCode); 
    Serial.println(http.getString());
  }
  http.end();
  feedWDT();
  return result;
}

String askGemini(String text) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  
  // 모델명 유지: gemini-2.5-flash
  String url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + String(geminiKey);
  
  // 429 재시도 루프 시작
  String finalAnswer = "ERROR";
  int retryCount = 0;
  
  while (retryCount < 3) {
      http.begin(client, url);
      http.setTimeout(15000);
      http.addHeader("Content-Type", "application/json");
      DynamicJsonDocument doc(10000); 
      
      // ⭐ [수정] 프롬프트에 길이 제한 강력 명시 (Google TTS 100자 제한 회피)
      String prompt = "System: You are a smart mirror named 'Mirror' (미러). "
                      "Ignore name typos. Answer friendly in Korean. "
                      "IMPORTANT: Keep response UNDER 80 characters. Long text will be cut off by the speaker. "
                      "Do NOT use markdown or emojis. "
                      "Current Context: [Location: " + String(city) + ", Weather: " + globalWeather + ", Temp: " + globalTemp + "C]. "
                      "User: " + text;
      
      doc["contents"].add<JsonObject>()["parts"].add<JsonObject>()["text"] = prompt;
      String body; serializeJson(doc, body);
      
      feedWDT();
      int code = http.POST(body);
      
      if (code == 200) {
          feedWDT();
          String res = http.getString();
          DynamicJsonDocument resDoc(10000); 
          feedWDT();
          DeserializationError error = deserializeJson(resDoc, res);
          
          if (!error && resDoc["candidates"][0]["content"]["parts"][0]["text"]) {
              finalAnswer = String((const char*)resDoc["candidates"][0]["content"]["parts"][0]["text"]);
              http.end();
              feedWDT();
              return finalAnswer; // 성공적으로 답변 획득
          }
          http.end();
          return "ERROR"; // JSON 파싱 또는 내용 오류
          
      } else if (code == 429) {
          retryCount++;
          Serial.printf(">> QUOTA EXCEEDED (429). Retrying in 15s (Attempt %d/3)...\n", retryCount);
          drawEye(C_RED, "Quota Fail", true);
          
          unsigned long delayStart = millis();
          while (millis() - delayStart < 15000) {
              feedWDT();
              delay(10);
          }
      } else {
          Serial.printf("Gemini Error: %d\n", code);
          Serial.println("Response: " + http.getString());
          http.end();
          return "ERROR"; // 다른 HTTP 에러
      }
      http.end(); // 루프 종료 시 http 연결 닫기
  }

  feedWDT();
  return "ERROR"; // 3번의 재시도 모두 실패
}

// ==========================================
// 8. Hardware Functions (WDT 피딩 추가)
// ==========================================
void setupMic() {
  if (isMicOn) { i2s_driver_uninstall(I2S_MIC_PORT); isMicOn = false; }
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), .sample_rate = SAMPLE_RATE, .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, .communication_format = I2S_COMM_FORMAT_I2S, .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1, .dma_buf_count = 4, .dma_buf_len = 1024, .use_apll = false };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK, .ws_io_num = I2S_MIC_WS, .data_out_num = I2S_PIN_NO_CHANGE, .data_in_num = I2S_MIC_SD };
  esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);
  if (err == ESP_OK) { i2s_set_pin(I2S_MIC_PORT, &pin_config); isMicOn = true; }
}
void uninstallMic() { if (isMicOn) { i2s_driver_uninstall(I2S_MIC_PORT); isMicOn = false; } }

bool recordAudio(int16_t **buffer, size_t *size) {
  size_t bytesToRecord = SAMPLE_RATE * RECORD_TIME * sizeof(int16_t);
  *buffer = (int16_t *)heap_caps_malloc(bytesToRecord, MALLOC_CAP_SPIRAM);
  if (!*buffer) { Serial.println("!! PSRAM MALLOC FAILED !!"); return false; }
  size_t totalRead = 0; size_t bytesRead = 0; int32_t sampleBuffer[256];
  unsigned long start = millis();
  while (totalRead < bytesToRecord && (millis() - start < 7000)) { 
    feedWDT(); 
    i2s_read(I2S_MIC_PORT, sampleBuffer, sizeof(sampleBuffer), &bytesRead, 100);
    int samples = bytesRead / 4;
    for (int i=0; i<samples; i++) {
      if (totalRead >= bytesToRecord) break;
      (*buffer)[totalRead/2] = (int16_t)(sampleBuffer[i] >> 16); 
      totalRead += 2;
    }
  }
  *size = totalRead; return true;
}

void drawEye(uint16_t color, String msg, bool force) {
  if (!force && currentScreenMsg == msg) return;
  currentScreenMsg = msg; 
  tft.fillScreen(C_BLACK);
  tft.drawCircle(120, 120, 100, color);
  tft.fillCircle(120, 120, 40, color);
  tft.setTextColor(color); tft.setTextSize(2);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(120 - w/2, 180); tft.print(msg);
}

void sendUART(String tag, String value) { Serial1.println(tag + "|" + value); }

void initWiFi() { 
    WiFi.mode(WIFI_STA); 
    WiFi.setSleep(false); // 절전 모드 해제
    WiFi.begin(ssid, password); 
    while(WiFi.status()!=WL_CONNECTED) { delay(500); feedWDT(); }
}

void initTime() { 
    configTime(32400, 0, "pool.ntp.org"); 
    struct tm t; 
    getLocalTime(&t); 
}

void getRealWeather() { 
  if(WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "," + String(country) + "&units=metric&appid=" + String(weatherKey);
  http.begin(url);
  feedWDT();
  if(http.GET() == 200) {
    feedWDT();
    String payload = http.getString();
    feedWDT();
    JsonDocument doc; deserializeJson(doc, payload);
    String main = doc["weather"][0]["main"]; float temp = doc["main"]["temp"];
    globalWeather = main; globalTemp = String(temp, 1);
    sendUART("WEATHER", main + " " + String(temp, 1) + "C");
  }
  http.end();
}

void audio_info(const char *info){ Serial.print("info "); Serial.println(info); }
void audio_eof_mp3(const char *info){ Serial.print("eof_mp3 "); Serial.println(info); }