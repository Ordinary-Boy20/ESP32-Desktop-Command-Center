#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <time.h>

// ============================================
// DISPLAY CONFIGURATION
// ============================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// ============================================
// PIN CONFIGURATION
// ============================================

#define UP_BUTTON     18
#define DOWN_BUTTON   19
#define SELECT_BUTTON 23
#define BACK_BUTTON   25
#define START_BUTTON  26

#define BUZZER_PIN 27

#define DHT_PIN  4
#define DHT_TYPE DHT11

// HC-SR04 ultrasonic distance sensor
#define TRIG_PIN 32
#define ECHO_PIN 33

// PIR motion sensor (OUT pin)
#define PIR_PIN 14

DHT dht(DHT_PIN, DHT_TYPE);

void showMotionDetector();

// ============================================
// MENU
// ============================================

const char* menuItems[] = {
  "Clock",
  "Pomodoro Timer",
  "Temperature",
  "Distance Scanner",
  "Motion Detector"
};

const int MENU_COUNT = 5;

int selectedItem = 0;
bool inMenu = false;
bool dashboardHome = true;

const char* WIFI_SSID = "POCO C3";
const char* WIFI_PASSWORD = "12345678";
const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;
bool clockReady = false;
bool wifiConnected = false;
unsigned long lastWiFiAttempt = 0;
const unsigned long WIFI_RETRY_INTERVAL = 30000;
const char* daysOfWeek[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
unsigned long lastDashboardRefresh = 0;

// ============================================
// POMODORO TIMER
// ============================================

const int MIN_DURATION = 1;
const int MAX_DURATION = 180;  // Up to 3 hours

int durationMinutes = 30;  // Default timer: 30 minutes
int durationStepMinutes = 5; // SELECT toggles between 1-minute and 5-minute steps
int remainingSeconds = 30 * 60;

bool timerRunning = false;
bool timerFinished = false;

unsigned long lastTick = 0;

// ============================================
// TEMPERATURE MONITOR
// ============================================

float temperature = NAN;
float humidity = NAN;

unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 2000;

// ============================================
// DISTANCE SCANNER
// ============================================

float distanceCm = NAN;
float filteredDistanceCm = NAN;
unsigned long lastDistanceRead = 0;
unsigned long lastValidDistanceRead = 0;
const unsigned long DISTANCE_INTERVAL = 180;
const unsigned long ECHO_TIMEOUT_US = 30000;
unsigned long lastProximityBeep = 0;
bool proximityBuzzerOn = false;
unsigned long buzzerPhaseStarted = 0;

// PIR motion detection
bool motionDetected = false;
unsigned long lastMotionScreenRefresh = 0;
unsigned long pirWarmupStarted = 0;
const unsigned long PIR_WARMUP_MS = 30000;

// ============================================
// BUTTON STATES
// ============================================

bool lastUp = HIGH;
bool lastDown = HIGH;
bool lastSelect = HIGH;
bool lastBack = HIGH;
bool lastStart = HIGH;

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(115200);

  pinMode(UP_BUTTON, INPUT_PULLUP);
  pinMode(DOWN_BUTTON, INPUT_PULLUP);
  pinMode(SELECT_BUTTON, INPUT_PULLUP);
  pinMode(BACK_BUTTON, INPUT_PULLUP);
  pinMode(START_BUTTON, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(PIR_PIN, INPUT);
  pirWarmupStarted = millis();

  Wire.begin(21, 22);

  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDRESS
      )) {
    Serial.println("OLED initialization failed!");

    while (true) {
      delay(1000);
    }
  }

  dht.begin();
  connectAndSyncClock();
  readDHT();
  dashboardHome = true;
  inMenu = false;
  showDashboard();
  Serial.println("Desktop Command Center ready!");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  bool up = digitalRead(UP_BUTTON);
  bool down = digitalRead(DOWN_BUTTON);
  bool select = digitalRead(SELECT_BUTTON);
  bool back = digitalRead(BACK_BUTTON);
  bool start = digitalRead(START_BUTTON);

  if (dashboardHome) {
    if (lastSelect == HIGH && select == LOW) {
      dashboardHome = false;
      inMenu = true;
      showMenu();
    }
    if (millis() - lastDashboardRefresh >= 1000) {
      lastDashboardRefresh = millis();
      if (millis() - lastDHTRead >= DHT_INTERVAL) readDHT();
      showDashboard();
    }
  } else if (inMenu) {
    if (lastUp == HIGH && up == LOW) {
      selectedItem--;
      if (selectedItem < 0) selectedItem = MENU_COUNT - 1;
      showMenu();
    }
    if (lastDown == HIGH && down == LOW) {
      selectedItem++;
      if (selectedItem >= MENU_COUNT) selectedItem = 0;
      showMenu();
    }
    if (lastSelect == HIGH && select == LOW) {
      inMenu = false;
      if (selectedItem == 0) {
        showClock();
      } else if (selectedItem == 1) {
        resetTimer();
        showTimer();
      } else if (selectedItem == 2) {
        readDHT();
        showTemperature();
      } else if (selectedItem == 3) {
        distanceCm = NAN;
        filteredDistanceCm = NAN;
        lastDistanceRead = 0;
        lastValidDistanceRead = 0;
        showDistanceScanner();
      } else {
        motionDetected = digitalRead(PIR_PIN) == HIGH;
        lastMotionScreenRefresh = 0;
        showMotionDetector();
      }
    }
    if (lastBack == HIGH && back == LOW) {
      inMenu = false;
      dashboardHome = true;
      showDashboard();
    }
  } else if (selectedItem == 0) {
    if (lastBack == HIGH && back == LOW) {
      dashboardHome = true;
      showDashboard();
    }
    static unsigned long lastClockRefresh = 0;
    if (millis() - lastClockRefresh >= 1000) {
      lastClockRefresh = millis();
      showClock();
    }
  } else if (selectedItem == 1) {
    handleTimer(up, down, select, back, start);
  } else if (selectedItem == 2) {
    if (lastBack == HIGH && back == LOW) {
      dashboardHome = true;
      digitalWrite(BUZZER_PIN, LOW);
      showDashboard();
    }
    updateTemperature();
  } else if (selectedItem == 3) {
    if (lastBack == HIGH && back == LOW) {
      dashboardHome = true;
      proximityBuzzerOn = false;
      digitalWrite(BUZZER_PIN, LOW);
      showDashboard();
    } else {
      updateDistanceScanner();
    }
  } else if (selectedItem == 4) {
    if (lastBack == HIGH && back == LOW) {
      dashboardHome = true;
      showDashboard();
    } else if (millis() - lastMotionScreenRefresh >= 200) {
      lastMotionScreenRefresh = millis();
      motionDetected = digitalRead(PIR_PIN) == HIGH;
      showMotionDetector();
    }
  }

  lastUp = up;
  lastDown = down;
  lastSelect = select;
  lastBack = back;
  lastStart = start;

  updateTimer();
  maintainWiFi();
  delay(20);
}

// TIMER CONTROLS
// ============================================

void handleTimer(
  bool up,
  bool down,
  bool select,
  bool back,
  bool start
) {
  // Increase duration while stopped. Default step is 5 minutes.
  if (lastUp == HIGH && up == LOW && !timerRunning && !timerFinished) {
    durationMinutes += durationStepMinutes;
    if (durationMinutes > MAX_DURATION) durationMinutes = MAX_DURATION;
    remainingSeconds = durationMinutes * 60;
    digitalWrite(BUZZER_PIN, LOW);
    showTimer();
  }

  // Decrease duration while stopped.
  if (lastDown == HIGH && down == LOW && !timerRunning && !timerFinished) {
    durationMinutes -= durationStepMinutes;
    if (durationMinutes < MIN_DURATION) durationMinutes = MIN_DURATION;
    remainingSeconds = durationMinutes * 60;
    digitalWrite(BUZZER_PIN, LOW);
    showTimer();
  }

  // SELECT switches adjustment step between 1 and 5 minutes.
  if (lastSelect == HIGH && select == LOW && !timerRunning && !timerFinished) {
    durationStepMinutes = (durationStepMinutes == 5) ? 1 : 5;
    showTimer();
  }

  // START runs/pauses, or resets after completion.
  if (lastStart == HIGH && start == LOW) {
    if (timerFinished) {
      digitalWrite(BUZZER_PIN, LOW);
      resetTimer();
      showTimer();
    } else {
      timerRunning = !timerRunning;
      lastTick = millis();
      showTimer();
    }
  }

  // BACK returns to the dashboard and stops the timer.
  if (lastBack == HIGH && back == LOW) {
    timerRunning = false;
    digitalWrite(BUZZER_PIN, LOW);
    inMenu = false;
    dashboardHome = true;
    showDashboard();
  }
}

// ============================================
// TIMER UPDATE
// ============================================

void updateTimer() {

  if (!timerRunning || timerFinished) {
    return;
  }

  unsigned long now = millis();

  if (now - lastTick >= 1000) {

    unsigned long elapsedSeconds =
      (now - lastTick) / 1000;

    lastTick += elapsedSeconds * 1000;

    if (elapsedSeconds >= (unsigned long)remainingSeconds) {

      remainingSeconds = 0;

      timerRunning = false;
      timerFinished = true;

      Serial.println("Pomodoro session complete!");

      digitalWrite(BUZZER_PIN, HIGH);

    } else {

      remainingSeconds -= elapsedSeconds;
    }

    if (!inMenu && selectedItem == 1) {
      showTimer();
    }
  }
}

// ============================================
// RESET TIMER
// ============================================

void resetTimer() {

  timerRunning = false;
  timerFinished = false;

  remainingSeconds = durationMinutes * 60;

  digitalWrite(BUZZER_PIN, LOW);
}

// ============================================
// READ DHT11
// ============================================

void readDHT() {

  float newHumidity = dht.readHumidity();
  float newTemperature = dht.readTemperature();

  if (!isnan(newHumidity) && !isnan(newTemperature)) {
    humidity = newHumidity;
    temperature = newTemperature;

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" C");

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.println("DHT11 reading failed!");
  }

  lastDHTRead = millis();
}

// ============================================
// UPDATE TEMPERATURE
// ============================================

void updateTemperature() {

  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    readDHT();
    showTemperature();
  }
}

// ============================================
// DISPLAY MENU
// ============================================

void showMenu() {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("DESKTOP COMMAND");

  display.drawLine(
    0, 11, 127, 11,
    SSD1306_WHITE
  );

  for (int i = 0; i < MENU_COUNT; i++) {

    display.setCursor(0, 16 + i * 12);

    display.print(i == selectedItem ? "> " : "  ");
    display.println(menuItems[i]);
  }

  display.display();
}

// ============================================
// DISPLAY POMODORO
// ============================================

void showTimer() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("CUSTOM TIMER");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setTextSize(2);
  if (remainingSeconds >= 3600) {
    int hours = remainingSeconds / 3600;
    int minutes = (remainingSeconds % 3600) / 60;
    int seconds = remainingSeconds % 60;
    display.setCursor(16, 18);
    display.printf("%02d:%02d:%02d", hours, minutes, seconds);
  } else {
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    display.setCursor(25, 18);
    display.printf("%02d:%02d", minutes, seconds);
  }

  display.setTextSize(1);
  display.setCursor(0, 42);
  if (timerFinished) display.println("Status: COMPLETE");
  else if (timerRunning) display.println("Status: RUNNING");
  else display.println("Status: READY/PAUSED");

  display.setCursor(0, 54);
  display.printf("UP/DN:+/-%dm SEL:Step", durationStepMinutes);
  display.display();
}

// ============================================
// DISPLAY TEMPERATURE
// ============================================

void showTemperature() {

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("ROOM MONITOR");

  display.drawLine(
    0, 11, 127, 11,
    SSD1306_WHITE
  );

  display.setCursor(0, 19);

  if (isnan(temperature) || isnan(humidity)) {

    display.println("Sensor unavailable");

  } else {

    display.print("Temperature: ");
    display.print(temperature, 1);
    display.println(" C");

    display.setCursor(0, 33);
    display.print("Humidity: ");
    display.print(humidity, 1);
    display.println(" %");
  }

  display.setCursor(0, 48);
  display.println("Status: Monitoring");

  display.setCursor(0, 57);
  display.println("BACK: Main Menu");

  display.display();
}

// ============================================
// DISTANCE SCANNER: measurement, warning levels, and OLED display
// ============================================

void updateDistanceScanner() {
  unsigned long now = millis();

  if (now - lastDistanceRead >= DISTANCE_INTERVAL) {
    lastDistanceRead = now;

    // HC-SR04 trigger pulse
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(3);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    unsigned long duration = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);

    if (duration == 0) {
      // Keep the previous smoothed reading briefly, then report no echo.
      if (lastValidDistanceRead == 0 || now - lastValidDistanceRead > 1000) {
        distanceCm = NAN;
        filteredDistanceCm = NAN;
      }
    } else {
      distanceCm = (duration * 0.0343f) / 2.0f;
      lastValidDistanceRead = now;

      // Light smoothing to reduce small jumps while still reacting to movement.
      if (isnan(filteredDistanceCm)) {
        filteredDistanceCm = distanceCm;
      } else {
        filteredDistanceCm = 0.65f * filteredDistanceCm + 0.35f * distanceCm;
      }
    }

    showDistanceScanner();
  }

  updateProximityBuzzer();
}

void updateProximityBuzzer() {
  // Alert only while the Distance Scanner is open, so it doesn't interfere
  // with the Pomodoro completion buzzer or other screens.
  if (dashboardHome || inMenu || selectedItem != 3 || isnan(filteredDistanceCm)) {
    proximityBuzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  unsigned long now = millis();
  unsigned long interval = 0;
  unsigned long onDuration = 0;

  if (filteredDistanceCm < 10.0f) {
    interval = 180;   // Very close: rapid warning
    onDuration = 90;
  } else if (filteredDistanceCm < 25.0f) {
    interval = 650;   // Near: slower warning
    onDuration = 100;
  } else {
    proximityBuzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  if (now - lastProximityBeep >= interval) {
    lastProximityBeep = now;
    buzzerPhaseStarted = now;
    proximityBuzzerOn = true;
    digitalWrite(BUZZER_PIN, HIGH);
  }

  if (proximityBuzzerOn && now - buzzerPhaseStarted >= onDuration) {
    proximityBuzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void showDistanceScanner() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("NEXUS // SCANNER");
  display.setCursor(101, 0);
  display.print("US");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 14);
  display.print("DISTANCE:");

  if (isnan(filteredDistanceCm)) {
    display.setTextSize(2);
    display.setCursor(18, 25);
    display.print("NO ECHO");
    display.setTextSize(1);
    display.setCursor(0, 47);
    display.print("Check target/wiring");
    display.setCursor(0, 57);
    display.print("BACK: Dashboard");
    display.display();
    return;
  }

  display.setTextSize(2);
  display.setCursor(20, 23);
  if (filteredDistanceCm >= 100.0f) {
    display.print(filteredDistanceCm, 0);
  } else {
    display.print(filteredDistanceCm, 1);
  }
  display.setTextSize(1);
  display.print("cm");

  // Distance bar: maps 0-200 cm to a 0-100 pixel filled bar.
  display.drawRect(13, 43, 102, 8, SSD1306_WHITE);
  int barWidth = (int)(constrain(filteredDistanceCm, 0.0f, 200.0f) / 200.0f * 98.0f);
  if (barWidth > 0) {
    display.fillRect(15, 45, barWidth, 4, SSD1306_WHITE);
  }

  display.setCursor(0, 54);
  if (filteredDistanceCm < 10.0f) {
    display.print("DANGER! BACK:Dash");
  } else if (filteredDistanceCm < 25.0f) {
    display.print("CLOSE  BACK:Dash");
  } else if (filteredDistanceCm < 60.0f) {
    display.print("NEAR   BACK:Dash");
  } else {
    display.print("CLEAR  BACK:Dash");
  }

  display.display();
}

// ============================================
// PIR MOTION DETECTOR
// ============================================

void showMotionDetector() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("MOTION DETECTOR");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  if (millis() - pirWarmupStarted < PIR_WARMUP_MS) {
    display.setCursor(0, 19);
    display.print("PIR warming up...");
    display.setCursor(0, 32);
    display.print("Allow ~30 seconds");
  } else {
    display.setCursor(0, 19);
    display.print("Status:");
    display.setTextSize(2);
    display.setCursor(0, 31);
    display.print(motionDetected ? "MOTION!" : "CLEAR");
    display.setTextSize(1);
    display.setCursor(0, 49);
    display.print(motionDetected ? "Movement detected" : "No movement detected");
  }

  display.setCursor(0, 58);
  display.print("BACK: Dashboard");
  display.display();
}

// ============================================
// WI-FI + NTP CLOCK

void connectAndSyncClock() {
  if (String(WIFI_SSID) == "YOUR_HOTSPOT_NAME" || strlen(WIFI_SSID) == 0) {
    Serial.println("Wi-Fi not configured; starting offline.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long attemptStart = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - attemptStart < 12000) {
    Serial.print(".");
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\\nWi-Fi unavailable; starting offline.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  wifiConnected = true;
  Serial.println("\\nWi-Fi connected!");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
             "pool.ntp.org", "time.google.com");

  struct tm timeInfo;
  unsigned long syncStart = millis();
  while (millis() - syncStart < 8000) {
    if (getLocalTime(&timeInfo, 200) && timeInfo.tm_year >= (2024 - 1900)) {
      clockReady = true;
      Serial.println("Clock synchronized!");
      break;
    }
    delay(100);
  }
  if (!clockReady) Serial.println("NTP sync not available yet.");
  lastWiFiAttempt = millis();
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    if (!clockReady) {
      struct tm timeInfo;
      if (getLocalTime(&timeInfo, 10) && timeInfo.tm_year >= (2024 - 1900)) {
        clockReady = true;
        Serial.println("Clock synchronized!");
      }
    }
    return;
  }

  wifiConnected = false;
  if (String(WIFI_SSID) == "YOUR_HOTSPOT_NAME" || strlen(WIFI_SSID) == 0) return;

  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    lastWiFiAttempt = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// CYBERPUNK HOME DASHBOARD

void showDashboard() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("NEXUS // COMMAND");
  display.setCursor(91, 0);
  display.print(wifiConnected ? "NET+" : "NET-");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  struct tm timeInfo;
  display.setTextSize(2);
  display.setCursor(10, 14);
  if (clockReady && getLocalTime(&timeInfo, 10)) {
    char timeString[12];
    strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeInfo);
    display.print(timeString);
  } else {
    display.print("--:--:--");
  }

  display.setTextSize(1);
  display.setCursor(0, 34);
  if (clockReady && getLocalTime(&timeInfo, 10)) {
    char dateString[20];
    strftime(dateString, sizeof(dateString), "%d-%m-%Y", &timeInfo);
    display.print(daysOfWeek[timeInfo.tm_wday]);
    display.print("  ");
    display.print(dateString);
  } else {
    display.print("CLOCK NOT SYNCED");
  }

  display.drawLine(0, 44, 127, 44, SSD1306_WHITE);
  display.setCursor(0, 48);
  display.print("T:");
  if (isnan(temperature)) display.print("--.-C");
  else { display.print(temperature, 1); display.print("C"); }

  display.setCursor(66, 48);
  display.print("H:");
  if (isnan(humidity)) display.print("--%");
  else { display.print(humidity, 0); display.print("%"); }

  display.setCursor(0, 58);
  display.print("SYS:");
  display.print(wifiConnected ? "ONLINE" : "OFFLINE");
  display.setCursor(79, 58);
  display.print("SEL:MENU");
  display.display();
}

// CLOCK FEATURE SCREEN

void showClock() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("SYSTEM CLOCK");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  struct tm timeInfo;
  if (clockReady && getLocalTime(&timeInfo, 10)) {
    char timeString[16];
    char dateString[20];
    strftime(timeString, sizeof(timeString), "%I:%M:%S %p", &timeInfo);
    strftime(dateString, sizeof(dateString), "%d-%m-%Y", &timeInfo);

    display.setTextSize(2);
    display.setCursor(5, 20);
    display.print(timeString);
    display.setTextSize(1);
    display.setCursor(0, 44);
    display.print(daysOfWeek[timeInfo.tm_wday]);
    display.print("  ");
    display.print(dateString);
    display.setCursor(0, 56);
    display.print(wifiConnected ? "NTP: WIFI" : "NTP: OFFLINE");
  } else {
    display.setCursor(0, 25);
    display.println("Time not synchronized");
    display.setCursor(0, 40);
    display.println("Connect hotspot to sync");
  }
  display.display();
}
