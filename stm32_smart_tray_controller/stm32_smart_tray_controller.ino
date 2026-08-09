#include <Arduino.h>
#include <Wire.h>

// STM32F401RE / STM32duino 3.x 기준 핀. Uart 생성자 순서는 RX, TX입니다.
Uart GpsSerial(PA10, PA9);   // USART1: Air530 TX -> PA10
Uart CoSerial(PC7, PC6);     // USART6: ZE07-CO TX -> PC7

constexpr uint32_t BUZZER_PIN = PB0;
constexpr uint32_t VIBRATION_PIN = PB1;
constexpr uint32_t STROBE_PIN = PC8;
constexpr uint8_t SHT40_ADDRESS = 0x44;

constexpr uint32_t HOST_BAUD = 115200;
constexpr uint32_t GPS_BAUD = 9600;
constexpr uint32_t CO_BAUD = 9600;
constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;
constexpr uint32_t GPS_STALE_MS = 3000;
constexpr uint32_t SENSOR_STALE_MS = 3000;
constexpr uint32_t CO_WARMUP_MS = 300000;  // 제조사 첫 사용 주의사항: 최소 5분

constexpr float CO_WARNING_PPM = 35.0f;
constexpr uint32_t CO_WARNING_HOLD_MS = 180000;
constexpr float CO_IMMEDIATE_ALARM_PPM = 100.0f;
constexpr float CO_CLEAR_PPM = 30.0f;
constexpr uint32_t CO_CLEAR_HOLD_MS = 30000;
constexpr size_t CO_HISTORY_SECONDS = 600;

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

char hostCommand[64];
size_t hostCommandLength = 0;
bool streamEnabled = true;
uint32_t telemetrySequence = 0;
uint32_t lastTelemetryMs = 0;

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

bool elapsedAtLeast(uint32_t now, uint32_t started, uint32_t duration) {
  return started != 0 && static_cast<uint32_t>(now - started) >= duration;
}

bool gpsFixLive(uint32_t now) {
  return gpsCurrentFix && gpsHasLastFix && static_cast<uint32_t>(now - gpsLastSentenceMs) <= GPS_STALE_MS;
}

bool environmentFresh(uint32_t now) {
  return environmentHasValue && static_cast<uint32_t>(now - environmentLastUpdateMs) <= SENSOR_STALE_MS;
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
  if (coAlarmLatched) {
    buzzer = now % 1000 < 500;
    vibration = now % 1000 < 700;
    strobe = (now / 250) % 2 == 0;
  } else if (coWarning) {
    const uint32_t phase = now % 10000;
    buzzer = phase < 180;
    vibration = phase < 350;
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
  output.print(",\"env\":{\"valid\":"); output.print(valid ? "true" : "false");
  output.print(",\"temp_c\":");
  if (valid) output.print(temperatureC, 2); else output.print("null");
  output.print(",\"humidity_pct\":");
  if (valid) output.print(humidityPct, 2); else output.print("null");
  output.print(",\"age_s\":");
  if (environmentHasValue) output.print((now - environmentLastUpdateMs) / 1000.0f, 1);
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
  char base[620];
  FixedBufferWriter output(base, sizeof(base));
  output.print("{\"v\":1,\"event\":\"telemetry\",\"seq\":");
  output.print(telemetrySequence++);
  output.print(",\"uptime_ms\":"); output.print(now);
  output.print(","); appendGpsJson(output, now);
  appendEnvironmentJson(output, now);
  appendCoJson(output, now);
  output.print(",\"power\":{\"valid\":false,\"percent\":null,\"days_left\":null}}");
  if (output.overflowed() || output.length() < 2) return;

  const uint16_t checksum = crc16Ccitt(reinterpret_cast<const uint8_t*>(output.data()), output.length());
  base[output.length() - 1] = '\0';
  char checksumText[5];
  snprintf(checksumText, sizeof(checksumText), "%04X", checksum);
  Serial.print(base);
  Serial.print(",\"crc16\":\"");
  Serial.print(checksumText);
  Serial.println("\"}");
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
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(VIBRATION_PIN, LOW);
  digitalWrite(STROBE_PIN, LOW);

  Serial.begin(HOST_BAUD);
  GpsSerial.begin(GPS_BAUD);
  CoSerial.begin(CO_BAUD);
  Wire.setSDA(PB9);
  Wire.setSCL(PB8);
  Wire.begin();
  Serial.println("{\"ok\":true,\"event\":\"boot\",\"v\":1,\"role\":\"sensor_hub\"}");
}

void loop() {
  const uint32_t now = millis();
  pumpGps(now);
  pumpCo(now);
  updateEnvironment(now);
  evaluateCo(now);
  updatePhysicalAlarm(now);
  pumpHost(now);
  if (streamEnabled && static_cast<uint32_t>(now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    sendTelemetry(now);
  }
}
