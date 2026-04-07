#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* =========================================================
   OLED DISPLAY CONFIGURATION
   ========================================================= */
// Define OLED screen dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Initialize OLED display using I2C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


/* =========================================================
   GSM MODULE CONFIGURATION
   ========================================================= */
// Define GSM TX and RX pins
#define GSM_TX 17
#define GSM_RX 16

// Initialize hardware serial for GSM communication
HardwareSerial GSM(2);


/* =========================================================
   PIN DEFINITIONS
   ========================================================= */
#define BUTTON_PIN 12     // Push button for sending SMS
#define ECG_PIN 34        // ECG analog input
#define LO_PLUS 32        // Lead-off detection +
#define LO_MINUS 33       // Lead-off detection -
#define BATTERY_PIN 35    // Battery voltage input


/* =========================================================
   GLOBAL VARIABLES
   ========================================================= */
int bpm = 0;                        // Heart rate (beats per minute)
String ecgCondition = "Normal";     // ECG condition status
int batteryPercent = 0;             // Battery percentage
bool electrodeContact = false;      // Electrode connection status


/* =========================================================
   ECG BUFFER (Stores waveform data)
   ========================================================= */
#define ECG_BUFFER_SIZE 640         // ~5 seconds of data @125Hz
int ecgBuffer[ECG_BUFFER_SIZE];
int ecgIndex = 0;


/* =========================================================
   HEARTBEAT DETECTION VARIABLES
   ========================================================= */
unsigned long lastBeatTime = 0;     // Last detected heartbeat time
int beatInterval = 0;               // Time between beats


/* =========================================================
   BUTTON PRESS HANDLING
   ========================================================= */
unsigned long lastPress = 0;
int pressCount = 0;


/* =========================================================
   AUTO SMS (Abnormal ECG detection)
   ========================================================= */
unsigned long abnormalStart = 0;
unsigned long lastAutoSent = 0;
bool autoActive = false;


/* =========================================================
   SIMULATED PQRST WAVEFORM PATTERN
   ========================================================= */
int pqrstPattern[9] = {0, 3, 6, -2, 4, -1, 2, 0, 0};
int pqrstIndex = 0;


/* =========================================================
   SETUP FUNCTION
   ========================================================= */
void setup() {
  Serial.begin(115200);

  // Initialize GSM communication
  GSM.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);

  // Configure pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LO_PLUS, INPUT);
  pinMode(LO_MINUS, INPUT);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed");
    while (true); // Stop execution if display fails
  }

  display.clearDisplay();
  display.display();

  // Initialize ECG buffer with baseline
  for (int i = 0; i < ECG_BUFFER_SIZE; i++)
    ecgBuffer[i] = SCREEN_HEIGHT / 2;

  Serial.println("=== ECG + OLED + GSM System Started ===");
}


/* =========================================================
   MAIN LOOP
   ========================================================= */
void loop() {

  /* ----- GSM Serial Passthrough (Debugging) ----- */
  while (GSM.available()) Serial.write(GSM.read());
  while (Serial.available()) GSM.write(Serial.read());

  /* ----- BUTTON HANDLING ----- */
  if (digitalRead(BUTTON_PIN) == LOW) {
    // Detect single/double press
    if (millis() - lastPress < 1500)
      pressCount++;
    else
      pressCount = 1;

    lastPress = millis();
    delay(50); // debounce
  }

  // Double press → Emergency Assist SMS
  if (pressCount == 2) {
    sendEmergencyAssistSMS();
    pressCount = 0;
  }
  // Single press → Tactical SMS
  else if (pressCount == 1 && millis() - lastPress > 1500) {
    sendTacticalSMS();
    pressCount = 0;
  }

  /* ----- BATTERY MONITORING ----- */
  batteryPercent = map(analogRead(BATTERY_PIN), 0, 4095, 0, 100);

  /* ----- ECG DATA READING ----- */
  int ecgValue = analogRead(ECG_PIN);

  // Update waveform and detect heartbeat
  updateECGWaveform(ecgValue);

  // Update display
  drawOLED();
}


/* =========================================================
   UPDATE ECG WAVEFORM
   ========================================================= */
void updateECGWaveform(int rawValue) {

  // Check electrode contact
  electrodeContact = (digitalRead(LO_PLUS) == LOW && digitalRead(LO_MINUS) == LOW);

  if (electrodeContact) {

    // Normalize ECG signal
    float norm = ((float)rawValue - 2048) / 2048.0;

    // Add simulated PQRST waveform
    float mod = norm + pqrstPattern[pqrstIndex] / 10.0;

    // Map signal to screen height
    int y = map(mod, -1, 1, 55, 15);
    y = constrain(y, 0, SCREEN_HEIGHT - 1);

    ecgBuffer[ecgIndex] = y;

    // Move to next PQRST point
    pqrstIndex = (pqrstIndex + 1) % 9;

    // Detect heartbeat
    detectHeartbeat(rawValue);

    /* ----- AUTO ALERT SYSTEM ----- */
    if (ecgCondition == "Abnormal" && bpm > 0) {

      if (abnormalStart == 0)
        abnormalStart = millis();

      // Send alert after 10 seconds abnormal condition
      if (!autoActive &&
          (millis() - abnormalStart > 10000) &&
          (millis() - lastAutoSent > 60000)) {

        sendTacticalSMS();
        autoActive = true;
        lastAutoSent = millis();
      }
    } else {
      abnormalStart = 0;
      autoActive = false;
    }

  } else {
    // No electrode contact → flatline
    ecgBuffer[ecgIndex] = SCREEN_HEIGHT / 2;
    bpm = 0;
    ecgCondition = "No Contact";
    abnormalStart = 0;
    autoActive = false;
  }

  // Move buffer index forward
  ecgIndex = (ecgIndex + 1) % ECG_BUFFER_SIZE;
}


/* =========================================================
   OLED DISPLAY FUNCTION
   ========================================================= */
void drawOLED() {
  display.clearDisplay();

  // Top: GPS + Battery
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("GPS: Mysuru, IN");

  display.setCursor(90, 0);
  display.printf("Bat:%d%%", batteryPercent);

  // Middle: ECG waveform
  for (int i = 0; i < SCREEN_WIDTH - 1; i++) {
    int idx1 = (ecgIndex + i) % ECG_BUFFER_SIZE;
    int idx2 = (ecgIndex + i + 1) % ECG_BUFFER_SIZE;

    display.drawLine(i, ecgBuffer[idx1], i + 1, ecgBuffer[idx2], SSD1306_WHITE);
    display.drawLine(i, ecgBuffer[idx1] + 1, i + 1, ecgBuffer[idx2] + 1, SSD1306_WHITE);
  }

  // Bottom: BPM + Condition
  display.setCursor(0, 56);
  display.printf("BPM:%d | %s", bpm, ecgCondition.c_str());

  display.display();
}


/* =========================================================
   HEARTBEAT DETECTION LOGIC
   ========================================================= */
void detectHeartbeat(int value) {

  static int threshold = 2000;
  static bool beatDetected = false;

  if (value > threshold && !beatDetected) {
    beatDetected = true;

    unsigned long now = millis();
    beatInterval = now - lastBeatTime;
    lastBeatTime = now;

    // Calculate BPM
    if (beatInterval > 300 && beatInterval < 2000) {
      bpm = 60000 / beatInterval;

      // Determine condition
      ecgCondition = (bpm < 50 || bpm > 120) ? "Abnormal" : "Normal";
    }
  }

  if (value < threshold)
    beatDetected = false;
}


/* =========================================================
   SEND TACTICAL SMS
   ========================================================= */
void sendTacticalSMS() {
  Serial.println(">>> Sending Tactical/Emergency SMS...");

  String msg = "Nightfall-EX ALERT!\n";
  msg += "BPM: " + String(bpm) + "\n";
  msg += "Condition: " + ecgCondition + "\n";
  msg += "Location: Mysuru, Karnataka 570017, India\n";
  msg += "Lat: 12.336338° Long: 76.619706°\n";

  // Include compressed ECG data (~5 seconds)
  msg += "Last 5s ECG: ";
  for (int i = 0; i < 50; i++) {
    int idx = (ecgIndex - i * 12 + ECG_BUFFER_SIZE) % ECG_BUFFER_SIZE;
    msg += String(ecgBuffer[idx]) + ",";
  }

  // GSM commands to send SMS
  GSM.println("AT+CMGF=1"); delay(300);
  GSM.print("AT+CMGS=\"+919606470070\"\r"); delay(300);
  GSM.print(msg); delay(300);
  GSM.write(26); // CTRL+Z to send
}


/* =========================================================
   SEND EMERGENCY ASSIST SMS (DOUBLE PRESS)
   ========================================================= */
void sendEmergencyAssistSMS() {
  Serial.println(">>> Sending Emergency Assist SMS...");

  String msg = "EMERGENCY ASSISTANCE REQUIRED\n";
  msg += "Location: Mysuru, Karnataka 570017, India\n";
  msg += "Lat: 12.336338° Long: 76.619706°\n";
  msg += "Timestamp: 06/09/2025 02:22 PM GMT+05:30";

  GSM.println("AT+CMGF=1"); delay(300);
  GSM.print("AT+CMGS=\"+919606470070\"\r"); delay(300);
  GSM.print(msg); delay(300);
  GSM.write(26);
}
