<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/b297e251-c161-402d-a36a-bbded58ad3e2" /># Smart_Mirror
2025-2학기 팀(Holy Cow) 프로젝트로 진행한 "Smart Mirror" 제작 기록용 리포지토리.
> ESP32-S3 기반 음성 인식 AI 스마트 미러 시스템  
> Speech Recognition + Gemini AI + TTS + IoT Display

---
<img width="1536" height="1024" alt="0e920b7e-f89c-4dee-9445-5f7b77b9d502" src="https://github.com/user-attachments/assets/7301407d-6387-4d5c-826c-f51b72f5bf33" />

## Overview

사용자의 음성을 인식하고 AI와 대화하며, 시간·날씨·상태 정보를 시각 및 음성으로 제공하는 인터랙티브 스마트 미러.

**목표**
- 음성 기반 인터페이스 구현
- 실시간 AI 응답 시스템 구축
- 임베디드 환경에서 안정적인 TTS 출력 구현
- 사용자 친화적인 시각 피드백 제공

---

## System Architecture

```
[User Voice]
     ↓
[ESP32-S3]
 ├─ Sound Detection (I2S Mic)
 ├─ STT (Deepgram Whisper)
 ├─ AI Processing (Gemini 2.5 Flash)
 ├─ TTS (Google Speech)
 ├─ Display (GC9A01A Round LCD)
 └─ UART Communication
     ↓
[Arduino UNO]
 ├─ PIR Motion Detection
 ├─ LCD / LED Output
 └─ 상태 보조 처리
```

---

## Hardware Components

| 분류 | 부품 |
|------|------|
| Main Controller | ESP32-S3 N16R8 (PSRAM 포함) |
| Sub Controller | Arduino UNO |
| 마이크 | I2S Microphone |
| 오디오 출력 | I2S Audio Amplifier + Speaker |
| 모션 센서 | PIR Motion Sensor (HC-SR501 계열) |
| 디스플레이 | GC9A01A Round TFT LCD |

---

## Pin Configuration

### ESP32-S3

| 기능 | 핀 |
|------|----|
| TFT CS | 10 |
| TFT DC | 9 |
| TFT RST | 8 |
| TFT SDA | 13 |
| TFT SCL | 14 |
| Mic SCK | 40 |
| Mic SD | 41 |
| Mic WS | 42 |
| Speaker BCLK | 5 |
| Speaker LRC | 4 |
| Speaker DIN | 7 |
| UART RX | 16 |
| UART TX | 17 |

### Arduino UNO

| 기능 | 핀 |
|------|----|
| PIR Sensor | 4 |
| LED | 6 |
| UART RX | 0 |
| UART TX | 1 |

---

## System Flow

1. PIR 센서 → 사용자 접근 감지
2. ESP32 → Wake 상태 진입
3. 음성 감지 (Threshold 기반)
4. STT (Deepgram Whisper)
5. AI 응답 생성 (Gemini)
6. TTS 출력 (Google Speech)
7. LCD 및 LED 상태 표시
8. 일정 시간 후 Sleep 모드 전환

---

## Key Features

**Voice Interaction**
- I2S 기반 실시간 음성 감지
- Wake Word 유사 필터링 (`Mirror` / `미러`)

**AI Response**
- Gemini 2.5 Flash 모델 사용
- 자연어 기반 응답 생성

**Stable TTS System**
- 네트워크 딜레이 대응
- Timeout 확장 (5s / 30s)
- WDT 기반 안정성 확보

**Visual Feedback**
- 상태별 UI (Listening / Thinking / Speaking)
- 원형 LCD 기반 인터페이스

**Power Management**
- 5분 비활성 시 Sleep
- PIR 기반 자동 Wake

---

## APIs Used

| API | 용도 |
|-----|------|
| Deepgram API | Speech-to-Text (Whisper) |
| Google Gemini API | AI 응답 생성 |
| Google TTS | Text-to-Speech |
| OpenWeather API | 실시간 날씨 정보 |

---

## Known Issues / Limitations

- 소리 기반 Wake → 노이즈에 민감
- TTS는 네트워크 상태에 영향을 받음
- Gemini API quota 제한 (429 발생 가능)
- 긴 문장은 TTS에서 잘림 (80자 제한 적용)

---

## Future Improvements

- 키워드 기반 Wake Word 모델 도입 (ML)
- UI 개선 (애니메이션 / 폰트)
- 배터리 기반 독립 구동
- 로컬 STT/TTS 적용 (오프라인화)
- 모바일 앱 연동

---

## How to Run

### 1. ESP32 Setup

- Arduino IDE + ESP32 Board v2.0.17
- PSRAM Enabled
- `WiFi` 및 API Key 설정

### 2. Arduino UNO Setup

- UART 통신 연결
- PIR 및 LCD 연결

### 3. 실행

1. 전원 연결
2. WiFi 연결 확인
3. `"미러"` 호출 후 음성 입력

---

## Notes

실제 임베디드 환경에서 AI 음성 인터페이스를 구현하는 것을 목표로 제작되었으며, 안정성과 실용성 확보를 위한 다양한 예외 처리 로직이 포함되어 있음.
