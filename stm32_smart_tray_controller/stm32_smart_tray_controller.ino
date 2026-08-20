#include <Arduino.h>
#include <Adafruit_BMP3XX.h>
#include <Wire.h>

// STM32F401RE / STM32duino 3.x 기준 핀. Uart 생성자 순서는 RX, TX입니다.
Uart GpsSerial(PA10, PA9);   // USART1: Air530 TX -> PA10
Uart CoSerial(PC7, PC6);     // USART6: ZE07-CO TX -> PC7

constexpr uint32_t BUZZER_PIN = PB0;
constexpr uint32_t VIBRATION_PIN = PB1;
constexpr uint32_t STROBE_PIN = PC8;
constexpr uint32_t JETSON_POWER_GATE_PIN = PC9;
constexpr uint32_t POWER_BUTTON_PIN = PA0;
constexpr uint32_t CHECKPOINT_BUTTON_PIN = PA1;
constexpr uint32_t VOICE_BUTTON_PIN = PA4;
constexpr uint8_t SHT40_ADDRESS = 0x44;
constexpr uint8_t DS3231_ADDRESS = 0x68;
constexpr uint8_t BMP390_PRIMARY_ADDRESS = 0x77;
constexpr uint8_t BMP390_SECONDARY_ADDRESS = 0x76;

constexpr uint32_t HOST_BAUD = 115200;
constexpr uint32_t GPS_BAUD = 9600;
constexpr uint32_t CO_BAUD = 9600;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
constexpr uint32_t GPS_STALE_MS = 3000;
constexpr uint32_t SENSOR_STALE_MS = 3000;
constexpr uint32_t RTC_INTERVAL_MS = 1000;
constexpr uint32_t BMP390_READ_INTERVAL_MS = 5000;
constexpr uint32_t BMP390_STALE_MS = 15000;
constexpr uint32_t BMP390_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t PRESSURE_TREND_SAMPLE_MS = 60000;
constexpr uint32_t PRESSURE_TREND_MIN_SPAN_MS = 600000;
constexpr size_t PRESSURE_TREND_HISTORY_SIZE = 31;
// 10분 이상 회귀 기울기가 시간당 ±0.5 hPa를 넘을 때만 방향을 말합니다 [추정].
constexpr float PRESSURE_TREND_THRESHOLD_HPA_PER_HOUR = 0.5f;
constexpr uint32_t CO_WARMUP_MS = 300000;  // 제조사 첫 사용 주의사항: 최소 5분

constexpr float CO_WARNING_PPM = 35.0f;
constexpr uint32_t CO_WARNING_HOLD_MS = 180000;
constexpr float CO_IMMEDIATE_ALARM_PPM = 100.0f;
constexpr float CO_CLEAR_PPM = 30.0f;
constexpr uint32_t CO_CLEAR_HOLD_MS = 30000;
constexpr size_t CO_HISTORY_SECONDS = 600;
constexpr uint32_t TRAIL_ALERT_WATCHDOG_MS = 5000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t POWER_LONG_PRESS_MS = 2000;
constexpr uint32_t POWER_ACK_CUT_DELAY_MS = 90000;
constexpr uint32_t POWER_PENDING_TIMEOUT_MS = 120000;

char gpsLine[160];
size_t gpsLineLength = 0;
bool gpsCurrentFix = false;
bool gpsHasLastFix = false;
double gpsLatitude = 0.0;
double gpsLongitude = 0.0;
uint8_t gpsSatellites = 0;
float gpsHdop = NAN;
uint32_t gpsLastSentenceMs = 0;
uint32_t gpsLastFixMs = 0;

bool shtRequestPending = false;
uint32_t shtRequestMs = 0;
uint32_t shtLastStartMs = 0;
uint32_t environmentLastUpdateMs = 0;
bool environmentHasValue = false;
float temperatureC = NAN;
float humidityPct = NAN;

Adafruit_BMP3XX bmp390;
bool bmp390Ready = false;
uint8_t bmp390Address = 0;
uint8_t bmp390ConsecutiveFailures = 0;
bool bmp390InitAttempted = false;
uint32_t bmp390LastInitAttemptMs = 0;
uint32_t bmp390LastReadAttemptMs = 0;
uint32_t pressureLastUpdateMs = 0;
bool pressureHasValue = false;
float pressureHpa = NAN;

struct PressureTrendSample {
  uint32_t atMs;
  float pressureHpa;
};

PressureTrendSample pressureTrendHistory[PRESSURE_TREND_HISTORY_SIZE];
size_t pressureTrendCount = 0;
size_t pressureTrendNext = 0;
uint32_t pressureTrendLastSampleMs = 0;

bool rtcValid = false;
uint32_t rtcLastReadMs = 0;
uint32_t rtcLastUpdateMs = 0;
char rtcIsoUtc[21] = "";

uint8_t coFrame[9];
size_t coFrameLength = 0;
uint32_t coLastUpdateMs = 0;
bool coHasValue = false;
float coPpm = NAN;
float coHistory[CO_HISTORY_SECONDS];
size_t coHistoryCount = 0;
size_t coHistoryIndex = 0;
uint32_t coLastHistoryMs = 0;
uint32_t coAbove35SinceMs = 0;
uint32_t coAbove70SinceMs = 0;
uint32_t coAbove150SinceMs = 0;
uint32_t coAbove400SinceMs = 0;
uint32_t coSafeSinceMs = 0;
bool coWarning = false;
bool coAlarmLatched = false;
uint8_t trailAlertLevel = 0;  // 0=off, 1=accuracy unknown caution, 2=confirmed alert
uint32_t trailAlertRefreshMs = 0;

char hostCommand[64];
size_t hostCommandLength = 0;
bool streamEnabled = true;
uint32_t telemetrySequence = 0;
uint32_t lastTelemetryMs = 0;
uint32_t buttonSequence = 0;
uint32_t outputSequence = 0;
uint32_t powerSequence = 0;
bool jetsonPowerGateOn = true;
bool powerShutdownPending = false;
bool powerGateOffScheduled = false;
uint32_t powerShutdownRequestedMs = 0;
uint32_t powerGateOffScheduledMs = 0;

struct ButtonInput {
  uint32_t pin;
  const char* name;
  bool rawPressed;
  bool stablePressed;
  uint32_t rawChangedMs;
  uint32_t pressedMs;
};

ButtonInput buttons[] = {
    {POWER_BUTTON_PIN, "power", false, false, 0, 0},
    {CHECKPOINT_BUTTON_PIN, "checkpoint", false, false, 0, 0},
    {VOICE_BUTTON_PIN, "voice", false, false, 0, 0},
};

class FixedBufferWriter : public Print {
 public:
  FixedBufferWriter(char* buffer, size_t capacity)
      : buffer_(buffer), capacity_(capacity), length_(0), overflow_(false) {
    if (capacity_ > 0) buffer_[0] = '\0';
  }

  using Print::write;

  size_t write(uint8_t value) override {
    if (length_ + 1 >= capacity_) {
      overflow_ = true;
      return 0;
    }
    buffer_[length_++] = static_cast<char>(value);
    buffer_[length_] = '\0';
    return 1;
  }

  const char* data() const { return buffer_; }
  size_t length() const { return length_; }
  bool overflowed() const { return overflow_; }

 private:
  char* buffer_;
  size_t capacity_;
  size_t length_;
  bool overflow_;
};

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void sendChecksummedJson(char* base, size_t length) {
  if (length < 2 || base[length - 1] != '}') return;
  const uint16_t checksum = crc16Ccitt(
      reinterpret_cast<const uint8_t*>(base), length);
  base[length - 1] = '\0';
  char checksumText[5];
  snprintf(checksumText, sizeof(checksumText), "%04X", checksum);
  Serial.print(base);
  Serial.print(",\"crc16\":\"");
  Serial.print(checksumText);
  Serial.println("\"}");
}

void sendButtonEvent(const ButtonInput& button, bool pressed, uint32_t heldMs) {
  char base[190];
  FixedBufferWriter output(base, sizeof(base));
  output.print("{\"v\":1,\"event\":\"button\",\"seq\":");
  output.print(buttonSequence++);
  output.print(",\"button\":\""); output.print(button.name);
  output.print("\",\"state\":\""); output.print(pressed ? "pressed" : "released");
  output.print("\",\"held_ms\":"); output.print(heldMs);
  output.print("}");
  if (!output.overflowed()) sendChecksummedJson(base, output.length());
}

void sendOutputEvent(const char* level, bool active) {
  char base[200];
  FixedBufferWriter output(base, sizeof(base));
  output.print("{\"v\":1,\"event\":\"output\",\"seq\":");
  output.print(outputSequence++);
  output.print(",\"output\":\"trail\",\"level\":\""); output.print(level);
  output.print("\",\"active\":"); output.print(active ? "true" : "false");
  output.print(",\"watchdog_ms\":"); output.print(TRAIL_ALERT_WATCHDOG_MS);
  output.print("}");
  if (!output.overflowed()) sendChecksummedJson(base, output.length());
}

void sendPowerEvent(const char* state) {
  char base[220];
  FixedBufferWriter output(base, sizeof(base));
  output.print("{\"v\":1,\"event\":\"power\",\"seq\":");
  output.print(powerSequence++);
  output.print(",\"state\":\""); output.print(state);
  output.print("\",\"gate_on\":"); output.print(jetsonPowerGateOn ? "true" : "false");
  output.print(",\"shutdown_pending\":");
  output.print(powerShutdownPending ? "true" : "false");
  output.print("}");
  if (!output.overflowed()) sendChecksummedJson(base, output.length());
}

void handlePowerButtonRelease(uint32_t now, uint32_t heldMs) {
  if (!jetsonPowerGateOn) {
    jetsonPowerGateOn = true;
    powerShutdownPending = false;
    powerGateOffScheduled = false;
    digitalWrite(JETSON_POWER_GATE_PIN, HIGH);
    sendPowerEvent("gate_on");
    return;
  }
  if (heldMs < POWER_LONG_PRESS_MS || powerShutdownPending) return;
  powerShutdownPending = true;
  powerShutdownRequestedMs = now;
  sendPowerEvent("shutdown_requested");
}

void updatePowerGate(uint32_t now) {
  if (powerGateOffScheduled &&
      static_cast<uint32_t>(now - powerGateOffScheduledMs) >= POWER_ACK_CUT_DELAY_MS) {
    powerGateOffScheduled = false;
    powerShutdownPending = false;
    jetsonPowerGateOn = false;
    digitalWrite(JETSON_POWER_GATE_PIN, LOW);
    sendPowerEvent("gate_off");
    return;
  }
  if (powerShutdownPending && !powerGateOffScheduled &&
      static_cast<uint32_t>(now - powerShutdownRequestedMs) >= POWER_PENDING_TIMEOUT_MS) {
    powerShutdownPending = false;
    sendPowerEvent("shutdown_timeout");
  }
}

void updateButtons(uint32_t now) {
  for (ButtonInput& button : buttons) {
    const bool rawPressed = digitalRead(button.pin) == LOW;
    if (rawPressed != button.rawPressed) {
      button.rawPressed = rawPressed;
      button.rawChangedMs = now;
    }
    if (rawPressed == button.stablePressed ||
        static_cast<uint32_t>(now - button.rawChangedMs) < BUTTON_DEBOUNCE_MS) {
      continue;
    }
    button.stablePressed = rawPressed;
    if (rawPressed) {
      button.pressedMs = now;
      sendButtonEvent(button, true, 0);
    } else {
      const uint32_t heldMs = static_cast<uint32_t>(now - button.pressedMs);
      if (strcmp(button.name, "power") == 0) handlePowerButtonRelease(now, heldMs);
      // 전원 이벤트를 먼저 내보내 MAP가 pending을 확인한 뒤 release SSE를 전달하게 한다.
      sendButtonEvent(button, false, heldMs);
    }
  }
}

uint8_t shtCrc8(const uint8_t* data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

uint8_t bcdToBinary(uint8_t value) {
  return static_cast<uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

uint8_t binaryToBcd(uint8_t value) {
  return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
}

bool leapYear(uint16_t year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t daysInMonth(uint16_t year, uint8_t month) {
  static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && leapYear(year) ? 29 : days[month - 1];
}

bool parseFixedRtcUtc(
    const char* text,
    uint16_t& year,
    uint8_t& month,
    uint8_t& day,
    uint8_t& hour,
    uint8_t& minute,
    uint8_t& second) {
  if (text == nullptr || strlen(text) != 20 || text[4] != '-' || text[7] != '-' ||
      text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z') {
    return false;
  }
  const uint8_t digitPositions[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
  for (const uint8_t position : digitPositions) {
    if (!isdigit(static_cast<unsigned char>(text[position]))) return false;
  }
  year = static_cast<uint16_t>(
      (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
      (text[2] - '0') * 10 + (text[3] - '0'));
  month = static_cast<uint8_t>((text[5] - '0') * 10 + (text[6] - '0'));
  day = static_cast<uint8_t>((text[8] - '0') * 10 + (text[9] - '0'));
  hour = static_cast<uint8_t>((text[11] - '0') * 10 + (text[12] - '0'));
  minute = static_cast<uint8_t>((text[14] - '0') * 10 + (text[15] - '0'));
  second = static_cast<uint8_t>((text[17] - '0') * 10 + (text[18] - '0'));
  return year >= 2025 && year <= 2100 && month >= 1 && month <= 12 &&
      day >= 1 && day <= daysInMonth(year, month) && hour <= 23 &&
      minute <= 59 && second <= 59;
}

bool provisionRtcUtc(const char* text, uint32_t now) {
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  if (!parseFixedRtcUtc(text, year, month, day, hour, minute, second)) return false;

  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(0x00);
  Wire.write(binaryToBcd(second));
  Wire.write(binaryToBcd(minute));
  Wire.write(binaryToBcd(hour));  // 24시간 형식
  Wire.write(1);                  // day-of-week는 항법 계산에 사용하지 않습니다.
  Wire.write(binaryToBcd(day));
  uint8_t monthRegister = binaryToBcd(month);
  if (year >= 2100) monthRegister |= 0x80;
  Wire.write(monthRegister);
  Wire.write(binaryToBcd(static_cast<uint8_t>(year % 100)));
  if (Wire.endTransmission() != 0) return false;

  uint8_t status = 0;
  if (!readDs3231Register(0x0F, status)) return false;
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(0x0F);
  Wire.write(static_cast<uint8_t>(status & ~0x80));  // OSF만 해제합니다.
  if (Wire.endTransmission() != 0) return false;

  rtcLastReadMs = now - RTC_INTERVAL_MS;
  updateRtc(now);
  return rtcValid;
}

bool readDs3231Register(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(DS3231_ADDRESS, static_cast<uint8_t>(1)) != 1) return false;
  value = Wire.read();
  return true;
}

void updateRtc(uint32_t now) {
  if (static_cast<uint32_t>(now - rtcLastReadMs) < RTC_INTERVAL_MS) return;
  rtcLastReadMs = now;
  uint8_t status = 0;
  if (!readDs3231Register(0x0F, status) || (status & 0x80) != 0) {
    rtcValid = false;
    return;
  }
  Wire.beginTransmission(DS3231_ADDRESS);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(DS3231_ADDRESS, static_cast<uint8_t>(7)) != 7) {
    rtcValid = false;
    return;
  }
  const uint8_t second = bcdToBinary(Wire.read() & 0x7F);
  const uint8_t minute = bcdToBinary(Wire.read() & 0x7F);
  const uint8_t rawHour = Wire.read();
  Wire.read();  // day-of-week는 날짜 검증에 사용하지 않습니다.
  const uint8_t day = bcdToBinary(Wire.read() & 0x3F);
  const uint8_t rawMonth = Wire.read();
  const uint8_t yearLow = bcdToBinary(Wire.read());
  uint8_t hour = 0;
  if ((rawHour & 0x40) != 0) {
    hour = bcdToBinary(rawHour & 0x1F);
    if (hour < 1 || hour > 12) {
      rtcValid = false;
      return;
    }
    if ((rawHour & 0x20) != 0 && hour != 12) hour += 12;
    if ((rawHour & 0x20) == 0 && hour == 12) hour = 0;
  } else {
    hour = bcdToBinary(rawHour & 0x3F);
  }
  const uint8_t month = bcdToBinary(rawMonth & 0x1F);
  const uint16_t year = static_cast<uint16_t>(2000 + yearLow + ((rawMonth & 0x80) ? 100 : 0));
  if (second > 59 || minute > 59 || hour > 23 || month < 1 || month > 12 ||
      day < 1 || day > daysInMonth(year, month) || year < 2025 || year > 2100) {
    rtcValid = false;
    return;
  }
  snprintf(
      rtcIsoUtc,
      sizeof(rtcIsoUtc),
      "%04u-%02u-%02uT%02u:%02u:%02uZ",
      year,
      month,
      day,
      hour,
      minute,
      second);
  rtcValid = true;
  rtcLastUpdateMs = now;
}

bool elapsedAtLeast(uint32_t now, uint32_t started, uint32_t duration) {
  return started != 0 && static_cast<uint32_t>(now - started) >= duration;
}

bool gpsFixLive(uint32_t now) {
  return gpsCurrentFix && gpsHasLastFix && static_cast<uint32_t>(now - gpsLastSentenceMs) <= GPS_STALE_MS;
}

bool environmentFresh(uint32_t now) {
  return environmentHasValue && static_cast<uint32_t>(now - environmentLastUpdateMs) <= SENSOR_STALE_MS;
}

bool pressureFresh(uint32_t now) {
  return pressureHasValue &&
      static_cast<uint32_t>(now - pressureLastUpdateMs) <= BMP390_STALE_MS;
}

bool coFresh(uint32_t now) {
  return coHasValue && static_cast<uint32_t>(now - coLastUpdateMs) <= SENSOR_STALE_MS;
}

bool validateNmeaChecksum(char* line) {
  if (line[0] != '$') return false;
  char* star = strchr(line, '*');
  if (star == nullptr || strlen(star + 1) < 2) return false;
  uint8_t checksum = 0;
  for (char* cursor = line + 1; cursor < star; ++cursor) checksum ^= static_cast<uint8_t>(*cursor);
  char expectedText[3] = {star[1], star[2], '\0'};
  char* end = nullptr;
  unsigned long expected = strtoul(expectedText, &end, 16);
  if (end == expectedText || *end != '\0' || checksum != static_cast<uint8_t>(expected)) return false;
  *star = '\0';
  return true;
}

double nmeaDegrees(const char* raw, char hemisphere) {
  const double value = atof(raw);
  const int wholeDegrees = static_cast<int>(value / 100.0);
  const double minutes = value - wholeDegrees * 100.0;
  double coordinate = wholeDegrees + minutes / 60.0;
  if (hemisphere == 'S' || hemisphere == 'W') coordinate = -coordinate;
  return coordinate;
}

void parseGga(char* line, uint32_t now) {
  if (!validateNmeaChecksum(line)) return;
  char* fields[16] = {nullptr};
  size_t count = 1;
  fields[0] = line;
  for (char* cursor = line; *cursor != '\0' && count < 16; ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[count++] = cursor + 1;
    }
  }
  if (count < 9 || strlen(fields[0]) < 6 || strcmp(fields[0] + strlen(fields[0]) - 3, "GGA") != 0) return;

  const int quality = atoi(fields[6]);
  gpsSatellites = static_cast<uint8_t>(constrain(atoi(fields[7]), 0, 99));
  gpsHdop = strlen(fields[8]) ? atof(fields[8]) : NAN;
  gpsLastSentenceMs = now;
  gpsCurrentFix = quality > 0;
  if (!gpsCurrentFix || count < 6 || strlen(fields[2]) == 0 || strlen(fields[4]) == 0) return;
  gpsLatitude = nmeaDegrees(fields[2], fields[3][0]);
  gpsLongitude = nmeaDegrees(fields[4], fields[5][0]);
  if (gpsLatitude < -90.0 || gpsLatitude > 90.0 || gpsLongitude < -180.0 || gpsLongitude > 180.0) {
    gpsCurrentFix = false;
    return;
  }
  gpsHasLastFix = true;
  gpsLastFixMs = now;
}

void pumpGps(uint32_t now) {
  while (GpsSerial.available() > 0) {
    const char character = static_cast<char>(GpsSerial.read());
    if (character == '\r') continue;
    if (character == '\n') {
      gpsLine[gpsLineLength] = '\0';
      if (gpsLineLength > 6) parseGga(gpsLine, now);
      gpsLineLength = 0;
      continue;
    }
    if (gpsLineLength + 1 < sizeof(gpsLine)) {
      gpsLine[gpsLineLength++] = character;
    } else {
      gpsLineLength = 0;
    }
  }
}

void startShtMeasurement(uint32_t now) {
  Wire.beginTransmission(SHT40_ADDRESS);
  Wire.write(0xFD);  // 고정밀 측정
  if (Wire.endTransmission() == 0) {
    shtRequestPending = true;
    shtRequestMs = now;
  }
  shtLastStartMs = now;
}

void finishShtMeasurement(uint32_t now) {
  if (!shtRequestPending || static_cast<uint32_t>(now - shtRequestMs) < 10) return;
  shtRequestPending = false;
  const uint8_t received = Wire.requestFrom(SHT40_ADDRESS, static_cast<uint8_t>(6));
  if (received != 6) {
    while (Wire.available()) Wire.read();
    return;
  }
  uint8_t data[6];
  for (uint8_t index = 0; index < 6; ++index) data[index] = static_cast<uint8_t>(Wire.read());
  if (shtCrc8(data, 2) != data[2] || shtCrc8(data + 3, 2) != data[5]) return;
  const uint16_t rawTemperature = static_cast<uint16_t>(data[0] << 8) | data[1];
  const uint16_t rawHumidity = static_cast<uint16_t>(data[3] << 8) | data[4];
  temperatureC = -45.0f + 175.0f * static_cast<float>(rawTemperature) / 65535.0f;
  humidityPct = constrain(-6.0f + 125.0f * static_cast<float>(rawHumidity) / 65535.0f, 0.0f, 100.0f);
  environmentHasValue = true;
  environmentLastUpdateMs = now;
}

void updateEnvironment(uint32_t now) {
  finishShtMeasurement(now);
  if (!shtRequestPending && static_cast<uint32_t>(now - shtLastStartMs) >= TELEMETRY_INTERVAL_MS) {
    startShtMeasurement(now);
  }
}

void resetPressureObservationSession() {
  pressureHasValue = false;
  pressureHpa = NAN;
  pressureLastUpdateMs = 0;
  pressureTrendCount = 0;
  pressureTrendNext = 0;
  pressureTrendLastSampleMs = 0;
}

bool initializeBmp390(uint32_t now) {
  bmp390InitAttempted = true;
  bmp390LastInitAttemptMs = now;
  const uint8_t addresses[] = {BMP390_PRIMARY_ADDRESS, BMP390_SECONDARY_ADDRESS};
  for (const uint8_t address : addresses) {
    if (!bmp390.begin_I2C(address, &Wire)) continue;
    const bool configured =
        bmp390.setTemperatureOversampling(BMP3_OVERSAMPLING_2X) &&
        bmp390.setPressureOversampling(BMP3_OVERSAMPLING_8X) &&
        bmp390.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3) &&
        bmp390.setOutputDataRate(BMP3_ODR_0_2_HZ);
    if (!configured) continue;
    resetPressureObservationSession();
    bmp390Ready = true;
    bmp390Address = address;
    bmp390ConsecutiveFailures = 0;
    bmp390LastReadAttemptMs = now - BMP390_READ_INTERVAL_MS;
    return true;
  }
  bmp390Ready = false;
  bmp390Address = 0;
  resetPressureObservationSession();
  return false;
}

void appendPressureTrendSample(uint32_t now, float valueHpa) {
  if (pressureTrendCount > 0 &&
      static_cast<uint32_t>(now - pressureTrendLastSampleMs) < PRESSURE_TREND_SAMPLE_MS) {
    return;
  }
  pressureTrendHistory[pressureTrendNext] = {now, valueHpa};
  pressureTrendNext = (pressureTrendNext + 1) % PRESSURE_TREND_HISTORY_SIZE;
  if (pressureTrendCount < PRESSURE_TREND_HISTORY_SIZE) ++pressureTrendCount;
  pressureTrendLastSampleMs = now;
}

const char* pressureTrendName() {
  if (pressureTrendCount < 2) return "unknown";
  const size_t oldestIndex =
      (pressureTrendNext + PRESSURE_TREND_HISTORY_SIZE - pressureTrendCount) %
      PRESSURE_TREND_HISTORY_SIZE;
  const size_t newestIndex =
      (pressureTrendNext + PRESSURE_TREND_HISTORY_SIZE - 1) % PRESSURE_TREND_HISTORY_SIZE;
  const uint32_t spanMs = static_cast<uint32_t>(
      pressureTrendHistory[newestIndex].atMs - pressureTrendHistory[oldestIndex].atMs);
  if (spanMs < PRESSURE_TREND_MIN_SPAN_MS) return "unknown";

  // millis() 래핑을 보존한 상대 시간으로 최소제곱 기울기를 계산합니다.
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumXX = 0.0f;
  float sumXY = 0.0f;
  for (size_t offset = 0; offset < pressureTrendCount; ++offset) {
    const size_t index = (oldestIndex + offset) % PRESSURE_TREND_HISTORY_SIZE;
    const float hours = static_cast<uint32_t>(
        pressureTrendHistory[index].atMs - pressureTrendHistory[oldestIndex].atMs) /
        3600000.0f;
    const float pressure = pressureTrendHistory[index].pressureHpa;
    sumX += hours;
    sumY += pressure;
    sumXX += hours * hours;
    sumXY += hours * pressure;
  }
  const float count = static_cast<float>(pressureTrendCount);
  const float denominator = count * sumXX - sumX * sumX;
  if (fabsf(denominator) < 0.000001f) return "unknown";
  const float slopeHpaPerHour = (count * sumXY - sumX * sumY) / denominator;
  if (slopeHpaPerHour > PRESSURE_TREND_THRESHOLD_HPA_PER_HOUR) return "rising";
  if (slopeHpaPerHour < -PRESSURE_TREND_THRESHOLD_HPA_PER_HOUR) return "falling";
  return "steady";
}

void updatePressure(uint32_t now) {
  if (!bmp390Ready) {
    if (!bmp390InitAttempted ||
        static_cast<uint32_t>(now - bmp390LastInitAttemptMs) >= BMP390_RETRY_INTERVAL_MS) {
      initializeBmp390(now);
    }
    return;
  }
  if (static_cast<uint32_t>(now - bmp390LastReadAttemptMs) < BMP390_READ_INTERVAL_MS) return;
  bmp390LastReadAttemptMs = now;
  if (!bmp390.performReading()) {
    if (++bmp390ConsecutiveFailures >= 3) {
      bmp390Ready = false;
      bmp390Address = 0;
      resetPressureObservationSession();
    }
    return;
  }
  const float valueHpa = static_cast<float>(bmp390.pressure / 100.0);
  if (!isfinite(valueHpa) || valueHpa < 300.0f || valueHpa > 1100.0f) {
    if (++bmp390ConsecutiveFailures >= 3) {
      bmp390Ready = false;
      bmp390Address = 0;
      resetPressureObservationSession();
    }
    return;
  }
  bmp390ConsecutiveFailures = 0;
  pressureHpa = valueHpa;
  pressureHasValue = true;
  pressureLastUpdateMs = now;
  appendPressureTrendSample(now, valueHpa);
}

uint8_t winsenChecksum(const uint8_t* frame) {
  uint8_t sum = 0;
  for (uint8_t index = 1; index <= 7; ++index) sum = static_cast<uint8_t>(sum + frame[index]);
  return static_cast<uint8_t>(~sum + 1);
}

void acceptCoFrame(const uint8_t* frame, uint32_t now) {
  if (frame[0] != 0xFF || winsenChecksum(frame) != frame[8]) return;
  float value = NAN;
  if (frame[1] == 0x04 && frame[2] == 0x03) {
    // ZE07-CO 기본 능동 업로드: 소수 자릿수 1, 고/저 바이트 농도.
    const uint16_t raw = static_cast<uint16_t>(frame[4] << 8) | frame[5];
    const uint8_t decimals = frame[3] > 3 ? 3 : frame[3];
    float divisor = 1.0f;
    for (uint8_t index = 0; index < decimals; ++index) divisor *= 10.0f;
    value = raw / divisor;
  }
  if (!isfinite(value) || value < 0.0f || value > 10000.0f) return;
  coPpm = value;
  coHasValue = true;
  coLastUpdateMs = now;
}

void pumpCo(uint32_t now) {
  while (CoSerial.available() > 0) {
    const uint8_t value = static_cast<uint8_t>(CoSerial.read());
    if (coFrameLength == 0 && value != 0xFF) continue;
    if (value == 0xFF && coFrameLength > 0) coFrameLength = 0;
    coFrame[coFrameLength++] = value;
    if (coFrameLength == sizeof(coFrame)) {
      acceptCoFrame(coFrame, now);
      coFrameLength = 0;
    }
  }
}

void updateConditionTimer(bool condition, uint32_t now, uint32_t& started) {
  if (condition) {
    if (started == 0) started = now == 0 ? 1 : now;
  } else {
    started = 0;
  }
}

void recordCoHistory(uint32_t now) {
  if (!coFresh(now) || static_cast<uint32_t>(now - coLastHistoryMs) < 1000) return;
  if (coLastHistoryMs != 0 && static_cast<uint32_t>(now - coLastHistoryMs) > 2000) {
    coHistoryCount = 0;
    coHistoryIndex = 0;
  }
  coLastHistoryMs = now;
  coHistory[coHistoryIndex] = coPpm;
  coHistoryIndex = (coHistoryIndex + 1) % CO_HISTORY_SECONDS;
  if (coHistoryCount < CO_HISTORY_SECONDS) ++coHistoryCount;
}

float coHistoryMinimum() {
  if (coHistoryCount == 0) return coPpm;
  float minimum = coHistory[0];
  for (size_t index = 1; index < coHistoryCount; ++index) minimum = min(minimum, coHistory[index]);
  return minimum;
}

void evaluateCo(uint32_t now) {
  recordCoHistory(now);
  if (!coFresh(now)) {
    coWarning = false;
    coAbove35SinceMs = 0;
    coAbove70SinceMs = 0;
    coAbove150SinceMs = 0;
    coAbove400SinceMs = 0;
    coSafeSinceMs = 0;
    return;  // 센서 단선 중에는 기존 경보 래치를 임의로 해제하지 않습니다.
  }

  updateConditionTimer(coPpm >= 35.0f, now, coAbove35SinceMs);
  updateConditionTimer(coPpm >= 70.0f, now, coAbove70SinceMs);
  updateConditionTimer(coPpm >= 150.0f, now, coAbove150SinceMs);
  updateConditionTimer(coPpm >= 400.0f, now, coAbove400SinceMs);
  const bool rapidRise = coHistoryCount >= 2 && coPpm - coHistoryMinimum() >= 20.0f;
  const bool warningHold = elapsedAtLeast(now, coAbove35SinceMs, CO_WARNING_HOLD_MS);
  coWarning = warningHold || rapidRise;

  const bool alarmCondition =
      coPpm >= CO_IMMEDIATE_ALARM_PPM ||
      elapsedAtLeast(now, coAbove70SinceMs, 3600000) ||
      elapsedAtLeast(now, coAbove150SinceMs, 600000) ||
      elapsedAtLeast(now, coAbove400SinceMs, 240000);
  if (alarmCondition) {
    coAlarmLatched = true;
    coSafeSinceMs = 0;
  } else if (coPpm < CO_CLEAR_PPM) {
    if (coSafeSinceMs == 0) coSafeSinceMs = now == 0 ? 1 : now;
    if (elapsedAtLeast(now, coSafeSinceMs, CO_CLEAR_HOLD_MS)) coAlarmLatched = false;
  } else {
    coSafeSinceMs = 0;
  }
}

void updatePhysicalAlarm(uint32_t now) {
  bool buzzer = false;
  bool vibration = false;
  bool strobe = false;
  if (trailAlertLevel != 0 &&
      static_cast<uint32_t>(now - trailAlertRefreshMs) > TRAIL_ALERT_WATCHDOG_MS) {
    trailAlertLevel = 0;
    sendOutputEvent("off", false);
  }
  if (coAlarmLatched) {
    buzzer = now % 1000 < 500;
    vibration = now % 1000 < 700;
    strobe = (now / 250) % 2 == 0;
  } else if (coWarning) {
    const uint32_t phase = now % 10000;
    buzzer = phase < 180;
    vibration = phase < 350;
  }
  if (trailAlertLevel != 0 && !coAlarmLatched) {
    if (trailAlertLevel == 2) {
      const uint32_t phase = now % 2000;
      vibration = vibration || phase < 300 || (phase >= 500 && phase < 800);
    } else {
      vibration = vibration || now % 5000 < 200;
    }
  }
  digitalWrite(BUZZER_PIN, buzzer ? HIGH : LOW);
  digitalWrite(VIBRATION_PIN, vibration ? HIGH : LOW);
  digitalWrite(STROBE_PIN, strobe ? HIGH : LOW);
}

void appendNullableFloat(Print& output, float value, uint8_t digits) {
  if (isfinite(value)) output.print(value, digits);
  else output.print("null");
}

void appendGpsJson(Print& output, uint32_t now) {
  const bool fix = gpsFixLive(now);
  output.print("\"gps\":{\"fix\":");
  output.print(fix ? "true" : "false");
  if (fix) {
    output.print(",\"lat\":"); output.print(gpsLatitude, 7);
    output.print(",\"lon\":"); output.print(gpsLongitude, 7);
    output.print(",\"acc_m\":null");  // HDOP를 미터 정확도로 꾸미지 않습니다.
    output.print(",\"hdop\":"); appendNullableFloat(output, gpsHdop, 1);
    output.print(",\"sats\":"); output.print(gpsSatellites);
    output.print(",\"age_s\":"); output.print((now - gpsLastFixMs) / 1000.0f, 1);
  } else {
    output.print(",\"sats\":"); output.print(gpsSatellites);
    output.print(",\"hdop\":"); appendNullableFloat(output, gpsHdop, 1);
    output.print(",\"acc_m\":null,\"age_s\":null,\"last_age_s\":");
    if (gpsHasLastFix) output.print((now - gpsLastFixMs) / 1000.0f, 1);
    else output.print("null");
  }
  output.print("}");
}

void appendEnvironmentJson(Print& output, uint32_t now) {
  const bool valid = environmentFresh(now);
  const bool pressureValid = pressureFresh(now);
  output.print(",\"env\":{\"valid\":"); output.print(valid ? "true" : "false");
  output.print(",\"sht_valid\":"); output.print(valid ? "true" : "false");
  output.print(",\"pressure_valid\":"); output.print(pressureValid ? "true" : "false");
  output.print(",\"temp_c\":");
  if (valid) output.print(temperatureC, 2); else output.print("null");
  output.print(",\"humidity_pct\":");
  if (valid) output.print(humidityPct, 2); else output.print("null");
  output.print(",\"press_hpa\":");
  if (pressureValid) output.print(pressureHpa, 2); else output.print("null");
  output.print(",\"press_trend\":\"");
  output.print(pressureValid ? pressureTrendName() : "unknown");
  output.print("\"");
  output.print(",\"age_s\":");
  if (environmentHasValue) output.print((now - environmentLastUpdateMs) / 1000.0f, 1);
  else output.print("null");
  output.print(",\"press_age_s\":");
  if (pressureHasValue) output.print((now - pressureLastUpdateMs) / 1000.0f, 1);
  else output.print("null");
  output.print(",\"bmp_address\":");
  if (bmp390Address != 0) output.print(bmp390Address); else output.print("null");
  output.print("}");
}

void appendRtcJson(Print& output, uint32_t now) {
  const bool valid = rtcValid &&
      static_cast<uint32_t>(now - rtcLastUpdateMs) <= SENSOR_STALE_MS;
  output.print(",\"rtc\":{\"valid\":"); output.print(valid ? "true" : "false");
  output.print(",\"iso_utc\":");
  if (valid) {
    output.print("\""); output.print(rtcIsoUtc); output.print("\"");
  } else {
    output.print("null");
  }
  output.print(",\"age_s\":");
  if (rtcLastUpdateMs != 0) output.print((now - rtcLastUpdateMs) / 1000.0f, 1);
  else output.print("null");
  output.print("}");
}

void appendCoJson(Print& output, uint32_t now) {
  const bool fresh = coFresh(now);
  const bool warmingUp = now < CO_WARMUP_MS;
  const bool valid = fresh && !warmingUp;
  const char* level = coAlarmLatched ? "alarm" : coWarning ? "warning" : fresh ? "normal" : "unknown";
  output.print(",\"co\":{\"valid\":"); output.print(valid ? "true" : "false");
  output.print(",\"warming_up\":"); output.print(warmingUp ? "true" : "false");
  output.print(",\"ppm\":");
  if (coHasValue) output.print(coPpm, 1); else output.print("null");
  output.print(",\"level\":\""); output.print(level); output.print("\"");
  output.print(",\"alarm\":"); output.print(coAlarmLatched ? "true" : "false");
  output.print(",\"age_s\":");
  if (coHasValue) output.print((now - coLastUpdateMs) / 1000.0f, 1);
  else output.print("null");
  output.print("}");
}

void sendTelemetry(uint32_t now) {
  char base[760];
  FixedBufferWriter output(base, sizeof(base));
  output.print("{\"v\":1,\"event\":\"telemetry\",\"seq\":");
  output.print(telemetrySequence++);
  output.print(",\"uptime_ms\":"); output.print(now);
  output.print(","); appendGpsJson(output, now);
  appendRtcJson(output, now);
  appendEnvironmentJson(output, now);
  appendCoJson(output, now);
  output.print(",\"power\":{\"valid\":false,\"percent\":null,\"days_left\":null");
  output.print(",\"jetson_gate_on\":"); output.print(jetsonPowerGateOn ? "true" : "false");
  output.print(",\"shutdown_pending\":"); output.print(powerShutdownPending ? "true" : "false");
  output.print("}}");
  if (output.overflowed() || output.length() < 2) return;

  sendChecksummedJson(base, output.length());
}

void sendFix(uint32_t now) {
  Serial.print("{\"ok\":true,\"event\":\"fix\",\"fix\":");
  const bool fix = gpsFixLive(now);
  Serial.print(fix ? "true" : "false");
  if (fix) {
    Serial.print(",\"lat\":"); Serial.print(gpsLatitude, 7);
    Serial.print(",\"lon\":"); Serial.print(gpsLongitude, 7);
    Serial.print(",\"acc_m\":null,\"sats\":"); Serial.print(gpsSatellites);
    Serial.print(",\"age_s\":"); Serial.print((now - gpsLastFixMs) / 1000.0f, 1);
  } else {
    Serial.print(",\"last_age_s\":");
    if (gpsHasLastFix) Serial.print((now - gpsLastFixMs) / 1000.0f, 1);
    else Serial.print("null");
  }
  Serial.println("}");
}

void handleHostCommand(char* command, uint32_t now) {
  while (*command == ' ') ++command;
  char* end = command + strlen(command);
  while (end > command && isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
  if (strcmp(command, "STREAM ON") == 0) {
    streamEnabled = true;
    Serial.println("{\"ok\":true,\"event\":\"stream\",\"enabled\":true}");
  } else if (strcmp(command, "STREAM OFF") == 0) {
    streamEnabled = false;
    Serial.println("{\"ok\":true,\"event\":\"stream\",\"enabled\":false}");
  } else if (strcmp(command, "GET_TELEMETRY") == 0) {
    sendTelemetry(now);
  } else if (strcmp(command, "GET_FIX") == 0) {
    sendFix(now);
  } else if (strcmp(command, "ALERT TRAIL ON") == 0) {
    trailAlertLevel = 2;
    trailAlertRefreshMs = now;
    sendOutputEvent("alert", true);
  } else if (strcmp(command, "ALERT TRAIL CAUTION") == 0) {
    trailAlertLevel = 1;
    trailAlertRefreshMs = now;
    sendOutputEvent("caution", true);
  } else if (strcmp(command, "ALERT TRAIL OFF") == 0) {
    trailAlertLevel = 0;
    sendOutputEvent("off", false);
  } else if (strcmp(command, "POWER OFF ACK") == 0) {
    if (jetsonPowerGateOn && powerShutdownPending) {
      if (!powerGateOffScheduled) {
        powerGateOffScheduled = true;
        powerGateOffScheduledMs = now;
      }
      sendPowerEvent("shutdown_ack");
    } else {
      Serial.println("{\"ok\":false,\"event\":\"error\",\"reason\":\"power_not_pending\"}");
    }
  } else if (strcmp(command, "POWER OFF CANCEL") == 0) {
    if (jetsonPowerGateOn && powerShutdownPending) {
      powerGateOffScheduled = false;
      powerShutdownPending = false;
      sendPowerEvent("shutdown_cancelled");
    } else {
      Serial.println("{\"ok\":false,\"event\":\"error\",\"reason\":\"power_not_pending\"}");
    }
  } else if (strcmp(command, "POWER STATUS") == 0) {
    sendPowerEvent("status");
  } else if (strncmp(command, "SET RTC UTC ", 12) == 0) {
    if (provisionRtcUtc(command + 12, now)) {
      Serial.print("{\"ok\":true,\"event\":\"rtc_set\",\"iso_utc\":\"");
      Serial.print(rtcIsoUtc);
      Serial.println("\"}");
    } else {
      Serial.println("{\"ok\":false,\"event\":\"error\",\"reason\":\"rtc_set_failed\"}");
    }
  } else if (strcmp(command, "PING") == 0) {
    Serial.println("{\"ok\":true,\"event\":\"pong\",\"v\":1}");
  } else if (*command != '\0') {
    Serial.println("{\"ok\":false,\"event\":\"error\",\"reason\":\"unknown_command\"}");
  }
}

void pumpHost(uint32_t now) {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\r') continue;
    if (character == '\n') {
      hostCommand[hostCommandLength] = '\0';
      handleHostCommand(hostCommand, now);
      hostCommandLength = 0;
      continue;
    }
    if (hostCommandLength + 1 < sizeof(hostCommand)) hostCommand[hostCommandLength++] = character;
    else hostCommandLength = 0;
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, OUTPUT);
  pinMode(STROBE_PIN, OUTPUT);
  pinMode(JETSON_POWER_GATE_PIN, OUTPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CHECKPOINT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(VOICE_BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(VIBRATION_PIN, LOW);
  digitalWrite(STROBE_PIN, LOW);
  digitalWrite(JETSON_POWER_GATE_PIN, HIGH);

  Serial.begin(HOST_BAUD);
  GpsSerial.begin(GPS_BAUD);
  CoSerial.begin(CO_BAUD);
  Wire.setSDA(PB9);
  Wire.setSCL(PB8);
  Wire.begin();
  initializeBmp390(millis());
  Serial.println("{\"ok\":true,\"event\":\"boot\",\"v\":1,\"role\":\"sensor_hub\"}");
}

void loop() {
  const uint32_t now = millis();
  pumpGps(now);
  pumpCo(now);
  updateEnvironment(now);
  updatePressure(now);
  updateRtc(now);
  evaluateCo(now);
  updatePhysicalAlarm(now);
  updateButtons(now);
  updatePowerGate(now);
  pumpHost(now);
  if (streamEnabled && static_cast<uint32_t>(now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    sendTelemetry(now);
  }
}
