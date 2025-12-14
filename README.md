# attention-flasher

One-line summary
----------------
An ESP32-powered, HomeKit-accessible attention light (NeoPixel) that provides multiple visual alert patterns (flash, ping, police, RGB lamp) for situations where audible alerts aren't practical.

Goals & Audience
-----------------
- Primary goal: simple, low-latency visual attention cues for gamers or desktop users who may miss audible notifications.
- Audience: makers and HomeKit users who want a local, USB-powered, monitor/desk-mounted visual notifier.

Features
--------
- Simple RGB light service (set color + brightness)
- Flashing white alert (100ms on / 400ms off)
- Ping alert (20ms ramp to 100%, fade out over 2000ms, then HomeKit light turns off)
- Police animation (looping blue/white animation; animation described in JSON below)
- Configurable pins and LED count via code constants
- HomeKit integration via HomeSpan (local-only; no cloud required)
- PlatformIO-ready build (Arduino framework) using Adafruit NeoPixel

Hardware (suggested)
--------------------
- ESP32 development board (e.g., esp32dev / DevKitC) — PlatformIO board: `esp32dev`
- WS2812 / NeoPixel LEDs (example: 12-LED NeoPixel ring) — adjust `LED_COUNT` in code
- USB 5V power (wall adapter or PC USB)
- Optional 3 tactile buttons (dismiss / brightness up / brightness down)
- Optional logic level shifter (3.3V -> 5V) if your strip is not tolerant of 3.3V data signals
- Diffuser / mount for desk or monitor

Power note
----------
- WS2812 typical full-white current: ~60mA per LED. For 12 LEDs, budget up to ~720mA at 5V under full-white. Use a USB supply rated >= 1A to be safe.

Software stack
--------------
- IDE: PlatformIO
- Framework: Arduino
- Libraries:
  - HomeSpan (HomeKit accessory framework)
  - Adafruit NeoPixel (or compatible)

Quick start (PlatformIO)
------------------------
1. Install PlatformIO in VSCode.
2. Add `lib_deps` for HomeSpan and Adafruit NeoPixel in `platformio.ini` (example below).
3. Configure `src/main.cpp` constants for your `LED_PIN`, `LED_COUNT`, and `BUTTON_PIN`.
4. Build and upload:

```bash
pio run -e esp32dev -t upload
```

platformio.ini (minimal example)
-------------------------------
```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
  HomeSpan
  Adafruit NeoPixel
monitor_speed = 115200
upload_port = /dev/ttyUSB0  ; adjust for your system
```

Configuration (code variables)
------------------------------
The firmware exposes a small set of top-level constants for easy tuning. Change these in `src/main.cpp` before building.

- `LED_PIN` — data pin for NeoPixel (default: choose a GPIO such as 5)
- `LED_COUNT` — number of LEDs in the strip/ring (default: 12)
- `BUTTON_PIN` — button GPIO; leave unassigned if not used
- `BRIGHTNESS_DEFAULT` — used by simple RGB light mode (0-255)

Behavior spec (explicit)
------------------------
- Flashing white: ON 100 ms, OFF 400 ms (repeats). Brightness = 100% for this pattern.
- Ping: linear ramp from 0% to 100% in 20 ms, then linear fade to 0% over 2000 ms. After fade completes, firmware sets HomeKit light `On` characteristic to `false`.
- Police: looping animation. All police and alert patterns run at 100% brightness.

Interruption policy
-------------------
- New incoming pattern (any pattern) immediately interrupts the running pattern. When a pattern starts, the firmware will explicitly turn off other HomeKit "device" services so HomeKit reflects the single active pattern.

Police animation — JSON format (looping)
--------------------------------------
Use this JSON schema to describe an animation the firmware can load or to provide an LLM with a precise animation spec. The example below alternates blue and white sides on a 12-LED ring.

Example animation (12 LEDs, loop true):

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

JSON schema notes
-----------------
- `start` and `end` are inclusive LED indices (0-based). Use `all` or `0..N-1` to target entire ring.
- `color` is an RGB triplet in 0–255.
- `duration_ms` is the duration that step is held before advancing.

HomeKit mapping (concept)
-------------------------
- The firmware exposes multiple HomeKit services (or a single accessory with multiple switches/lights) mapped to behaviors. Example mapping:
  - `Simple RGB Light` — standard HomeKit Lightbulb (color + brightness)
  - `Flashing` — HomeKit Lightbulb service that, when turned ON, starts the Flashing pattern
  - `Ping` — HomeKit Lightbulb service that, when turned ON, performs the Ping pattern and then sets itself to OFF
  - `Police` — HomeKit Lightbulb service that starts/stops the Police animation

When a pattern service is enabled, firmware will set all other pattern services to `Off` to reflect the interruption policy.

Network & provisioning
----------------------
- HomeSpan handles initial Wi‑Fi provisioning (air‑gap pairing or captive setup depending on HomeSpan/firmware). The user will configure Wi‑Fi via HomeSpan's setup flow.

Level shifting recommendation
----------------------------
- Many WS2812 strips tolerate 3.3V data while powered at 5V, but results vary. If you see flicker or unreliable colors, add a 74HCT125 / MOSFET level shifter to move data to 5V.

Testing & verification (manual)
------------------------------
- Boot behavior: LEDs should initialize and show a short startup pattern.
- Flash test: enable `Flashing` service — LED should blink 100ms on / 400ms off.
- Ping test: enable `Ping` service — LED should ramp to full in ~20ms then fade over ~2000ms; `Ping` HomeKit service should switch to `Off` at the end.
- Police test: enable `Police` service — animation should loop; starting `Ping` or `Flashing` should interrupt police.

Repository notes
----------------
- This README contains the LLM-friendly spec and JSON animation format. Keep firmware constants at the top of `src/main.cpp` for easy editing.

License
-------
MIT

Open questions / items to confirm
--------------------------------
- Exact ESP32 board model (if you want prebuilt `platformio.ini` envs).
- Exact `LED_PIN` / `BUTTON_PIN` choices — currently exposed as code variables; do you want defaults (e.g., `LED_PIN=5`)?
- Police animation timing and colors — the JSON above is a suggested default; confirm or provide alternate frames.
- Do you want OTA firmware updates? If yes, preferred method (PlatformIO OTA, custom HTTP OTA).

If you want, I can now:
- Draft `src/main.cpp` skeleton with the constants and HomeSpan + NeoPixel initialization (PlatformIO-ready), or
- Produce a complete `platformio.ini` and a small sample `main.cpp` implementing the patterns above.

If you'd like the skeleton or full firmware, tell me your preferred defaults for `LED_PIN` and `LED_COUNT` (e.g., `LED_PIN=5`, `LED_COUNT=12`).

---

<br>

#### ORIGINAL PROMPT:

# attention-flasher

### Problem scenario:
Some people have trouble hearing when their name is called while playing video games with headphones on. It becomes a problem when they also do not check their phones for texts or calls. Since sound is not an option, the next best thing in terms of simplicity is to use a light to get their attention.

### Proposed solution:
A controllable light attached to the monitor or placed visibly on the desk would allow others to get their attention without shouting or texting.

### What it does:
This device shows up in homekit as 4 different light devices, as outlined below. It allows anyone in the home to control the attention flasher to improve communication. This is mostly for just myself, not going to try to sell it.

### Initial hardware considerations:
- ESP32
- Neopixel or similar for RGB
    - Adafruit Industries NeoPixel Ring - 12 x WS2812 5050 RGB LED with Integrated Drivers
- Multiple buttons to dismiss alert and control brightness
- 3D printed enclosure with light diffuser for LEDs
- USB power supply

### Initial software considerations:
- Homespan to interface ESP32 with HomeKit
- Multiple "devices" combined in one ESP32
    - Simple RGB light
    - Flashing white light
        - When turned on, the light flashes on for 100ms, off for 400
    - "Ping" white light
        - Ramps to 100% over 20ms, fades to zero over 2000ms
        - Automatically turns homekit "light" off after fading to zero
    - Police lights
        - Flashes blue and white lights to mimic a police light when toggled on