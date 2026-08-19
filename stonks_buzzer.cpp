#include <WiFiS3.h>             
#include <WiFiSSLClient.h>      
#include <Arduino_Modulino.h>
#include "Arduino_LED_Matrix.h" 

// --- 1. Network Credentials ---
const char* ssid     = "XXX";     
const char* password = "XXX"; 

// --- 2. Finnhub API Settings ---
const char* finnhubApiKey  = "XXX";
String currentTicker       = "SNDK"; // Now a dynamic String 
float changeThreshold = 1.0;   // Highly sensitive for testing (±0.1% movement triggers alert)
const long checkInterval    = 30000; // Check every 30 seconds (30000ms)

unsigned long lastCheckTime = 0;
float lastAlertedPercent = -999.0;
// --- NEW VARIABLES FOR 24/7 TRACKING ---
float currentPreviousClose = -999.0; // The previous close price we are currently tracking
float livePreviousClose = 0.0;       // The live previous close price fetched from Finnhub
WiFiSSLClient client;

ModulinoPixels leds;
ModulinoBuzzer buzzer;
ArduinoLEDMatrix matrix; 
ModulinoButtons buttons;
ModulinoKnob knob;

// --- Custom Up/Down Animations ---
const uint32_t arrowUP[][4] = {
	{ 0x600f01f,   0x83fc7fef, 0xff1f81f8, 180 },
	{ 0xf01f83f,   0xc7fefff1, 0xf81f8000, 180 },
	{ 0x1f83fc7f,  0xefff1f81, 0xf8000000, 180 },
	{ 0x3fc7feff,  0xf1f81f80, 0x0,        180 },
	{ 0x7fefff1f,  0x81f80000, 0x0,        150 },
	{ 0xfff1f81f,  0x0,        0x0,        150 },
	{ 0x1f81f800,  0x0,        0x0,        150 },
	{ 0x1f800000,  0x0,        0x0,        150 },
	{ 0x0,         0x0,        0x0,        300 }
};

const uint32_t arrowDOWN[][4] = {
	{ 0x1f81f8ff,  0xf7fe3fc1, 0xf80f0060, 180 },
	{ 0x1f81f,     0x8fff7fe3, 0xfc1f80f0, 180 },
	{ 0x1f,        0x81f8fff7, 0xfe3fc1f8, 180 },
	{ 0x0,         0x1f81f8f,  0xff7fe3fc, 180 },
	{ 0x0,         0x1f81,     0xf8fff7fe, 180 },
	{ 0x0,         0x1,        0xf81f8fff, 150 },
	{ 0x0,         0x0,        0x1f81f8,   150 },
	{ 0x0,         0x0,        0x1f8,      150 },
	{ 0x0,         0x0,        0x0,        300 }
};

void setup() {
  Serial.begin(115200);
  Modulino.begin();
  leds.begin();
  buzzer.begin();
  matrix.begin(); 
  buttons.begin(); 
  
  knob.begin(); // Initialize the Knob
  knob.set(10); // Set the internal starting integer to 10 (representing 1.0%)

  buttons.setLeds(true, false, false);

  setPixelsColor(ModulinoColor(0, 0, 100)); // Blue: Connecting...
  connectToWiFi();
  setPixelsColor(ModulinoColor(0, 100, 100)); // Teal: Ready
  delay(1000);
  clearPixels();
}

void playCatchUpAlert(float percentChange) {
  int count = abs((int)percentChange);
  if (count <= 0) return;

  if (percentChange > 0) {
    // Upward Catch-up
    matrix.loadSequence(arrowUP);
    matrix.play(true);
    setPixelsColor(ModulinoColor(0, 150, 0)); // Green

    Serial.print("Playing ");
    Serial.print(count);
    Serial.println(" UP chimes...");

    for (int i = 0; i < count; i++) {
      buzzer.tone(880, 80);
      delay(100);
      buzzer.tone(1320, 120);
      delay(180); // Pace between repetitions
    }
  } else {
    // Downward Catch-up
    matrix.loadSequence(arrowDOWN);
    matrix.play(true);
    setPixelsColor(ModulinoColor(150, 0, 0)); // Red

    Serial.print("Playing ");
    Serial.print(count);
    Serial.println(" DOWN chimes...");

    for (int i = 0; i < count; i++) {
      buzzer.tone(330, 120);
      delay(140);
      buzzer.tone(220, 180);
      delay(180); // Pace between repetitions
    }
  }

  matrix.clear();
  clearPixels();
}

void switchTicker(String newTicker) {
  if (currentTicker == newTicker) return; // Ignore if already on this ticker
  
  Serial.print("\n--- Switching Active Ticker to ");
  Serial.print(newTicker);
  Serial.println(" ---");

  currentTicker = newTicker;
  lastAlertedPercent = -999.0; // Reset baseline to force the catch-up chimes
  currentPreviousClose = -999.0; // Reset previous close baseline for the new ticker
  lastCheckTime = 0;           // Force an immediate API check on the next loop cycle
  
  matrix.clear();
  clearPixels();
}

void loop() {
  
  // --- KNOB / SENSITIVITY LOGIC ---
  int knobRaw = knob.get(); // Read the raw integer from the knob
  
  // Clamp the values so it doesn't go below 1 (0.1%) or above 20 (2.0%)
  if (knobRaw < 1) {
    knob.set(1);
    knobRaw = 1;
  } else if (knobRaw > 20) {
    knob.set(20);
    knobRaw = 20;
  }

  // Convert the integer into our float threshold (e.g., 5 becomes 0.5)
  float newThreshold = (float)knobRaw / 10.0;

  // If you turned the knob and changed the threshold, print it out!
  if (newThreshold != changeThreshold) {
    changeThreshold = newThreshold;
    Serial.print("Sensitivity adjusted to: ");
    Serial.print(changeThreshold, 1);
    Serial.println("%");
    
    // Optional: Give a tiny tactile beep when you change the setting
    buzzer.tone(1500, 10); 
  }
  // --- BUTTON CHECKING LOGIC ---
  if (buttons.update()) { // Request new data from the button module
    if (buttons.isPressed(0)) { // Check Button A
      switchTicker("SNDK");
      buttons.setLeds(true, false, false); // Light up Button A
    } 
    else if (buttons.isPressed(1)) { // Check Button B
      switchTicker("MU");
      buttons.setLeds(false, true, false); // Light up Button B
    } 
    else if (buttons.isPressed(2)) { // Check Button C
      switchTicker("WDC");
      buttons.setLeds(false, false, true); // Light up Button C
    }
  }
  if (millis() - lastCheckTime >= checkInterval || lastCheckTime == 0) {
    lastCheckTime = millis();
    
float percentChange = fetchStockFromFinnhub();

    if (percentChange != -999.0) {

      // --- NEW DAY DETECTED (24/7 ROLLOVER) ---
      if (currentPreviousClose != -999.0 && livePreviousClose > 0.0 && currentPreviousClose != livePreviousClose) {
        Serial.println("\n--- NEW TRADING DAY DETECTED! ---");
        Serial.print("Previous Close shifted from $");
        Serial.print(currentPreviousClose, 2);
        Serial.print(" to $");
        Serial.println(livePreviousClose, 2);
        
        lastAlertedPercent = -999.0; // Force re-initialization for the new day
      }

      // --- 1. INITIALIZATION & CATCH-UP ON BOOT / MARKET OPEN ---
      if (lastAlertedPercent == -999.0) {
        lastAlertedPercent = percentChange;
        currentPreviousClose = livePreviousClose; // Store active previous close
        
        Serial.print("Connected! Initial market position: ");
        Serial.print(percentChange, 2);
        Serial.println("%");

        int initialCount = abs((int)percentChange);

        if (initialCount > 0) {
          Serial.print("Stock is ");
          Serial.print(percentChange > 0 ? "UP " : "DOWN ");
          Serial.print(initialCount);
          Serial.println("% from close. Starting catch-up alerts!");
          
          playCatchUpAlert(percentChange);
        } else {
          Serial.println("Stock is near 0%. Monitoring quietly...");
          pulseMonitorActiveLed();
        }
        return;
      }

      // --- 2. REGULAR INTRADAY TRACKING ---
      float movement = percentChange - lastAlertedPercent;

      Serial.print("Live Daily Change: ");
      Serial.print(percentChange, 4); 
      Serial.println("%");

      Serial.print("Movement since last alert: ");
      Serial.print(movement, 4); 
      Serial.println("%");

      if (movement >= changeThreshold) {
        Serial.print("ALERT: Stock climbed by ");
        Serial.print(movement, 4);
        Serial.println("%!");
        
        triggerUpAlert();
        lastAlertedPercent = percentChange; 
      } 
      else if (movement <= -changeThreshold) {
        Serial.print("ALERT: Stock dropped by ");
        Serial.print(abs(movement), 4);
        Serial.println("%!");
        
        triggerDownAlert();
        lastAlertedPercent = percentChange; 
      } 
      else {
        Serial.println("Holding within threshold. Monitoring quietly...\n");
        pulseMonitorActiveLed();
      }

    } else {
      Serial.println("[ERR] Could not parse quote from Finnhub.\n");
    }
  }
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
}

// Connects directly to Finnhub over SSL and parses the real-time "dp" (daily percent) field
float fetchStockFromFinnhub() {
  if (WiFi.status() != WL_CONNECTED) connectToWiFi();

  if (client.connect("finnhub.io", 443)) {
    client.println("GET /api/v1/quote?symbol=" + currentTicker + "&token=" + String(finnhubApiKey) + " HTTP/1.1");
    client.println("Host: finnhub.io");
    client.println("User-Agent: Arduino/1.0");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break; 
    }

    String response = client.readString();
    client.stop();

    // 1. Parse the "dp" (Daily Percent Change)
    float dpValue = -999.0;
    int dpIndex = response.indexOf("\"dp\":");
    if (dpIndex != -1) {
      int commaIndex = response.indexOf(',', dpIndex);
      if (commaIndex == -1) commaIndex = response.indexOf('}', dpIndex);
      dpValue = response.substring(dpIndex + 5, commaIndex).toFloat();
    }

    // 2. Parse the "pc" (Previous Close Price) to detect new trading days
    int pcIndex = response.indexOf("\"pc\":");
    if (pcIndex != -1) {
      int commaIndex = response.indexOf(',', pcIndex);
      if (commaIndex == -1) commaIndex = response.indexOf('}', pcIndex);
      livePreviousClose = response.substring(pcIndex + 5, commaIndex).toFloat();
    }
    
    return dpValue;
  }
  return -999.0;
}

void triggerUpAlert() {
  matrix.loadSequence(arrowUP);
  matrix.play(true);
  setPixelsColor(ModulinoColor(0, 150, 0));
  buzzer.tone(880, 150); delay(150); buzzer.tone(1320, 300);
  delay(8000);
  matrix.clear(); clearPixels();
}

void triggerDownAlert() {
  matrix.loadSequence(arrowDOWN);
  matrix.play(true);
  setPixelsColor(ModulinoColor(150, 0, 0));
  buzzer.tone(330, 200); delay(200); buzzer.tone(220, 400);
  delay(8000);
  matrix.clear(); clearPixels();
}

void pulseMonitorActiveLed() {
  leds.set(0, ModulinoColor(0, 30, 30)); leds.show();
  delay(300); leds.clear(0); leds.show();
}

void setPixelsColor(ModulinoColor color) {
  for (int i = 0; i < 8; i++) leds.set(i, color);
  leds.show();
}

void clearPixels() {
  for (int i = 0; i < 8; i++) leds.clear(i);
  leds.show();
}
