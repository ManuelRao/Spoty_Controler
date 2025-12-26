# Hardware Test Files

This folder contains test files to verify the hardware components of the Spotify Controller.

## Button and Potentiometer Test

### File: `button_pot_test.cpp`

This test helps verify that all 4 buttons and the potentiometer are working correctly.

### How to run the test:

1. **Upload the test:**
   ```bash
   pio run -e button_test --target upload
   ```

2. **Open the serial monitor:**
   ```bash
   pio device monitor -e button_test
   ```

   Or use VS Code's PlatformIO extension and click "Monitor" after selecting the `button_test` environment.

### What the test does:

- **Button Testing**: Monitors all 4 buttons with proper debouncing
  - Button 0 (D5): PREV/UP - Skip backward / Navigate up  
  - Button 1 (D6): SELECT/ENTER - Play/Pause / Select option
  - Button 2 (D7): NEXT/DOWN - Skip forward / Navigate down
  - Button 3 (D8): BACK/MENU - Like/Unlike / Back/Menu

- **Potentiometer Testing**: Monitors the analog input (A0) for volume control
  - Shows raw value (0-1023) and mapped percentage (0-100%)
  - Only reports significant changes to avoid spam

- **Status Updates**: Shows system status every 10 seconds

### Expected Output:

```
=== BUTTON & POTENTIOMETER TEST ===
Hardware Test for Spotify Controller
====================================

Button Layout:
  Button 0 (D5): PREV/UP      - Skip backward / Navigate up
  Button 1 (D6): SELECT/ENTER - Play/Pause / Select option  
  Button 2 (D7): NEXT/DOWN    - Skip forward / Navigate down
  Button 3 (D8): BACK/MENU    - Like/Unlike / Back/Menu

Potentiometer (A0): Volume control (0-100%)

--- Starting Test ---

Initial potentiometer: 512 (50%)

🔘 BUTTON 1 PRESSED: SELECT/ENTER (Pin D6)
🎚️ POTENTIOMETER: 256/1023 (25%)
🔘 BUTTON 0 PRESSED: PREV/UP (Pin D5)
```

### Troubleshooting:

- **No button presses detected**: Check wiring and pull-up resistors
- **Buttons triggering multiple times**: Adjust debounce delay in code
- **Potentiometer not changing**: Check A0 connection and power supply
- **Erratic potentiometer readings**: Check for loose connections or interference

### Hardware Requirements:

- NodeMCU v2 (ESP8266)
- 4 buttons connected to D5, D6, D7, D8 (with pull-up resistors or using internal pull-ups)
- Potentiometer connected to A0 (with proper power connections)

### To return to main firmware:

```bash
pio run -e Spoty_Controler --target upload
```
