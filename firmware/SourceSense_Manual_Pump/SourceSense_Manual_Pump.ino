/*
  SourceSense complete code | Team 200: Victory OK | atomQUEST 2026, Track 2
  Board: ESP32 Dev Module. Serial Monitor: 115200, Newline.
  Libraries: Adafruit GFX, Adafruit ST7735 and ST7789, OneWire, DallasTemperature.

  Pump is NOT connected to the ESP32: it runs from an external 5V supply and a hand switch.

  Type after every restart:  c = save clear water level,  m680 = ORP value in mV
  Run one cycle (pump OFF): press BOOT or type n
  When the screen says Pump ON: switch the pump on, let water flow, switch it off,
  then press BOOT or type n to close the valve.
  Other commands: m = clear ORP, b = learn normal water again, x = close valves, stop
*/
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <driver/gpio.h>

// Pins
#define TFT_CS 5
#define TFT_RST 4
#define TFT_DC 2          // TFT SCK = GPIO18, SDA = GPIO23
#define TEMP_PIN 13       // DS18B20 data, 4.7k to 3V3
#define TDS_POWER_PIN 16  // TDS board +
#define TDS_PIN 34        // TDS board A
#define TURB_PIN 35       // turbidity S through 10k, 20k to GND
#define FLOW_PIN 27       // YF-S201 yellow through 10k, 20k to GND
#define RO_RELAY 26
#define BYPASS_RELAY 25
#define BUTTON_PIN 0

// Settings you may change
#define RELAY_ACTIVE_LOW 1            // 0 if your relay turns on with HIGH
const float TDS_FACTOR = 1.0;         // TDS correction from your TDS pen
const float CALIBRATION_FACTOR = 7.5; // YF-S201: Hz = 7.5 x L/min
const float TDS_LIMIT = 280;          // never bypass above this
const float ORP_MIN = 550;            // below this: no chlorine
const float CLOUDY_DROP_PCT = 3;      // set this from your milk test
const unsigned long VALVE_MAX_MS = 60000; // valve closes by itself after this

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

volatile unsigned long pulseCount = 0;
bool orpEntered = false, flowWarn = false, lastBtn = true, runNow = false;
float orpValue = 0, clearMv = 0, normalTds = 0, normalOrp = 0, normalTurb = 0;
float litresRO = 0, litresBypass = 0, lastLitres = 0;
int learnCount = 0;
unsigned long cycleCount = 0;
char cmd[16];
byte cmdLen = 0;

void IRAM_ATTR pulseCounter() { pulseCount = pulseCount + 1; }

void relay(uint8_t pin, bool on) {
  digitalWrite(pin, (on == (bool)RELAY_ACTIVE_LOW) ? LOW : HIGH);
}

void relaySetup(uint8_t pin) {
#if RELAY_ACTIVE_LOW
  gpio_set_level((gpio_num_t)pin, 1);     // released before the pin becomes an output
  pinMode(pin, OUTPUT_OPEN_DRAIN);
#else
  gpio_set_level((gpio_num_t)pin, 0);
  pinMode(pin, OUTPUT);
#endif
  relay(pin, false);
}

void allOff() {
  relay(RO_RELAY, false);
  relay(BYPASS_RELAY, false);
}

float readMv(uint8_t pin) {               // average of 30 samples
  long total = 0;
  for (int i = 0; i < 30; i++) {
    total += analogReadMilliVolts(pin);
    delay(10);
  }
  return total / 30.0;
}

float readTds(float mv, float tempC) {
  float v = (mv / 1000.0) / (1.0 + 0.02 * (tempC - 25.0));
  return (133.42 * v * v * v - 255.86 * v * v + 857.39 * v) * 0.5 * TDS_FACTOR;
}

const char* decide(float tempC, float tds, float tdsMv, float turbMv) {
  if (tempC < 1 || tempC > 60) return "FAULT:TEMP";
  if (tds < 10) return "FAULT:TDS DRY";
  if (tds > 2000 || tdsMv > 3000) return "FAULT:TDS RNG";
  if (turbMv < 300 || turbMv > 3050) return "FAULT:TURB";
  if (tds > TDS_LIMIT) return "HIGH TDS";
  if (!orpEntered) return "NO ORP VALUE";
  if (orpValue < ORP_MIN) return "NO CHLORINE";

  const float keep = 1.0 - CLOUDY_DROP_PCT / 100.0;
  if (clearMv > 0 && turbMv < clearMv * keep) return "CLOUDY WATER";

  bool cloudy = learnCount > 0 && turbMv < normalTurb * keep;
  bool changed = learnCount > 0 && (tds > normalTds * 1.25 || orpValue < normalOrp - 150);

  if (learnCount < 2) {                   // learn this home's normal water
    if (learnCount == 0 || cloudy || changed) {
      normalTds = tds; normalOrp = orpValue; normalTurb = turbMv; learnCount = 1;
    } else {
      normalTds = (normalTds + tds) / 2;
      normalOrp = (normalOrp + orpValue) / 2;
      normalTurb = (normalTurb + turbMv) / 2;
      learnCount = 2;
    }
    return learnCount >= 2 ? "SAFE" : "LEARNING";
  }
  if (cloudy) return "CLOUDY WATER";
  if (changed) return "SOURCE CHANGE";

  normalTds = 0.9 * normalTds + 0.1 * tds;   // follow slow changes in the same water
  normalOrp = 0.9 * normalOrp + 0.1 * orpValue;
  normalTurb = 0.9 * normalTurb + 0.1 * turbMv;
  return "SAFE";
}

void show(float tds, float tempC, float turbMv, bool bypass, const char* why) {
  float ref = clearMv > 0 ? clearMv : normalTurb;
  float pct = ref > 0 ? 100.0 * turbMv / ref : 100.0;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 2);
  tft.print("TDS "); tft.print(tds, 0); tft.print("ppm");
  tft.setCursor(2, 20);
  if (orpEntered) { tft.print("ORP "); tft.print(orpValue, 0); tft.print(" MAN"); }
  else tft.print("ORP none");
  tft.setCursor(2, 38);
  tft.print("CL"); tft.print(pct, 0); tft.print("% "); tft.print(tempC, 1); tft.print("C");
  tft.setTextSize(3);
  tft.setCursor(2, 60);
  tft.setTextColor(bypass ? ST77XX_GREEN : ST77XX_ORANGE);
  tft.print(bypass ? "BYPASS" : "RO");
  tft.setTextSize(2);
  tft.setCursor(2, 90);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(why);
  tft.setTextSize(1);
  tft.setCursor(2, 114);
  tft.setTextColor(ST77XX_CYAN);
  if (flowWarn) tft.print("NO FLOW?");
  else { tft.print("last "); tft.print(lastLitres, 3); tft.print(" L"); }
}

void message(const char* a, const char* b) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 10); tft.print("SourceSense");
  tft.setCursor(2, 50); tft.print(a);
  tft.setCursor(2, 80); tft.print(b);
}

float dispense(bool bypass) {
  noInterrupts();
  pulseCount = 0;
  interrupts();
  relay(bypass ? BYPASS_RELAY : RO_RELAY, true);  // valve opens first

  message(bypass ? "BYPASS open" : "RO open", "Pump ON");
  tft.setTextSize(1);
  tft.setCursor(2, 105);
  tft.print("Pump OFF, then press BOOT");
  Serial.printf("%s valve open. Switch pump ON. When done, switch it OFF, then press BOOT or type n.\n",
                bypass ? "BYPASS" : "RO");

  bool done = false;
  unsigned long openedAt = millis();
  while (!done && millis() - openedAt < VALVE_MAX_MS) {
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == 'n' || ch == 'x') done = true;
    }
    if (digitalRead(BUTTON_PIN) == LOW) done = true;
    delay(10);
  }
  if (!done) Serial.println("Valve time limit reached. Switch pump OFF.");

  allOff();                                       // pump is already off by hand
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);  // same press must not start a new cycle
  delay(300);
  lastBtn = true;
  noInterrupts();
  unsigned long pulses = pulseCount;
  interrupts();
  flowWarn = pulses < 3;
  return pulses / (CALIBRATION_FACTOR * 60.0);
}

void runCycle() {
  cycleCount++;
  message("Measuring", "Pump OFF");
  allOff();
  delay(500);

  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  float useTemp = (tempC > 1 && tempC < 60) ? tempC : 25.0;

  digitalWrite(TDS_POWER_PIN, HIGH);
  delay(1500);
  float tdsMv = readMv(TDS_PIN);
  digitalWrite(TDS_POWER_PIN, LOW);
  float tds = readTds(tdsMv, useTemp);

  delay(1000);                            // bubbles rise, TDS board is off
  float turbMv = readMv(TURB_PIN);

  const char* why = decide(tempC, tds, tdsMv, turbMv);
  bool bypass = strcmp(why, "SAFE") == 0;

  Serial.printf("[%lu] Temp %.2f C | TDS %.0f ppm | ORP %s%.0f | Turbidity %.0f mV -> %s (%s)\n",
                cycleCount, tempC, tds, orpEntered ? "" : "none ", orpEntered ? orpValue : 0,
                turbMv, bypass ? "BYPASS" : "RO", why);
  show(tds, tempC, turbMv, bypass, why);

  if (strcmp(why, "FAULT:TDS DRY") == 0) {   // jar may be empty: do not run the pump dry
    lastLitres = 0;
    flowWarn = false;
    Serial.println("Jar may be empty: do NOT switch the pump on.");
    message("Jar empty?", "Keep pump OFF");
    delay(3000);
  } else {
    delay(3000);                             // time to read the result before the valve opens
    lastLitres = dispense(bypass);
    if (bypass) litresBypass += lastLitres; else litresRO += lastLitres;
  }
  show(tds, tempC, turbMv, bypass, why);

  Serial.printf("DATA,%lu,%lu,%.2f,%.1f,%.0f,%.1f,%s,%s,%.3f,%.3f,%d\n", cycleCount, (unsigned long)millis(),
                tempC, tds, orpEntered ? orpValue : -1, turbMv, bypass ? "BYPASS" : "RO", why,
                litresRO, litresBypass, flowWarn ? 1 : 0);
}

void command(char* c) {
  float val = atof(c + 1);
  switch (c[0]) {
    case 'n': runNow = true; break;
    case 'm':
      orpEntered = c[1] != 0 && val > -600 && val < 1100;
      orpValue = orpEntered ? val : 0;
      if (orpEntered) Serial.printf("ORP (MANUAL) = %.0f mV\n", orpValue);
      else Serial.println("No ORP value. Water goes to RO.");
      break;
    case 'c': {
      allOff();
      delay(1000);
      float mv = readMv(TURB_PIN);
      if (mv < 300 || mv > 3050) {
        Serial.printf("Clear level REJECTED: %.0f mV. Check wiring and divider.\n", mv);
      } else {
        clearMv = mv;
        Serial.printf("Clear level saved: %.0f mV\n", mv);
      }
      break;
    }
    case 'b':
      learnCount = 0;
      Serial.println("Learning normal water again.");
      break;
    case 'x':
      allOff();
      Serial.println("STOP: valves closed. Switch the pump OFF by hand.");
      message("STOP", "Pump OFF");
      break;
    default:
      Serial.println("Commands: n  m680  m  c  b  x");
  }
}

void setup() {
  relaySetup(RO_RELAY);                   // first: both valves closed at start
  relaySetup(BYPASS_RELAY);
  pinMode(TDS_POWER_PIN, OUTPUT);
  digitalWrite(TDS_POWER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SOURCESENSE ===");

  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(TURB_PIN, ADC_11db);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tempSensor.begin();
  Serial.printf("DS18B20 count: %d\n", tempSensor.getDeviceCount());

  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, FALLING);

  Serial.println("Type c, then m680, then press BOOT");
  message("Type c, m680", "Press BOOT");
}

void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (cmdLen > 0) {
        cmd[cmdLen] = 0;
        command(cmd);
        cmdLen = 0;
      }
    } else if (cmdLen < sizeof(cmd) - 1) {
      cmd[cmdLen++] = ch;
    }
  }
  bool btn = digitalRead(BUTTON_PIN);
  if (lastBtn && !btn) runNow = true;
  lastBtn = btn;
  if (runNow) {
    runNow = false;
    runCycle();
  }
  delay(10);
}
