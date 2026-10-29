#include <HTTPClient.h>
#include <WiFi.h>
#include <string.h>

#if __has_include("credentials.h")
#include "credentials.h"
#else
#include "credentials.example.h"
#endif

// For a classic ESP32 DevKit, GPIO 34 and 35 are ADC input pins.
// Connect sensor AO pins here and connect all sensor grounds to ESP32 GND.
// Keep sensor AO voltage at or below 3.3 V.
const int RAIN_PIN = 34;
const int SOUND_PIN = 35;
const int BUZZER_PIN = 23;

// Calibrate these against the raw ADC readings from your sensors.
const int RAIN_DRY_ADC = 4095;
const int RAIN_WET_ADC = 0;
const int SOUND_QUIET_RANGE = 10;
const int SOUND_LOUD_RANGE = 800;
const int RAIN_ALERT_THRESHOLD = 50;
const int SOUND_ALERT_THRESHOLD = 70;
const int BUZZER_ON_MS = 100;
const int BUZZER_OFF_MS = 300;

volatile bool buzzerAlert = false;

int toPercent(int value, int atZero, int atHundred) {
  if (atZero == atHundred) {
    return 0;
  }

  long percent = (long)(value - atZero) * 100 / (atHundred - atZero);
  return constrain(percent, 0, 100);
}

int readRainAdc() {
  long total = 0;
  for (int i = 0; i < 10; i++) {
    total += analogRead(RAIN_PIN);
    delay(5);
  }
  return total / 10;
}

int readSoundRange() {
  int minimum = 4095;
  int maximum = 0;
  unsigned long startedAt = millis();

  while (millis() - startedAt < 40) {
    int sample = analogRead(SOUND_PIN);
    if (sample < minimum) minimum = sample;
    if (sample > maximum) maximum = sample;
  }

  return maximum - minimum;
}

bool wifiIsConfigured() {
  return strcmp(WIFI_SSID, "YOUR_WIFI_NAME") != 0 &&
         strcmp(WIFI_PASSWORD, "YOUR_WIFI_PASSWORD") != 0 &&
         strcmp(SERVER_URL, "http://YOUR_PI_IP:5000/api/sensors") != 0;
}

void buzzerTask(void *parameter) {
  for (;;) {
    if (buzzerAlert) {
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(BUZZER_ON_MS));
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(pdMS_TO_TICKS(BUZZER_OFF_MS));
    } else {
      digitalWrite(BUZZER_PIN, LOW);
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  }
}

void connectToWifi() {
  if (!wifiIsConfigured()) {
    Serial.println("Set Wi-Fi details in include/credentials.h before uploading.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected. ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  xTaskCreate(buzzerTask, "buzzer", 2048, nullptr, 1, nullptr);
  connectToWifi();
}

void loop() {
  if (!wifiIsConfigured()) {
    delay(5000);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(1000);
    return;
  }

  int rainAdc = readRainAdc();
  int soundRange = readSoundRange();
  int wetness = toPercent(rainAdc, RAIN_DRY_ADC, RAIN_WET_ADC);
  int sound = toPercent(soundRange, SOUND_QUIET_RANGE, SOUND_LOUD_RANGE);
  bool alert = wetness >= RAIN_ALERT_THRESHOLD ||
               sound >= SOUND_ALERT_THRESHOLD;
  buzzerAlert = alert;

  Serial.printf(
    "Rain ADC: %d, sound range: %d, wetness: %d%%, sound: %d%%, buzzer: %s\n",
    rainAdc, soundRange, wetness, sound, alert ? "ON" : "OFF"
  );

  HTTPClient http;
  http.setTimeout(2000);
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"wetness\":" + String(wetness) +
                   ",\"sound\":" + String(sound) + "}";
  int responseCode = http.POST(payload);
  Serial.printf("Raspberry Pi response: %d\n", responseCode);
  http.end();

  delay(1000);
}
