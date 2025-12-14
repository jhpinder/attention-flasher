# attention-flasher: Complete Development Plan

**Purpose:** This document provides a complete, LLM-executable specification for developing the attention-flasher firmware and project structure. It is designed to be consumed by Claude, GPT-4, or other advanced code generation LLMs to produce a fully functional project from a single comprehensive prompt.

---

## Executive Summary

**Project:** attention-flasher  
**Platform:** ESP32 (Arduino framework)  
**Build System:** PlatformIO  
**Integration:** HomeKit via HomeSpan  
**Primary Libraries:** HomeSpan, Adafruit NeoPixel  
**License:** MIT  

The attention-flasher is a USB-powered, HomeKit-accessible visual alert device featuring an addressable LED strip/ring. It exposes four distinct light services in HomeKit:
1. **Simple RGB Light** — user-controlled RGB color and brightness
2. **Flashing Alert** — rapid white flash pattern
3. **Ping Alert** — smooth ramp-and-fade pattern with auto-off
4. **Police Animation** — looping blue/white pattern

Patterns interrupt each other with strict priority: any new pattern immediately stops all running patterns and disables other HomeKit services.

---

## Hardware Specification

### Bill of Materials (BOM)

| Component | Part # / Description | Qty | Notes |
|-----------|----------------------|-----|-------|
| Microcontroller | ESP32 DevKit (WROOM-32) | 1 | PlatformIO board: `esp32dev` |
| LED Strip | Adafruit NeoPixel Ring (12 x WS2812 5050) | 1 | Adjustable via `LED_COUNT` constant; strip length flexible |
| Power Supply | USB 5V >= 1.5A | 1 | Supports full-white brightness on 12 LEDs |
| Level Shifter | Adafruit TXB0104 Bi-Directional Level Shifter | 1 | **REQUIRED** for reliable 3.3V→5V conversion; bi-directional with auto-direction sensing |
| Buttons (optional) | 6mm tactile push-button | 3 | For pattern dismiss, brightness up/down (GPIO configurable) |
| Diffuser | 3D printed or frosted acrylic | 1 | Mount for desk/monitor |
| Cable | USB-C or Micro-USB | 1 | Depends on ESP32 board variant |

### Power Budget

- **Per-LED full-white draw:** ~60 mA at 5V
- **12 LEDs full-white:** ~720 mA
- **ESP32 idle:** ~80–100 mA
- **Total recommended USB supply:** >= 1.5A

### GPIO Pinout (Configurable in Code)

| Signal | Default GPIO | Purpose | Notes |
|--------|--------------|---------|-------|
| `LED_DATA` | GPIO 5 | Data line to WS2812 | Configure in `LED_PIN` constant |
| `BUTTON_DISMISS` | GPIO 18 | Dismiss current pattern | Configure in `BUTTON_DISMISS_PIN` constant |
| `BUTTON_BRIGHTNESS_UP` | GPIO 19 | Increase RGB light brightness | Configure in `BUTTON_BRIGHTNESS_UP_PIN` constant |
| `BUTTON_BRIGHTNESS_DOWN` | GPIO 23 | Decrease RGB light brightness | Configure in `BUTTON_BRIGHTNESS_DOWN_PIN` constant |

### Electrical Details

- **WS2812 data signal:** 5V nominal, requires proper level shifting from ESP32's 3.3V
- **Level Shifter:** Use Adafruit TXB0104 for 3.3V → 5V conversion (see Hardware Documentation for details)
- **TXB0104 features:** Bi-directional, auto-direction sensing, ~5ns propagation delay, minimal power draw
- **Power delivery:** Connect USB 5V directly to NeoPixel power pins and ESP32 USB port
- **Ground:** Common ground between ESP32, TXB0104, and NeoPixel strip (critical for proper operation)

---

## Software Architecture

### Project Structure

```
attention-flasher/
├── README.md                       # User guide and specs
├── DEVELOPMENT_PLAN.md             # This file
├── platformio.ini                  # PlatformIO build configuration
├── src/
│   └── main.cpp                    # Main firmware (all patterns, HomeKit logic)
├── include/
│   ├── config.h                    # Configuration constants (pins, LED count, defaults)
│   ├── led_patterns.h              # Pattern definitions (flash, ping, police)
│   ├── homekit_services.h          # HomeKit service definitions
│   └── utils.h                     # Utility functions (timing, interpolation)
└── data/
    └── animations.json             # JSON animation definitions (optional future use)
```

### Core Modules

#### 1. **config.h** — Configuration Constants

Define all user-tunable parameters:

```cpp
#ifndef CONFIG_H
#define CONFIG_H

// GPIO Pins
#define LED_PIN 5
#define BUTTON_DISMISS_PIN 18
#define BUTTON_BRIGHTNESS_UP_PIN 19
#define BUTTON_BRIGHTNESS_DOWN_PIN 23

// LED Configuration
#define LED_COUNT 12
#define LED_BRIGHTNESS_DEFAULT 128  // 0-255 for RGB light mode

// Behavior Timings (milliseconds)
#define FLASH_ON_MS 100
#define FLASH_OFF_MS 400
#define PING_RAMP_MS 20
#define PING_FADE_MS 2000
#define POLICE_FRAME_DURATION_MS 150

// HomeKit Service Names
#define SERVICE_NAME_RGB "RGB Light"
#define SERVICE_NAME_FLASH "Flashing"
#define SERVICE_NAME_PING "Ping"
#define SERVICE_NAME_POLICE "Police"

#endif
```

#### 2. **led_patterns.h** — Pattern Engines

Encapsulate pattern logic into reusable classes:

```cpp
#ifndef LED_PATTERNS_H
#define LED_PATTERNS_H

#include <Adafruit_NeoPixel.h>

enum PatternType {
  PATTERN_IDLE,
  PATTERN_RGB,
  PATTERN_FLASH,
  PATTERN_PING,
  PATTERN_POLICE
};

class PatternEngine {
public:
  PatternEngine(Adafruit_NeoPixel& strip);
  
  // Pattern lifecycle
  void startPattern(PatternType type);
  void stopPattern();
  void update(unsigned long currentMillis);
  
  // Getters
  PatternType getCurrentPattern() const;
  bool isRunning() const;
  
  // Pattern-specific setters
  void setRGBColor(uint8_t r, uint8_t g, uint8_t b);
  void setRGBBrightness(uint8_t brightness);
  
private:
  Adafruit_NeoPixel& strip;
  PatternType currentPattern;
  unsigned long patternStartTime;
  
  // Pattern implementation methods
  void updateFlash(unsigned long elapsed);
  void updatePing(unsigned long elapsed);
  void updatePolice(unsigned long elapsed);
  void updateRGB();
  void setAllLEDs(uint8_t r, uint8_t g, uint8_t b);
};

#endif
```

#### 3. **homekit_services.h** — HomeKit Service Definitions

Define HomeSpan services and linked callbacks:

```cpp
#ifndef HOMEKIT_SERVICES_H
#define HOMEKIT_SERVICES_H

#include "HomeSpan.h"

// Base service class for all light services
struct LightService : public Service::Lightbulb {
  Characteristic::On power;
  Characteristic::Brightness brightness;
  
  LightService(PatternEngine* engine, PatternType type);
  bool update() override;
  
private:
  PatternEngine* patternEngine;
  PatternType linkedPattern;
};

// RGB Light service (supports full color control)
struct RGBLightService : public Service::Lightbulb {
  Characteristic::On power;
  Characteristic::Brightness brightness;
  Characteristic::Hue hue;
  Characteristic::Saturation saturation;
  
  RGBLightService(PatternEngine* engine);
  bool update() override;
  
private:
  PatternEngine* patternEngine;
};

#endif
```

#### 4. **utils.h** — Utility Functions

Provide reusable math and timing utilities:

```cpp
#ifndef UTILS_H
#define UTILS_H

#include <cstdint>

// Interpolation
uint8_t linearInterpolate(uint8_t start, uint8_t end, float t);
uint32_t colorBrightness(uint32_t color, uint8_t brightness);

// Timing helpers
unsigned long elapsedMillis(unsigned long startTime, unsigned long currentTime);
float normalizedTime(unsigned long elapsed, unsigned long duration);

// Color conversions
void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b);

#endif
```

#### 5. **main.cpp** — Main Firmware

Integrate all modules, initialize HomeKit and hardware:

- **Initialization block:**
  - Configure serial @ 115200 baud
  - Initialize Adafruit_NeoPixel with `LED_PIN`, `LED_COUNT`, NEO_GRB + NEO_KHZ800
  - Instantiate PatternEngine
  - Configure GPIO button pins (INPUT_PULLUP)
  - Initialize HomeSpan with device name and services
  - Add all four service objects (RGB, Flash, Ping, Police)

- **Main loop:**
  - Read button states and debounce
  - Update pattern engine based on HomeKit commands
  - Enforce interruption policy (new pattern → disable other services)
  - Call `HomeSpan.poll()` to handle HomeKit requests
  - Update LEDs via pattern engine

- **Interruption policy implementation:**
  ```cpp
  void activatePattern(PatternType newPattern) {
    patternEngine.startPattern(newPattern);
    // Disable HomeKit services for other patterns
    if (newPattern != PATTERN_RGB) rgbService->power.setVal(0);
    if (newPattern != PATTERN_FLASH) flashService->power.setVal(0);
    if (newPattern != PATTERN_PING) pingService->power.setVal(0);
    if (newPattern != PATTERN_POLICE) policeService->power.setVal(0);
  }
  ```

---

## Pattern Specifications (Precise Timings & Behaviors)

### 1. Simple RGB Light

**HomeKit Service:** Lightbulb with Hue, Saturation, Brightness  
**User Control:** Full RGB color selection + brightness (0–255)  
**Pattern Behavior:**
- Continuously displays the selected color and brightness.
- No animation; static output.
- Brightness applies directly: `(R×B/255, G×B/255, B×B/255)` where B = brightness (0–255).

**Interruption:** Canceled immediately by any other pattern.

### 2. Flashing White Alert

**HomeKit Service:** Lightbulb (On/Off switch)  
**Activation:** `On = true` in HomeKit  
**Pattern Behavior:**
- White color (255, 255, 255) at 100% brightness
- Repeating on/off cycle: **100 ms ON, 400 ms OFF**
- Cycle duration: 500 ms total
- Loops indefinitely until stopped

**Interruption:** Canceled by new pattern or `On = false` in HomeKit.

### 3. Ping Alert

**HomeKit Service:** Lightbulb (On/Off switch)  
**Activation:** `On = true` in HomeKit  
**Pattern Behavior:**
- White color (255, 255, 255)
- **Phase 1 (Ramp):** Linear brightness ramp from 0% to 100% over **20 ms**
- **Phase 2 (Fade):** Linear brightness fade from 100% to 0% over **2000 ms**
- Total duration: 2020 ms
- **Auto-off:** After fade completes (t=2020 ms), firmware sets `power.setVal(0)` to disable the Ping service in HomeKit

**Interruption:** Canceled mid-animation by new pattern. If interrupted, auto-off does NOT execute.

### 4. Police Animation

**HomeKit Service:** Lightbulb (On/Off switch)  
**Activation:** `On = true` in HomeKit  
**Pattern Behavior:**
- Looping animation; no auto-off
- Animation frames defined as JSON (see below)
- Each frame has a duration and LED color map
- Loops continuously until stopped or interrupted

**Default Police Animation (12-LED ring):**

```json
{
  "name": "police_loop",
  "led_count": 12,
  "loop": true,
  "steps": [
    {
      "duration_ms": 150,
      "pattern": [
        {"start": 0, "end": 5, "color": [0,0,255]},
        {"start": 6, "end": 11, "color": [255,255,255]}
      ]
    },
    {
      "duration_ms": 150,
      "pattern": [
        {"start": 0, "end": 5, "color": [255,255,255]},
        {"start": 6, "end": 11, "color": [0,0,255]}
      ]
    }
  ]
}
```

**Animation Expansion Logic:**
- For LED counts != 12, scale frame logic symmetrically:
  - Frame 1: First half = blue, second half = white
  - Frame 2: First half = white, second half = blue
  - Duration: 150 ms each
  - Loop forever

**Interruption:** Canceled by new pattern or `On = false` in HomeKit.

---

## Interruption Policy (Strict)

**Rule 1: Only one pattern active at a time**
- When any pattern is activated, immediately stop all other patterns.

**Rule 2: HomeKit service reflection**
- When pattern X starts, firmware calls `.setVal(0)` on HomeKit services for other patterns.
- Example: If Ping starts, HomeKit shows:
  - RGB Light → `On = false`
  - Flashing → `On = false`
  - Ping → `On = true` (current)
  - Police → `On = false`

**Rule 3: Button dismiss behavior**
- Dismiss button (GPIO configured in `BUTTON_DISMISS_PIN`) immediately calls `stopPattern()`.
- Must debounce button reads (suggest 50 ms debounce window).

---

## HomeKit Integration via HomeSpan

### HomeKit Accessory Configuration

**Accessory Name:** "Attention Flasher" (or user configurable)  
**Accessory Category:** Light  
**Services Exposed:**

1. **Simple RGB Light**
   - Service Type: `Lightbulb`
   - Characteristics: `On`, `Brightness`, `Hue`, `Saturation`
   - Linked Pattern: `PATTERN_RGB`

2. **Flashing Alert**
   - Service Type: `Lightbulb` (simple on/off)
   - Characteristics: `On`
   - Linked Pattern: `PATTERN_FLASH`

3. **Ping Alert**
   - Service Type: `Lightbulb` (simple on/off)
   - Characteristics: `On`
   - Linked Pattern: `PATTERN_PING`

4. **Police Animation**
   - Service Type: `Lightbulb` (simple on/off)
   - Characteristics: `On`
   - Linked Pattern: `PATTERN_POLICE`

### HomeKit Service Naming

Each service should be named descriptively for clarity in HomeKit app:

```cpp
homeSpan.setAccessoryName("Attention Flasher");
homeSpan.setDefaultAccessoryName("Attention Flasher");

// Example service naming (HomeSpan API)
auto rgbService = new RGBLightService(patternEngine);
rgbService->setName("RGB Light");

auto flashService = new FlashLightService(patternEngine);
flashService->setName("Flashing");

auto pingService = new PingLightService(patternEngine);
pingService->setName("Ping");

auto policeService = new PoliceLightService(patternEngine);
policeService->setName("Police");
```

### Network Configuration

- **Wi‑Fi Setup:** HomeSpan provides a captive portal on first boot (or air-gap pairing). User connects ESP32 to home Wi‑Fi via HomeSpan's interface.
- **HomeKit Pairing:** Standard HomeKit setup code (HomeSpan generates one automatically or you can set a custom code).
- **Security:** Local-only; no cloud dependency. HomeKit communication encrypted end-to-end within local network.

---

## Build & Deployment

### PlatformIO Configuration

**platformio.ini:**

```ini
[env:esp32dev]
platform = espressif32@6.7.0
board = esp32dev
framework = arduino
board_build.partitions = default.csv

lib_deps =
  HomeSpan@^1.10.0
  Adafruit NeoPixel@^1.11.0
  Adafruit GFX@^1.11.9

monitor_speed = 115200
upload_speed = 921600
upload_port = /dev/ttyUSB0
monitor_port = /dev/ttyUSB0

# Build flags (optional: disable verbose logging for production)
build_flags =
  -DCORE_DEBUG_LEVEL=0
```

### Build & Upload Steps

```bash
# Install PlatformIO (if not already installed)
pip install platformio

# Clone/navigate to project
cd attention-flasher

# Build firmware
pio run -e esp32dev

# Upload to ESP32
pio run -e esp32dev -t upload

# Monitor serial output
pio device monitor -b 115200
```

### Serial Monitor Output (Expected on Boot)

```
[ESP32] Starting HomeSpan...
[ESP32] Initializing NeoPixel strip (12 LEDs on GPIO 5)
[ESP32] HomeSpan ready. SSID: Attention-Flasher-XXXX
[ESP32] Default HomeKit Setup Code: XXX-XX-XXX
```

---

## Testing & Verification Checklist

### 1. Hardware Verification

- [ ] ESP32 powers on (LED blinks or serial prints boot message)
- [ ] NeoPixel strip shows initialization pattern (brief startup animation)
- [ ] USB power is stable (measure 5V at NeoPixel connector; should be ≥4.9V under load)
- [ ] Buttons register presses (optional; connect serial monitor to verify GPIO reads if wired)

### 2. Firmware Verification

- [ ] Serial monitor shows HomeSpan startup message
- [ ] HomeSpan SSID visible in Wi‑Fi networks list
- [ ] HomeKit pairing succeeds with provided setup code
- [ ] All four services appear in HomeKit app (RGB Light, Flashing, Ping, Police)

### 3. Pattern Verification

| Pattern | Test | Expected Behavior |
|---------|------|-------------------|
| **RGB Light** | Toggle `On`, set hue/saturation/brightness | LED strip displays selected color immediately |
| **Flashing** | Toggle `On` → observe for 2–3 cycles, then toggle `Off` | White LED blinks 100 ms on / 400 ms off, then stops |
| **Ping** | Toggle `On`, observe fade, check HomeKit service state | LED ramps to white over ~20 ms, fades over ~2000 ms, then `On` auto-resets to `false` |
| **Police** | Toggle `On` → observe loops | Animation alternates blue/white at 150 ms per frame, loops indefinitely |

### 4. Interruption Policy Verification

- [ ] Start RGB Light (red), then toggle Flashing `On` → RGB service switches to `Off`, strip starts flashing white
- [ ] Ping running, then toggle Police `On` → Ping stops mid-animation, Police animation starts, Ping service shows `Off` in HomeKit
- [ ] Press dismiss button mid-pattern → pattern stops immediately

### 5. Load Testing

- [ ] Monitor ESP32 temperature and current draw under continuous operation
- [ ] Run all four patterns in sequence for 10+ minutes; verify no crashes or glitches
- [ ] Verify Wi‑Fi connection stability (no disconnects)

---

## Implementation Order (Recommended)

### Phase 1: Foundation (Days 1–2)

1. **Set up PlatformIO project structure**
   - Create `platformio.ini` with dependencies
   - Create `src/main.cpp`, `include/config.h`, `include/utils.h`

2. **Implement basic NeoPixel control**
   - Initialize Adafruit_NeoPixel library
   - Write test to set all LEDs to a color
   - Verify serial output

3. **Implement PatternEngine core**
   - Skeleton PatternEngine class with `startPattern()`, `stopPattern()`, `update()`
   - Test with simple RGB setAll() function

### Phase 2: Pattern Implementation (Days 3–4)

4. **Implement pattern engines**
   - Flashing: 100 ms on / 400 ms off loop
   - Ping: 20 ms ramp, 2000 ms fade, auto-off logic
   - Police: JSON-based animation frame stepping
   - RGB: Static color + brightness

5. **Add button input handling**
   - GPIO setup (INPUT_PULLUP)
   - Debouncing logic (50 ms window)
   - Dismiss button → `stopPattern()`

### Phase 3: HomeKit Integration (Days 5–6)

6. **Integrate HomeSpan**
   - Initialize HomeSpan in main loop
   - Create RGBLightService, FlashLightService, PingLightService, PoliceLightService classes
   - Wire `.update()` callbacks to PatternEngine calls

7. **Implement interruption policy**
   - When pattern X starts, call `.setVal(0)` on other services
   - Test with manual HomeKit app toggles

### Phase 4: Testing & Polish (Days 7+)

8. **Full integration testing**
   - Run all test cases from checklist above
   - Verify interruption priority
   - Load testing (patterns running 10+ min continuously)

9. **Documentation & release**
   - Update README with confirmed GPIO pins and defaults
   - Create DEVELOPMENT_PLAN.md (this file)
   - Tag first release on GitHub

---

## Known Constraints & Assumptions

1. **3.3V Logic on WS2812:** The Adafruit TXB0104 level shifter is **required** for reliable conversion to 5V logic. See Hardware Documentation (docs/HARDWARE.md) for complete details and assembly instructions.

2. **HomeSpan Version:** Assumes HomeSpan >= 1.10.0; API may differ in earlier versions.

3. **Button Debouncing:** Simple debounce window (50 ms suggested); production may require more sophisticated debouncing.

4. **Memory:** ESP32 SPRAM is limited (~520 KB free typically). Pattern animation frames stored in PROGMEM (flash) when possible.

5. **OTA Updates:** Not implemented in initial version; can be added later via PlatformIO OTA or custom HTTP endpoint.

6. **Security:** Assumes home Wi‑Fi is trusted. HomeKit encryption handles pairing security.

---

## Future Enhancements (Out of Scope)

- [ ] OTA firmware updates (via HomeKit or HTTP)
- [ ] Custom animation upload via JSON files
- [ ] MQTT integration (alternative to HomeKit)
- [ ] Motion sensor trigger (auto-start pattern on motion)
- [ ] Sound-reactive patterns
- [ ] Web UI for configuration (no HomeKit app required)
- [ ] SD card support for storing animation presets

---

## Notes for LLM Consumption

**To use this document as a single-prompt specification for code generation:**

1. Copy this entire document into an LLM prompt.
2. Request the LLM to generate:
   - `src/main.cpp` (complete, production-ready)
   - `include/config.h` (configuration constants)
   - `include/led_patterns.h` (PatternEngine class definition)
   - `include/homekit_services.h` (HomeKit service classes)
   - `include/utils.h` (utility functions)
   - `platformio.ini` (build configuration)

3. The LLM should produce code that:
   - Compiles without errors on PlatformIO (esp32dev board)
   - Initializes all hardware correctly
   - Exposes four HomeKit services with correct names and characteristics
   - Implements all pattern logic with exact timings specified above
   - Enforces strict interruption policy
   - Passes all tests in the verification checklist

---

**Document Version:** 1.0  
**Last Updated:** December 13, 2025  
**Status:** Ready for LLM consumption
