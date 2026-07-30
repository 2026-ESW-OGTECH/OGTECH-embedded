#include <Arduino.h>
#include <Servo.h>

const int LAYER_COUNT = 3;
const int SERVO_COUNT = 4;
const int CELL_COUNT = 10;

const uint8_t SERVO_PINS[SERVO_COUNT] = {PA0, PA1, PA2, PA3};
const uint8_t CELL_LED_PINS[CELL_COUNT] = {PB0, PB1, PB2, PB3, PB4, PB5, PB6, PB7, PB8, PB9};
const uint8_t STOCK_SENSOR_PINS[CELL_COUNT] = {PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PC8, PC9};
const uint8_t BATTERY_ADC_PIN = PA4;

const float BATTERY_DIVIDER_RATIO = 5.0f;
const float BATTERY_EMPTY_VOLTAGE = 10.0f;
const float BATTERY_FULL_VOLTAGE = 14.6f;

const char* CELL_IDS[CELL_COUNT] = {
  "1-1", "1-2", "1-3",
  "2-1", "2-2", "2-3", "2-4", "2-5", "2-6",
  "3-1",
};

const int SERVO_LOCK_ANGLE = 15;
const int SERVO_RELEASE_ANGLE = 70;
const unsigned long RELEASE_MS = 650;

Servo actuatorServos[SERVO_COUNT];
int openLayer = 0;
String activeCell = "";

void printOk(const char* event) {
  Serial.print("{\"ok\":true,\"event\":\"");
  Serial.print(event);
  Serial.println("\"}");
}

void printError(const char* reason) {
  Serial.print("{\"ok\":false,\"reason\":\"");
  Serial.print(reason);
  Serial.println("\"}");
}

int cellIndex(String cell) {
  cell.trim();
  for (int i = 0; i < CELL_COUNT; i++) {
    if (cell == CELL_IDS[i]) return i;
  }
  return -1;
}

void clearCellLeds() {
  for (int i = 0; i < CELL_COUNT; i++) {
    digitalWrite(CELL_LED_PINS[i], LOW);
  }
  activeCell = "";
}

void setCellLed(String cell) {
  int index = cellIndex(cell);
  if (index < 0) {
    printError("invalid_cell");
    return;
  }
  clearCellLeds();
  digitalWrite(CELL_LED_PINS[index], HIGH);
  activeCell = cell;
  Serial.print("{\"ok\":true,\"event\":\"set_cell_led\",\"cell\":\"");
  Serial.print(cell);
  Serial.println("\"}");
}

void openLayerLatch(int layer) {
  if (layer < 1 || layer > LAYER_COUNT) {
    printError("invalid_layer");
    return;
  }
  Servo& servo = actuatorServos[layer - 1];
  servo.write(SERVO_RELEASE_ANGLE);
  delay(RELEASE_MS);
  servo.write(SERVO_LOCK_ANGLE);
  openLayer = layer;
  Serial.print("{\"ok\":true,\"event\":\"open_layer\",\"layer\":");
  Serial.print(layer);
  Serial.println("}");
}

void closeAll() {
  openLayer = 0;
  clearCellLeds();
  printOk("close_all");
}

void readStock() {
  Serial.print("{\"ok\":true,\"event\":\"stock\",\"sensors\":{");
  for (int i = 0; i < CELL_COUNT; i++) {
    bool present = digitalRead(STOCK_SENSOR_PINS[i]) == LOW;
    Serial.print("\"stock_");
    String cell = String(CELL_IDS[i]);
    cell.replace("-", "_");
    Serial.print(cell);
    Serial.print("\":");
    Serial.print(present ? "true" : "false");
    if (i < CELL_COUNT - 1) Serial.print(",");
  }
  Serial.println("}}");
}

void readBattery() {
  int raw = analogRead(BATTERY_ADC_PIN);
  float adcVoltage = (raw / 1023.0f) * 3.3f;
  float packVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;
  int percent = constrain(
    (int)((packVoltage - BATTERY_EMPTY_VOLTAGE) * 100.0f / (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)),
    0,
    100
  );
  Serial.print("{\"ok\":true,\"event\":\"battery\",\"voltage\":");
  Serial.print(packVoltage, 2);
  Serial.print(",\"percent\":");
  Serial.print(percent);
  Serial.println("}");
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("OPEN_LAYER ")) {
    int layer = line.substring(11).toInt();
    openLayerLatch(layer);
    return;
  }
  if (line.startsWith("SET_CELL_LED ")) {
    setCellLed(line.substring(13));
    return;
  }
  if (line == "CLOSE_ALL") {
    closeAll();
    return;
  }
  if (line == "READ_STOCK") {
    readStock();
    return;
  }
  if (line == "GET_BATTERY") {
    readBattery();
    return;
  }
  printError("unknown_command");
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < SERVO_COUNT; i++) {
    actuatorServos[i].attach(SERVO_PINS[i]);
    actuatorServos[i].write(SERVO_LOCK_ANGLE);
  }
  for (int i = 0; i < CELL_COUNT; i++) {
    pinMode(CELL_LED_PINS[i], OUTPUT);
    digitalWrite(CELL_LED_PINS[i], LOW);
    pinMode(STOCK_SENSOR_PINS[i], INPUT_PULLUP);
  }
  printOk("boot");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleCommand(line);
  }
}
