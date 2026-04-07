#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// === GSM Setup ===
#define GSM_TX 17
#define GSM_RX 16
HardwareSerial GSM(2);
// === Pins ===
#define BUTTON_PIN 12
#define ECG_PIN 34
#define LO_PLUS 32
#define LO_MINUS 33
#define BATTERY_PIN 35
// === State Variables ===
int bpm = 0;
String ecgCondition = "Normal";
int batteryPercent = 0;
bool electrodeContact = false;
// ECG buffer (~5s @125Hz ≈ 640 samples)
#define ECG_BUFFER_SIZE 640
int ecgBuffer[ECG_BUFFER_SIZE];
int ecgIndex = 0;
// BPM calc
unsigned long lastBeatTime = 0;
int beatInterval = 0;
// Button press timing
unsigned long lastPress = 0;
int pressCount = 0;
// Auto abnormal SMS detection
unsigned long abnormalStart = 0;
unsigned long lastAutoSent = 0;
bool autoActive = false;
// PQRST waveform template
int pqrstPattern[9] = {0, 3, 6, -2, 4, -1, 2, 0, 0};
int pqrstIndex = 0;
void setup() {
Serial.begin(115200);
GSM.begin(115200, SERIAL_8N1, GSM_RX, GSM_TX);
pinMode(BUTTON_PIN, INPUT_PULLUP);
pinMode(LO_PLUS, INPUT);
pinMode(LO_MINUS, INPUT);
if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
Serial.println("SSD1306 init failed");
while (true);
}
display.clearDisplay();
display.display();
for (int i = 0; i < ECG_BUFFER_SIZE; i++) ecgBuffer[i] = SCREEN_HEIGHT / 2;
Serial.println("=== ECG + OLED + GSM (Auto + Single Click) ===");
}
void loop() {
// --- GSM passthrough ---
while (GSM.available()) Serial.write(GSM.read());
while (Serial.available()) GSM.write(Serial.read());
// --- Button handling ---
if (digitalRead(BUTTON_PIN) == LOW) {
if (millis() - lastPress < 1500) pressCount++;
else pressCount = 1;
lastPress = millis();
delay(50); // small debounce
}
if (pressCount == 2) {
sendEmergencyAssistSMS();
pressCount = 0;
} else if (pressCount == 1 && millis() - lastPress > 1500) {
sendTacticalSMS();
pressCount = 0;
}
// --- Battery reading ---
batteryPercent = map(analogRead(BATTERY_PIN), 0, 4095, 0, 100);
// --- ECG reading ---
int ecgValue = analogRead(ECG_PIN);
updateECGWaveform(ecgValue);
drawOLED();
}
// --- Update ECG waveform with PQRST ---
void updateECGWaveform(int rawValue) {
electrodeContact = (digitalRead(LO_PLUS) == LOW && digitalRead(LO_MINUS) == LOW);
if (electrodeContact) {
float norm = ((float)rawValue - 2048) / 2048.0;
float mod = norm + pqrstPattern[pqrstIndex] / 10.0;
int y = map(mod, -1, 1, 55, 15);
y = constrain(y, 0, SCREEN_HEIGHT - 1);
ecgBuffer[ecgIndex] = y;
pqrstIndex = (pqrstIndex + 1) % 9;
detectHeartbeat(rawValue);
// Automatic abnormal SMS after 10 seconds
if (ecgCondition == "Abnormal" && bpm > 0) {
if (abnormalStart == 0) abnormalStart = millis();
if (!autoActive && (millis() - abnormalStart > 10000) && (millis() - lastAutoSent > 60000)) {
sendTacticalSMS();
autoActive = true;
lastAutoSent = millis();
}
} else {
abnormalStart = 0;
autoActive = false;
}
} else {
// Flatline if no contact
ecgBuffer[ecgIndex] = SCREEN_HEIGHT / 2;
bpm = 0;
ecgCondition = "No Contact";
abnormalStart = 0;
autoActive = false;
}
ecgIndex = (ecgIndex + 1) % ECG_BUFFER_SIZE;
}
// === Draw OLED ===
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
// === Heartbeat Detection ===
void detectHeartbeat(int value) {
static int threshold = 2000;
static bool beatDetected = false;
if (value > threshold && !beatDetected) {
beatDetected = true;
unsigned long now = millis();
beatInterval = now - lastBeatTime;
lastBeatTime = now;
if (beatInterval > 300 && beatInterval < 2000) {
bpm = 60000 / beatInterval;
ecgCondition = (bpm < 50 || bpm > 120) ? "Abnormal" : "Normal";
}
}
if (value < threshold) beatDetected = false;
}
// === Send Tactical SMS (BPM + Condition + 5s ECG + Location) ===
void sendTacticalSMS() {
Serial.println(">>> Sending Tactical/Emergency SMS...");
String msg = "Nightfall-EX ALERT!\n";
msg += "BPM: " + String(bpm) + "\n";
msg += "Condition: " + ecgCondition + "\n";
msg += "Location: Mysuru, Karnataka 570017, India\n";
msg += "Lat: 12.336338° Long: 76.619706°\n";
// Last 5s ECG (~640 samples compressed to 50 points)
msg += "Last 5s ECG: ";
for(int i=0; i<50; i++){
int idx = (ecgIndex - i*12 + ECG_BUFFER_SIZE) % ECG_BUFFER_SIZE;
msg += String(ecgBuffer[idx]) + ",";
}
GSM.println("AT+CMGF=1"); delay(300);
GSM.print("AT+CMGS=\"+919606470070\"\r"); delay(300);
GSM.print(msg); delay(300);
GSM.write(26);
}
// === Send Emergency Assist SMS (double press) ===
void sendEmergencyAssistSMS() {
Serial.println(">>> Sending double-press Emergency Assist SMS...");
String msg = "EMERGENCY ASSISTANCE REQUIRED \n";
msg += "Location: Mysuru, Karnataka 570017, India\n";
msg += "Lat: 12.336338° Long: 76.619706°\n";
msg += "06/09/2025 02:22 PM GMT+05:30";
GSM.println("AT+CMGF=1"); delay(300);
GSM.print("AT+CMGS=\"+919606470070\"\r"); delay(300);
GSM.print(msg); delay(300);
GSM.write(26);
}