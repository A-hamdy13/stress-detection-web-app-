#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

// WiFi credentials
const char* ssid = "iPhone";
const char* password = "123456bb";

// Server details - verify the IP address is correct
const char* serverUrl = "http://172.20.10.10:5000/update";

// Pins
const int gsrPin = 33;
const int lm35Pin = 32;

// Calibration settings
#define CALIBRATION_DURATION 150000
unsigned long calibrationStartTime;
bool calibrated = false;

// Raw value tracking
float minRawGsr = 10000.0, maxRawGsr = 0.0;
float minRawTemp = 100.0, maxRawTemp = 0.0;
float minRawHR = 300.0, maxRawHR = 0.0;

// Default ranges in case calibration fails
const float DEFAULT_MIN_GSR = 0.01;
const float DEFAULT_MAX_GSR = 10.0;
const float DEFAULT_MIN_TEMP = 20.0;
const float DEFAULT_MAX_TEMP = 40.0;
const float DEFAULT_MIN_HR = 60.0;
const float DEFAULT_MAX_HR = 120.0;

// HR detection variables
float beatsPerMinute;
int beatAvg = 0;
unsigned long lastBeat = 0;

// Last send time
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 5000; // Send every 5 seconds
int failedRequests = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Stress monitoring system starting up...");

  // Initialize MAX30102 - Modified to use STANDARD speed instead of FAST
  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 sensor not found! Check wiring.");
    // Continue anyway, we'll use default values
  } else {
    Serial.println("MAX30102 sensor initialized successfully");
    particleSensor.setup();
    // Modified pulse amplitude settings to match working code
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeIR(0x0A);
  }

  // Connect to WiFi
  Serial.print("Connecting to WiFi network: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print("...");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Server URL: ");
    Serial.println(serverUrl);
  } else {
    Serial.println("\nWiFi connection failed. Will retry in operation loop.");
  }

  // Start calibration
  calibrationStartTime = millis();
  Serial.println("Starting sensor calibration phase...");
}

float mapToRange(float rawValue, float rawMin, float rawMax, float targetMin, float targetMax) {
  // Ensure we have valid min and max values
  if (rawMin >= rawMax) {
    rawMin = min(rawValue, DEFAULT_MIN_GSR);
    rawMax = max(rawValue, DEFAULT_MAX_GSR);
  }
  
  return constrain(targetMin + ((rawValue - rawMin) * (targetMax - targetMin)) / (rawMax - rawMin), targetMin, targetMax);
}

void loop() {
  // Check WiFi connection and reconnect if needed
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);  // Wait 5 seconds between reconnection attempts
  }

  // 1. Heart Rate Measurement
  long irValue = particleSensor.getIR();
  if (irValue < 5000) { // Lower threshold like in the simple code
    // If no finger is detected or sensor error
    beatAvg = 0;
    beatsPerMinute = 0;
    lastBeat = millis();
    if (millis() % 2000 < 10) {
      Serial.println("No finger detected on HR sensor or sensor error");
    }
  } else {
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      
      beatsPerMinute = 60.0 / (delta / 1000.0);
      
      // Using the simpler averaging like in the working code
      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        beatAvg = (beatAvg * 3 + (int)beatsPerMinute) / 4;
      }
    }
  }

  // 2. Temperature Measurement
  int rawLM35 = analogRead(lm35Pin);
  float rawTemp = (rawLM35 * 3.3 / 4095.0) * 100.0;
  
  // Validate temperature reading
  if (rawTemp < 0 || rawTemp > 100) {
    rawTemp = 25.0; // Default to room temperature if reading is invalid
    Serial.println("Invalid temperature reading detected, using default");
  }

  // 3. GSR Measurement
  int rawGsr = analogRead(gsrPin);
  float voltageGsr = rawGsr * (3.3 / 4095.0);
  
  // Avoid division by zero
  float denominator = (10000.0 * (3.3 - voltageGsr)) / voltageGsr;
  float rawConductance;
  
  if (denominator <= 0.01) {
    rawConductance = 0.01; // Prevent division by zero or negative values
  } else {
    rawConductance = 1.0 / denominator * 1000000.0;
  }
  
  // Handle infinity or very large values
  if (isnan(rawConductance) || isinf(rawConductance) || rawConductance > 10000000) {
    rawConductance = 0.01;
    Serial.println("Invalid GSR reading - using default value");
  }

  // Calibration phase
  if (!calibrated && millis() - calibrationStartTime < CALIBRATION_DURATION) {
    if (rawConductance > 0 && rawConductance < minRawGsr) minRawGsr = rawConductance;
    if (rawConductance > maxRawGsr) maxRawGsr = rawConductance;
    
    if (rawTemp > 0 && rawTemp < minRawTemp) minRawTemp = rawTemp;
    if (rawTemp > maxRawTemp) maxRawTemp = rawTemp;
    
    if (beatAvg > 40) {
      if (beatAvg < minRawHR) minRawHR = beatAvg;
      if (beatAvg > maxRawHR) maxRawHR = beatAvg;
    }

    if (millis() % 2000 < 20) {
      Serial.println("--- Calibration in progress ---");
      Serial.print("GSR: "); Serial.print(minRawGsr); Serial.print(" - "); Serial.print(maxRawGsr);
      Serial.print(" | Temp: "); Serial.print(minRawTemp); Serial.print(" - "); Serial.print(maxRawTemp);
      Serial.print(" | HR: "); Serial.print(minRawHR); Serial.print(" - "); Serial.println(maxRawHR);
    }
  } 
  else if (!calibrated) {
    calibrated = true;
    
    // Set default values if calibration wasn't successful
    if (minRawGsr >= maxRawGsr || minRawGsr <= 0) {
      minRawGsr = DEFAULT_MIN_GSR;
      maxRawGsr = DEFAULT_MAX_GSR;
    }
    
    if (minRawTemp >= maxRawTemp || minRawTemp <= 0) {
      minRawTemp = DEFAULT_MIN_TEMP;
      maxRawTemp = DEFAULT_MAX_TEMP;
    }
    
    if (minRawHR >= maxRawHR || minRawHR <= 0) {
      minRawHR = DEFAULT_MIN_HR;
      maxRawHR = DEFAULT_MAX_HR;
    }
    
    Serial.println("\n=== Calibration complete! ===");
    Serial.print("GSR range: "); Serial.print(minRawGsr); Serial.print(" - "); Serial.println(maxRawGsr);
    Serial.print("Temperature range: "); Serial.print(minRawTemp); Serial.print(" - "); Serial.println(maxRawTemp);
    Serial.print("Heart rate range: "); Serial.print(minRawHR); Serial.print(" - "); Serial.println(maxRawHR);
  }

  // Calculate mapped values for prediction - use model training ranges
  float mappedEDA = calibrated ? mapToRange(rawConductance, minRawGsr, maxRawGsr, 0.31, 1.14) : 0.31;
  float mappedTemp = calibrated ? mapToRange(rawTemp, minRawTemp, maxRawTemp, 31.21, 35.41) : 31.21;
  float mappedHR = calibrated ? mapToRange(beatAvg > 0 ? beatAvg : 70, minRawHR, maxRawHR, 145.96, 434.88) : 145.96;

  // Print values once per second
  if (millis() % 1000 < 20) {
    Serial.println("\n--- Current Sensor Readings ---");
    Serial.print("Raw - GSR: "); Serial.print(rawConductance, 2);
    Serial.print(" | Temp: "); Serial.print(rawTemp-30, 2);
    // Serial.print(" | HR: "); Serial.println(beatAvg);
    Serial.print(" | HR: ");if (beatAvg == 0) {
      Serial.println(0);
    } else {
      Serial.println(beatAvg + 60);
    }
    Serial.print("Mapped - EDA: "); Serial.print(mappedEDA, 6);
    Serial.print(" | Temp: "); Serial.print(mappedTemp, 2);
    Serial.print(" | HR: "); Serial.println(mappedHR, 2);
  }

  // Send data to server at regular intervals if calibrated
  if (calibrated && millis() - lastSendTime >= SEND_INTERVAL) {
    // Use default HR value if no finger detected
    int hrToSend = beatAvg > 0 ? beatAvg : 70;
    
    // Create JSON payload with validated data
    String jsonPayload = "{\"eda\":" + String(mappedEDA, 6) + 
                       ",\"temp\":" + String(mappedTemp, 2) + 
                       ",\"hr\":" + String(mappedHR, 2) +
                       ",\"eda_raw\":" + String(rawConductance, 2) +
                       ",\"temp_raw\":" + String(rawTemp, 2) +
                       ",\"hr_raw\":" + String(hrToSend) + "}";

    // Send to server
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");
      
      Serial.println("Sending data to server...");
      Serial.println(jsonPayload);
      
      int httpCode = http.POST(jsonPayload);
      if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Server response: " + response);
        lastSendTime = millis(); // Update last successful send time
        failedRequests = 0;      // Reset failed requests counter
      } else {
        Serial.println("HTTP Error: " + String(httpCode));
        failedRequests++;
        
        // If we've had too many failures, check server URL
        if (failedRequests > 5) {
          Serial.println("ERROR: Multiple failed requests. Please verify:");
          Serial.println("1. The Flask server is running");
          Serial.println("2. The server IP in ESP32 code is correct");
          Serial.print("Current server URL: ");
          Serial.println(serverUrl);
          
          // Try increasing the delay between requests after failures
          delay(5000);
        }
      }
      http.end();
    } else {
      Serial.println("WiFi not connected. Cannot send data.");
    }
    
    // Always update the last send time to avoid flooding
    lastSendTime = millis();
  }
  
  delay(50); // 20Hz update rate
}
