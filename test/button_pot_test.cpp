/*
  Button and Potentiometer Test
  
  This test file helps verify that all 4 buttons and the potentiometer
  are working correctly by printing their states to the serial monitor.
  
  Hardware:
  - 4 buttons connected to pins D5, D6, D7, D8
  - Potentiometer connected to A0
  
  Upload this test and open the serial monitor at 115200 baud to see:
  - Button presses (with debouncing)
  - Potentiometer value (0-1023)
  - Mapped potentiometer value (0-100 for volume)
*/

#include <Arduino.h>

// Button pins (same as main project)
const int buttonPins[] = {D5, D6, D7, D8};
const String buttonNames[] = {"PREV/UP", "SELECT/ENTER", "NEXT/DOWN", "BACK/MENU"};

// Button debouncing variables
bool lastButtonStates[] = {LOW, LOW, LOW, LOW};
bool currentButtonStates[] = {LOW, LOW, LOW, LOW};
unsigned long lastDebounceTime[] = {0, 0, 0, 0};
const unsigned long debounceDelay = 50;

// Potentiometer variables
int lastPotValue = -1;
const int potThreshold = 5; // Only report changes greater than this

void setup() {
  Serial.begin(115200);
  
  // Wait for serial to initialize
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("\n=== BUTTON & POTENTIOMETER TEST ===");
  Serial.println("Hardware Test for Spotify Controller");
  Serial.println("====================================");
  Serial.println();
  Serial.println("Button Layout:");
  Serial.println("  Button 0 (D5): PREV/UP      - Skip backward / Navigate up");
  Serial.println("  Button 1 (D6): SELECT/ENTER - Play/Pause / Select option");  
  Serial.println("  Button 2 (D7): NEXT/DOWN    - Skip forward / Navigate down");
  Serial.println("  Button 3 (D8): BACK/MENU    - Like/Unlike / Back/Menu");
  Serial.println();
  Serial.println("Potentiometer (A0): Volume control (0-100%)");
  Serial.println();
  Serial.println("--- Starting Test ---");
  Serial.println();
  
  // Initialize button pins as inputs with internal pull-up
  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    // Read initial state (inverted because of pull-up)
    lastButtonStates[i] = !digitalRead(buttonPins[i]);
  }
  
  // Read initial potentiometer value
  lastPotValue = analogRead(A0);
  int mappedValue = map(lastPotValue, 0, 1023, 0, 100);
  Serial.print("Initial potentiometer: ");
  Serial.print(lastPotValue);
  Serial.print(" (");
  Serial.print(mappedValue);
  Serial.println("%)");
  Serial.println();
}

void loop() {
  // Test buttons with debouncing
  for (int i = 0; i < 4; i++) {
    // Read the button state (inverted because of pull-up resistor)
    bool reading = !digitalRead(buttonPins[i]);
    
    // Check if button state changed
    if (reading != lastButtonStates[i]) {
      lastDebounceTime[i] = millis();
    }
    
    // If enough time has passed since the last state change
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      // If the button state has changed
      if (reading != currentButtonStates[i]) {
        currentButtonStates[i] = reading;
        
        // Only print when button is pressed (not released)
        if (currentButtonStates[i] == HIGH) {
          Serial.print("🔘 BUTTON ");
          Serial.print(i);
          Serial.print(" PRESSED: ");
          Serial.print(buttonNames[i]);
          Serial.print(" (Pin ");
          Serial.print(buttonPins[i]);
          Serial.println(")");
        }
      }
    }
    
    lastButtonStates[i] = reading;
  }
  
  // Test potentiometer (only report significant changes)
  int currentPotValue = analogRead(A0);
  if (abs(currentPotValue - lastPotValue) > potThreshold) {
    int mappedValue = map(currentPotValue, 0, 1023, 0, 100);
    Serial.print("🎚️ POTENTIOMETER: ");
    Serial.print(currentPotValue);
    Serial.print("/1023 (");
    Serial.print(mappedValue);
    Serial.println("%)");
    lastPotValue = currentPotValue;
  }
  
  // Show periodic status every 10 seconds
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime > 10000) {
    lastStatusTime = millis();
    Serial.println("--- System Status ---");
    Serial.print("⏱️ Uptime: ");
    Serial.print(millis() / 1000);
    Serial.println(" seconds");
    
    Serial.print("🔘 Button states: ");
    for (int i = 0; i < 4; i++) {
      Serial.print(currentButtonStates[i] ? "1" : "0");
      if (i < 3) Serial.print(" ");
    }
    Serial.println();
    
    int potValue = analogRead(A0);
    int mappedValue = map(potValue, 0, 1023, 0, 100);
    Serial.print("🎚️ Current pot: ");
    Serial.print(potValue);
    Serial.print(" (");
    Serial.print(mappedValue);
    Serial.println("%)");
    Serial.println();
  }
  
  // Small delay to prevent overwhelming the serial output
  delay(10);
}
