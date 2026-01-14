/*
 * Attention Flasher - ESP32 HomeKit Visual Alert Device
 * Uses HomeSpan and Adafruit NeoPixel (RGBW support)
 *
 * Hardware:
 * - ESP-WROOM-32 ESP32 ESP-32S (3.3V logic)
 * - Adafruit NeoPixel Jewel RGBW (Product ID: 2860)
 * - 7 x 5050 RGBW LEDs (Cool White ~6000K)
 * - Adafruit TXB0104 Bi-Directional Level Shifter (3.3V ↔ 5V logic)
 * - See docs/HARDWARE.md for complete circuit diagrams and BOM
 *
 * Features:
 * - RGB Light (full color + brightness control)
 * - Flashing Alert (100ms on / 400ms off white flash)
 * - Ping Alert (20ms ramp, 2000ms fade, auto-off)
 * - Police Animation (blue/white alternating pattern)
 */

#include <Arduino.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include "HomeSpan.h"

// ============= CONFIGURATION =============

// GPIO Configuration
#define LED_PIN 15            // NeoPixel data pin
#define BUTTON_DISMISS_PIN -1 // Optional dismiss button (set to GPIO or -1 to disable)

// LED Configuration
#define LED_COUNT 7                // Number of LEDs (Adafruit NeoPixel Jewel RGBW)
#define LED_IS_RGBW true           // Set true for RGBW, false for RGB
#define LED_DEFAULT_BRIGHTNESS 128 // Default brightness for RGB mode (0-255)

// Pattern Timings (milliseconds)
#define FLASH_ON_MS 70
#define FLASH_OFF_MS 160
#define PING_RAMP_MS 40
#define PING_FADE_MS 1500
#define POLICE_FRAME_MS 300

// HomeKit Device Info
#define DEVICE_NAME "Attention Flasher"
#define MANUFACTURER "jhpinder"
#define SERIAL_NUMBER "AF-001"
#define MODEL "ESP32-JEWEL-RGBW-7"
#define FIRMWARE_VERSION "1.0.1"

// ============= PATTERN ENGINE =============

enum PatternType
{
  PATTERN_IDLE,
  PATTERN_RGB,
  PATTERN_FLASH,
  PATTERN_PING,
  PATTERN_POLICE
};

class PatternEngine
{
private:
  Adafruit_NeoPixel &strip;
  PatternType currentPattern = PATTERN_IDLE;
  unsigned long startTime = 0;

  // RGB mode state
  uint8_t rgbR = 255, rgbG = 255, rgbB = 255, rgbW = 0;
  uint8_t rgbBrightness = LED_DEFAULT_BRIGHTNESS;

  // Helper: linear interpolation for uint8_t
  uint8_t lerp8(uint8_t a, uint8_t b, float t)
  {
    t = constrain(t, 0.0f, 1.0f);
    return (uint8_t)(a + (b - a) * t + 0.5f);
  }

  // Helper: create RGBW color (works for both RGB and RGBW strips)
  uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0)
  {
    if (LED_IS_RGBW)
    {
      return strip.Color(r, g, b, w);
    }
    else
    {
      return strip.Color(r, g, b);
    }
  }

  // Helper: set all LEDs to one color
  void setAll(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0)
  {
    uint32_t color = makeColor(r, g, b, w);
    for (int i = 0; i < LED_COUNT; i++)
    {
      strip.setPixelColor(i, color);
    }
    strip.show();
  }

  // Pattern renderers
  void renderRGB()
  {
    float brightness = rgbBrightness / 255.0f;
    uint8_t r = (uint8_t)(rgbR * brightness);
    uint8_t g = (uint8_t)(rgbG * brightness);
    uint8_t b = (uint8_t)(rgbB * brightness);
    uint8_t w = (uint8_t)(rgbW * brightness);
    setAll(r, g, b, w);
  }

  void renderFlash(unsigned long elapsed)
  {
    unsigned long cycle = FLASH_ON_MS + FLASH_OFF_MS;
    unsigned long pos = elapsed % cycle;
    bool on = (pos < FLASH_ON_MS);

    if (on)
    {
      // Prefer RGB channels for white unless user explicitly set the white
      // channel (`rgbW`) — only then drive the cool-white LED.
      uint8_t w = (LED_IS_RGBW && rgbW > 0) ? 255 : 0;
      setAll(255, 255, 255, w);
    }
    else
    {
      setAll(0, 0, 0, 0);
    }
  }

  void renderPing(unsigned long elapsed)
  {
    unsigned long totalDuration = PING_RAMP_MS + PING_FADE_MS;

    if (elapsed >= totalDuration)
    {
      // Pattern complete - stop and clear
      stopPattern();
      return;
    }

    uint8_t brightness;
    if (elapsed < PING_RAMP_MS)
    {
      // Ramp up phase
      float t = (float)elapsed / (float)PING_RAMP_MS;
      brightness = lerp8(0, 255, t);
    }
    else
    {
      // Fade down phase with exponential decay for natural fade perception
      unsigned long fadeElapsed = elapsed - PING_RAMP_MS;
      float t = (float)fadeElapsed / (float)PING_FADE_MS;
      // Exponential decay: fast drop early, then asymptotic tail
      // e^(-5*t) starts at 1 and drops sharply, reaches ~0.007 by t=1
      t = exp(-5.0f * t);
      brightness = (uint8_t)(255 * t + 0.5f);
    }

    setAll(brightness / 32, 0, 0, brightness);
  }

  void renderPolice(unsigned long elapsed)
  {
    // Two-frame animation: half blue/half white, then swap
    unsigned long frameTime = POLICE_FRAME_MS;
    unsigned long twoFrames = frameTime * 2;
    unsigned long pos = elapsed % twoFrames;

    int halfCount = LED_COUNT / 2;
    bool frame1 = (pos < frameTime);

    for (int i = 0; i < LED_COUNT; i++)
    {
      bool firstHalf = (i < halfCount);
      uint32_t color;

      if (frame1)
      {
        // Frame 1: first half blue, second half white
        if (firstHalf)
        {
          color = makeColor(0, 0, 200, 0);
        }
        else
        {
          color = makeColor(0, 0, 0, 255);
        }
      }
      else
      {
        // Frame 2: first half white, second half blue
        if (firstHalf)
        {
          color = makeColor(0, 0, 0, 255);
        }
        else
        {
          color = makeColor(0, 0, 255, 0);
        }
      }

      strip.setPixelColor(i, color);
    }
    strip.show();
  }

public:
  PatternEngine(Adafruit_NeoPixel &s) : strip(s) {}

  void begin()
  {
    strip.begin();
    strip.clear();
    strip.show();
  }

  void startPattern(PatternType type)
  {
    currentPattern = type;
    startTime = millis();
  }

  void stopPattern()
  {
    currentPattern = PATTERN_IDLE;
    strip.clear();
    strip.show();
  }

  void update()
  {
    if (currentPattern == PATTERN_IDLE)
      return;

    unsigned long elapsed = millis() - startTime;

    switch (currentPattern)
    {
    case PATTERN_RGB:
      renderRGB();
      break;
    case PATTERN_FLASH:
      renderFlash(elapsed);
      break;
    case PATTERN_PING:
      renderPing(elapsed);
      break;
    case PATTERN_POLICE:
      renderPolice(elapsed);
      break;
    default:
      break;
    }
  }

  // RGB color control
  void setRgbColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0)
  {
    rgbR = r;
    rgbG = g;
    rgbB = b;
    rgbW = w;
  }

  void setRgbBrightness(uint8_t brightness)
  {
    rgbBrightness = brightness;
  }

  PatternType getCurrentPattern() const
  {
    return currentPattern;
  }
};

// ============= HOMEKIT SERVICES =============

// Global pattern engine (will be initialized in setup)
PatternEngine *patternEngine = nullptr;

// Forward declarations for cross-service interruption
struct RGBLightService;
struct FlashLightService;
struct PingLightService;
struct PoliceLightService;

RGBLightService *rgbService = nullptr;
FlashLightService *flashService = nullptr;
PingLightService *pingService = nullptr;
PoliceLightService *policeService = nullptr;

// Forward declaration - defined after struct definitions
void disableOtherServices(PatternType activePattern);

// RGB Light Service (with HSV color control)
struct RGBLightService : Service::LightBulb
{
  SpanCharacteristic *power;
  SpanCharacteristic *brightness;
  SpanCharacteristic *hue;
  SpanCharacteristic *saturation;

  RGBLightService() : Service::LightBulb()
  {
    new Characteristic::Name("RGBW");
    power = new Characteristic::On(0);
    brightness = new Characteristic::Brightness(50);
    hue = new Characteristic::Hue(0);
    saturation = new Characteristic::Saturation(0);

    Serial.println("Created RGB Light Service");
  }

  boolean update() override
  {
    bool on = power->getNewVal();

    if (!on)
    {
      patternEngine->stopPattern();
      return true;
    }

    // Convert HSV to RGB
    float h = hue->getNewVal() / 360.0f;
    float s = saturation->getNewVal() / 100.0f;
    float v = brightness->getNewVal() / 100.0f;

    // HSV to RGB conversion
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float rf, gf, bf;
    switch (i % 6)
    {
    case 0:
      rf = v;
      gf = t;
      bf = p;
      break;
    case 1:
      rf = q;
      gf = v;
      bf = p;
      break;
    case 2:
      rf = p;
      gf = v;
      bf = t;
      break;
    case 3:
      rf = p;
      gf = q;
      bf = v;
      break;
    case 4:
      rf = t;
      gf = p;
      bf = v;
      break;
    case 5:
      rf = v;
      gf = p;
      bf = q;
      break;
    default:
      rf = 0;
      gf = 0;
      bf = 0;
      break;
    }

    uint8_t r = (uint8_t)(rf * 255);
    uint8_t g = (uint8_t)(gf * 255);
    uint8_t b = (uint8_t)(bf * 255);
    uint8_t w = 0; // Keep white channel at 0 for RGB color mode

    patternEngine->setRgbColor(r, g, b, w);
    patternEngine->setRgbBrightness((uint8_t)(v * 255));
    patternEngine->startPattern(PATTERN_RGB);
    disableOtherServices(PATTERN_RGB);

    return true;
  }
};

// Flash Light Service (simple on/off)
struct FlashLightService : Service::LightBulb
{
  SpanCharacteristic *power;

  FlashLightService() : Service::LightBulb()
  {
    new Characteristic::Name("Strobe");
    power = new Characteristic::On(0);
    Serial.println("Created Flash Light Service");
  }

  boolean update() override
  {
    bool on = power->getNewVal();

    if (on)
    {
      patternEngine->startPattern(PATTERN_FLASH);
      disableOtherServices(PATTERN_FLASH);
    }
    else
    {
      patternEngine->stopPattern();
    }

    return true;
  }
};

// Ping Light Service (simple on/off with auto-off after animation)
struct PingLightService : Service::LightBulb
{
  SpanCharacteristic *power;

  PingLightService() : Service::LightBulb()
  {
    new Characteristic::Name("Ping");
    power = new Characteristic::On(0);
    Serial.println("Created Ping Light Service");
  }

  boolean update() override
  {
    bool on = power->getNewVal();

    if (on)
    {
      patternEngine->startPattern(PATTERN_PING);
      disableOtherServices(PATTERN_PING);
    }
    else
    {
      patternEngine->stopPattern();
    }

    return true;
  }
};

// Police Light Service (simple on/off)
struct PoliceLightService : Service::LightBulb
{
  SpanCharacteristic *power;

  PoliceLightService() : Service::LightBulb()
  {
    new Characteristic::Name("Police");
    power = new Characteristic::On(0);
    Serial.println("Created Police Light Service");
  }

  boolean update() override
  {
    bool on = power->getNewVal();

    if (on)
    {
      patternEngine->startPattern(PATTERN_POLICE);
      disableOtherServices(PATTERN_POLICE);
    }
    else
    {
      patternEngine->stopPattern();
    }

    return true;
  }
};

// Helper: disable all other services when one activates
void disableOtherServices(PatternType activePattern)
{
  if (activePattern != PATTERN_RGB && rgbService)
  {
    rgbService->power->setVal(false);
  }
  if (activePattern != PATTERN_FLASH && flashService)
  {
    flashService->power->setVal(false);
  }
  if (activePattern != PATTERN_PING && pingService)
  {
    pingService->power->setVal(false);
  }
  if (activePattern != PATTERN_POLICE && policeService)
  {
    policeService->power->setVal(false);
  }
}

// ============= GLOBAL OBJECTS =============

// NeoPixel strip (RGBW or RGB based on LED_IS_RGBW setting)
Adafruit_NeoPixel strip(
    LED_COUNT,
    LED_PIN,
    LED_IS_RGBW ? (NEO_GRBW + NEO_KHZ800) : (NEO_GRB + NEO_KHZ800));

// ============= SETUP =============

void setup()
{
  Serial.begin(115200);
  Serial.println("\n\nAttention Flasher Starting...");
  Serial.printf("LED Count: %d, RGBW: %s\n", LED_COUNT, LED_IS_RGBW ? "Yes" : "No");

  // Initialize pattern engine
  patternEngine = new PatternEngine(strip);
  patternEngine->begin();

  // Optional button setup
  if (BUTTON_DISMISS_PIN >= 0)
  {
    pinMode(BUTTON_DISMISS_PIN, INPUT_PULLUP);
    Serial.printf("Dismiss button enabled on GPIO %d\n", BUTTON_DISMISS_PIN);
  }

  // Initialize HomeSpan
  homeSpan.setLogLevel(1);
  homeSpan.begin(Category::Lighting, DEVICE_NAME);

  // Create accessory
  new SpanAccessory();

  // Accessory Information
  new Service::AccessoryInformation();
  new Characteristic::Name(DEVICE_NAME);
  new Characteristic::Manufacturer(MANUFACTURER);
  new Characteristic::SerialNumber(SERIAL_NUMBER);
  new Characteristic::Model(MODEL);
  new Characteristic::FirmwareRevision(FIRMWARE_VERSION);
  new Characteristic::Identify();
  // Create services
  rgbService = new RGBLightService();
  flashService = new FlashLightService();
  pingService = new PingLightService();
  policeService = new PoliceLightService();

  Serial.println("HomeSpan initialization complete");
  Serial.println("Waiting for HomeKit pairing...");
}

// ============= MAIN LOOP =============

void loop()
{
  // Handle optional dismiss button
  if (BUTTON_DISMISS_PIN >= 0 && digitalRead(BUTTON_DISMISS_PIN) == LOW)
  {
    static unsigned long lastPress = 0;
    if (millis() - lastPress > 200)
    { // Simple debounce
      Serial.println("Dismiss button pressed");
      patternEngine->stopPattern();
      // Update all service states
      if (rgbService)
        rgbService->power->setVal(false);
      if (flashService)
        flashService->power->setVal(false);
      if (pingService)
        pingService->power->setVal(false);
      if (policeService)
        policeService->power->setVal(false);
      lastPress = millis();
    }
  }

  // Check if Ping pattern completed and auto-off needed
  if (patternEngine->getCurrentPattern() == PATTERN_IDLE &&
      pingService && pingService->power->getVal() == true)
  {
    pingService->power->setVal(false);
  }

  // Update pattern engine
  patternEngine->update();

  // Handle HomeKit
  homeSpan.poll();
}