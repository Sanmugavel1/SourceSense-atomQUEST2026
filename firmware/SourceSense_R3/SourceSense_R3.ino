/*
  SourceSense Round 3 firmware | Team 200: Victory OK | atomQUEST 2026, Track 2
  Board: ESP32 Dev Module. Serial Monitor: 115200, Newline.
  Libraries: Adafruit GFX, Adafruit ST7735 and ST7789, OneWire, DallasTemperature.

  What changed since Round 2
  - Pump is on the third relay, so the ESP32 starts and stops it (no hand switch).
  - Normal water is learned from 3 matching samples, then FROZEN and saved in flash.
    It no longer creeps upward, and a power cut does not make it learn new water.
  - TDS change rule: more than max(25 ppm, 20 %) above, or max(25 ppm, 30 %) below normal.
  - Typed ORP removed. A source tag (corporation / borewell / tanker) is used instead.
  - Pump runs a FIXED 10 s per cycle (PUMP_RUN_MS). It stops early only on no flow,
    on BOOT, or on x. Flow is still measured and logged (litres, pump_ms).
  - Watchdog: if the program hangs, the ESP32 restarts and every output turns off.
  - Every cycle also shows what a plain TDS < 300 rule would have done.
  - One CSV line (DATA,...) per cycle for the test log.

  Changes after the college lab tests (MIT Chemistry Lab, 23-11-2026)
  - TDS factor 1.029 by default. Against NaCl standards (50-500 mg/L) the probe read
    2.0-3.3 % low (fit slope 0.972). The factor makes it read about 1 % from the standards.
    On real waters it then reads 2-5 % above gravimetric TDS, which errs toward RO (safe side).
    Old saved factors are replaced once (key "calv"). f alone now resets to 1.029, not 1.0.
  - Turbidity: "Clear %" is turned into an estimated NTU using the lab NTU readings
    (98 % = 0.4, 91 % = 3.2, 80 % = 7.1, 61 % = 16.5, 39 % = 31.2, 22 % = 45 NTU).
    Water above 1 NTU (BIS IS 10500 acceptable limit) is CLOUDY. In the lab the 3.2 NTU
    sample passed as CLEAR; it is now CLOUDY. Needs the clear level saved with c using
    distilled or RO water, as in the lab. Without c, the old 3 % drop rule is used.
  - Estimated NTU is shown on the screen and added as the last column of DATA (ntu_est).
  - Checked against the lab data, no change needed: rise limit max(25 ppm, 20 %) caught
    the 44.7 mg/L nitrate spike (+74 ppm); DS18B20 was within 0.2 C of the lab
    thermometer, so no temperature offset is added; 280 ppm hard limit unchanged.

  First time only (all saved in flash):
    s1 / s2 / s3   source: corporation / borewell / tanker
    c              save clear-water level (DISTILLED or RO water in the jar, as in the lab)
    k  then v<ml>  flow calibration: k runs the pump into the bypass jar, press BOOT
                   to stop, measure the water, type e.g. v480
    f<ppm>         optional: correct TDS to a reference (e.g. bottle label), f alone = lab 1.029
    b              learn normal water again (3 matching samples, water goes to RO meanwhile)

  Every day:  BOOT or n = one full cycle,  q = measure only (no water moves)
              l<name> = sample label for the log
              x = stop now,  i = settings,  h = CSV header,  w = watchdog test
*/
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include "Reading.h"

// Pins (same as Round 2, plus the pump relay)
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
#define PUMP_RELAY 32
#define BUTTON_PIN 0

// Settings
#define RELAY_ACTIVE_LOW 0             // 0 = relays turn ON with HIGH (3.3 V), checked on the rig 25-11-2026
const float TDS_LIMIT = 280;           // never bypass above this
const float RULE_LIMIT = 300;          // today's plain TDS rule, for comparison only
const float RISE_PPM = 25, RISE_PCT = 20;   // TDS rise that counts as a change
const float FALL_PPM = 25, FALL_PCT = 30;   // TDS fall that counts as a change
const float LEARN_TOL_PPM = 10, LEARN_TOL_PCT = 10; // learning samples must agree this well
const float CLOUDY_DROP_PCT = 3;       // clearness drop that counts as cloudy (no clear level saved)
const float LAB_TDS_FACTOR = 1;    // from NaCl standards, lab 23-11-2026
const float CLOUDY_NTU = 1.0;          // BIS acceptable turbidity
const int CAL_VERSION = 2;             // bump when lab defaults change
// Lab turbidity curve, 23-11-2026: clear % (vs clear level saved with c) -> NTU
const float CURVE_PCT[] = {99.0, 98.0, 91.0, 80.0, 61.0, 39.0, 22.0};
const float CURVE_NTU[] = { 0.0,  0.4,  3.2,  7.1, 16.5, 31.2, 45.0};
const int CURVE_N = 7;
const int LEARN_SAMPLES = 3;
const unsigned long CYCLE_MAX_MS = 60000;   // pump + valve never stay on longer
const unsigned long PUMP_RUN_MS = 10000;    // fixed pump time per cycle (10 s)
const unsigned long FIRST_FLOW_MS = 4000;   // pulses must start within this
const unsigned long FLOW_GAP_MS = 2500;     // flow stopped if no pulse for this long
const uint32_t WDT_MS = 4000;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
#include "Ui.h"              // TFT dashboard layout (160 x 128)
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
Preferences prefs;

volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseMs = 0;

// saved in flash
int source = 0;                 // 0 none, 1 corporation, 2 borewell, 3 tanker
bool locked = false;
float baseTds = 0, baseTurb = 0; int baseSrc = 0;
float clearMv = 0, pulsesPerL = 450, tdsFactor = LAB_TDS_FACTOR;
int doseMl = 300;

// this session only
float learnTds = 0, learnTurb = 0; int learnN = 0;
float litresRO = 0, litresBypass = 0;
unsigned long cycleCount = 0;
bool lastBtn = true, runNow = false;
char label[20] = "-";
char cmd[32];
byte cmdLen = 0;

const char* SRC_NAME[] = {"NONE", "CORP", "BORE", "TANKER"};

void IRAM_ATTR pulseCounter() { pulseCount = pulseCount + 1; lastPulseMs = millis(); }

void feed() { esp_task_wdt_reset(); }
void pause(unsigned long ms) {            // delay that keeps the watchdog fed
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { feed(); delay(10); }
}

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
void allOff() {                           // pump first, then valves
  relay(PUMP_RELAY, false);
  relay(RO_RELAY, false);
  relay(BYPASS_RELAY, false);
}

void saveAll() {
  prefs.putInt("src", source);
  prefs.putBool("lock", locked);
  prefs.putFloat("bTds", baseTds);
  prefs.putFloat("bTurb", baseTurb);
  prefs.putInt("bSrc", baseSrc);
  prefs.putFloat("clear", clearMv);
  prefs.putFloat("ppl", pulsesPerL);
  prefs.putFloat("tdsF", tdsFactor);
  prefs.putInt("dose", doseMl);
}
void loadAll() {
  source = prefs.getInt("src", 0);
  locked = prefs.getBool("lock", false);
  baseTds = prefs.getFloat("bTds", 0);
  baseTurb = prefs.getFloat("bTurb", 0);
  baseSrc = prefs.getInt("bSrc", 0);
  clearMv = prefs.getFloat("clear", 0);
  pulsesPerL = prefs.getFloat("ppl", 450);
  tdsFactor = prefs.getFloat("tdsF", LAB_TDS_FACTOR);
  if (prefs.getInt("calv", 0) < CAL_VERSION) {   // first start after the lab update
    tdsFactor = LAB_TDS_FACTOR;
    prefs.putFloat("tdsF", tdsFactor);
    prefs.putInt("calv", CAL_VERSION);
  }
  doseMl = prefs.getInt("dose", 300);
  if (source < 0 || source > 3) source = 0;
}

float readMv(uint8_t pin) {               // average of 30 samples
  long total = 0;
  for (int i = 0; i < 30; i++) {
    total += analogReadMilliVolts(pin);
    delay(10);
  }
  feed();
  return total / 30.0;
}

float tdsFromMv(float mv, float tempC) {
  float v = (mv / 1000.0) / (1.0 + 0.02 * (tempC - 25.0));
  return (133.42 * v * v * v - 255.86 * v * v + 857.39 * v) * 0.5;
}

// Estimated NTU from clear %, straight lines between the lab points. -1 = no clear level.
float clearPct(const Reading& r) {
  float ref = clearMv > 0 ? clearMv : baseTurb;
  return ref > 0 ? 100.0 * r.turbMv / ref : 100.0;
}
float ntuEst(const Reading& r) {
  if (clearMv <= 0) return -1;
  float pct = 100.0 * r.turbMv / clearMv;
  if (pct >= CURVE_PCT[0]) return 0;
  for (int i = 1; i < CURVE_N; i++)
    if (pct >= CURVE_PCT[i])
      return CURVE_NTU[i - 1] + (CURVE_PCT[i - 1] - pct) * (CURVE_NTU[i] - CURVE_NTU[i - 1]) / (CURVE_PCT[i - 1] - CURVE_PCT[i]);
  int a = CURVE_N - 2, b = CURVE_N - 1;    // below the last point: keep the last slope
  return CURVE_NTU[b] + (CURVE_PCT[b] - pct) * (CURVE_NTU[b] - CURVE_NTU[a]) / (CURVE_PCT[a] - CURVE_PCT[b]);
}

float changeLimit(float base, float ppm, float pct) {
  float p = base * pct / 100.0;
  return p > ppm ? p : ppm;
}

Reading measure() {
  Reading r;
  allOff();
  pause(500);
  tempSensor.requestTemperatures();
  r.tempC = tempSensor.getTempCByIndex(0);
  feed();
  float useTemp = (r.tempC > 1 && r.tempC < 60) ? r.tempC : 25.0;
  digitalWrite(TDS_POWER_PIN, HIGH);
  pause(1500);
  r.tdsMv = readMv(TDS_PIN);
  digitalWrite(TDS_POWER_PIN, LOW);
  r.tds = (tdsFromMv(r.tdsMv, useTemp) * tdsFactor/1.347)/1.029;
  pause(1000);                            // TDS board off before the optical reading
  r.turbMv = readMv(TURB_PIN);
  return r;
}

// Returns "SAFE" only if every check passes. Anything else means RO.
const char* decide(const Reading& r) {
  if (r.tempC < 1 || r.tempC > 60) return "FAULT:TEMP";
  if (r.tds < 10) return "FAULT:TDS DRY";
  if (r.tds > 2000 || r.tdsMv > 3000) return "FAULT:TDS RNG";
  if (r.turbMv < 300 || r.turbMv > 3050) return "FAULT:TURB";
  if (r.tds > TDS_LIMIT) return "HIGH TDS";
  if (source == 0) return "NO SOURCE TAG";
  if (source == 3) return "TANKER WATER";
  const float keep = 1.0 - CLOUDY_DROP_PCT / 100.0;
  if (clearMv > 0 && ntuEst(r) > CLOUDY_NTU) return "CLOUDY WATER";

  if (!locked) {                          // learning: water still goes to RO
    bool agrees = learnN > 0 &&
      fabs(r.tds - learnTds) <= changeLimit(learnTds, LEARN_TOL_PPM, LEARN_TOL_PCT) &&
      r.turbMv >= learnTurb * keep && r.turbMv <= learnTurb / keep;
    if (!agrees) { learnTds = r.tds; learnTurb = r.turbMv; learnN = 1; }
    else {
      learnTds = (learnTds * learnN + r.tds) / (learnN + 1);
      learnTurb = (learnTurb * learnN + r.turbMv) / (learnN + 1);
      learnN++;
    }
    if (learnN >= LEARN_SAMPLES) {
      baseTds = learnTds; baseTurb = learnTurb; baseSrc = source; locked = true;
      saveAll();
      Serial.printf("EVT,LEARNED,%.1f,%.0f,%s\n", baseTds, baseTurb, SRC_NAME[baseSrc]);
      return "LEARNED";                  // this cycle still goes to RO
    }
    return "LEARNING";
  }
  if (source != baseSrc) return "SOURCE CHANGE";
  if (r.tds > baseTds + changeLimit(baseTds, RISE_PPM, RISE_PCT)) return "TDS ROSE";
  if (r.tds < baseTds - changeLimit(baseTds, FALL_PPM, FALL_PCT)) return "TDS FELL";
  if (r.turbMv < baseTurb * keep) return "CLOUDY WATER";
  return "SAFE";
}

void show(const Reading& r, bool bypass, const char* why, bool ruleBypass, float litres, const char* flowMsg,
          unsigned long pumpMs = 0) {
  UiData d;
  d.tds = r.tds; d.tempC = r.tempC; d.ntu = ntuEst(r); d.clearPct = clearPct(r);
  d.flowLpm = (litres > 0 && pumpMs > 500) ? litres * 60000.0 / pumpMs : -1;
  d.flowNote = flowMsg;
  static const char* SRC_SHOW[] = {"NONE", "CORP", "BORE", "TANK"};
  d.source = SRC_SHOW[source];
  d.sourceCol = source == 0 ? C_RED : (source == 3 || (locked && source != baseSrc)) ? C_YELLOW : C_CYAN;
  d.locked = locked; d.normalTds = baseTds; d.learnN = learnN; d.learnOf = LEARN_SAMPLES;
  d.bypass = bypass; d.why = why; d.cycle = cycleCount;
  bool tdsBad = r.tds > TDS_LIMIT || strncmp(why, "FAULT:TDS", 9) == 0;
  d.tdsCol = tdsBad ? C_RED : (strcmp(why, "TDS ROSE") == 0 || strcmp(why, "TDS FELL") == 0) ? C_YELLOW : C_GREEN;
  d.tempCol = strcmp(why, "FAULT:TEMP") == 0 ? C_RED : C_CYAN;
  bool cloudy = strcmp(why, "CLOUDY WATER") == 0 || strcmp(why, "FAULT:TURB") == 0 || (d.ntu > CLOUDY_NTU);
  d.turbCol = cloudy ? C_RED : C_GREEN;
  (void)ruleBypass;                        // TDS<300 rule stays in the DATA log and dashboard
  uiShow(d);
}

void message(const char* a, const char* b, const char* c = "") { uiMessage(a, b, c); }

bool stopAsked() {                        // BOOT press or x / n on serial
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == 'x' || ch == 'n') return true;
  }
  return digitalRead(BUTTON_PIN) == LOW;
}

// Opens one valve, runs the pump, stops at runMs / no flow / stop / 60 s limit.
// runMs = 0 means run until stopped (used by flow calibration).
const char* dispense(uint8_t valve, unsigned long runMs, unsigned long& pulsesOut, unsigned long& pumpMs) {
  noInterrupts(); pulseCount = 0; interrupts();
  relay(valve, true);                     // valve first
  Serial.printf("EVT,VALVE_OPEN,%s\n", valve == RO_RELAY ? "RO" : "BYPASS");
  pause(300);
  relay(PUMP_RELAY, true);
  Serial.println("EVT,PUMP_ON");
  unsigned long t0 = millis(), lastLive = 0;
  const char* end = "DONE";
  while (true) {
    feed();
    unsigned long now = millis();
    noInterrupts(); unsigned long p = pulseCount, lp = lastPulseMs; interrupts();
    if (runMs && now - t0 >= runMs) { end = "DONE"; break; }
    if (now - t0 > CYCLE_MAX_MS) { end = "TIME LIMIT"; break; }
    if (p < 5 && now - t0 > FIRST_FLOW_MS) { end = "NO FLOW"; break; }
    if (p >= 5 && now - lp > FLOW_GAP_MS) { end = "FLOW STOPPED"; break; }
    if (now - t0 > 400 && stopAsked()) { end = "STOPPED"; break; }
    if (now - lastLive > 500) {
      lastLive = now;
      Serial.printf("LIVE,%lu,%.3f\n", p, p / pulsesPerL);
      if (uiFrame && now - t0 > 1000) uiFlow(p / pulsesPerL * 60000.0 / (now - t0), "", C_GREEN);
    }
    delay(5);
  }
  pumpMs = millis() - t0;
  relay(PUMP_RELAY, false);               // pump off first
  Serial.println("EVT,PUMP_OFF");
  pause(400);
  relay(valve, false);
  Serial.printf("EVT,VALVE_CLOSED,%s\n", end);
  while (digitalRead(BUTTON_PIN) == LOW) pause(10);   // same press must not start a new cycle
  pause(300);
  lastBtn = true;
  noInterrupts(); pulsesOut = pulseCount; interrupts();
  return end;
}

void logLine(const char* mode, const Reading& r, const char* route, const char* why, bool ruleBypass,
             float litres, unsigned long pulses, unsigned long pumpMs, const char* flowEnd) {
  float pct = clearPct(r);
  Serial.printf("DATA,%lu,%lu,%s,%s,%s,%.2f,%.1f,%.0f,%.0f,%.1f,%.1f,%s,%s,%s,%.3f,%lu,%lu,%s,%.3f,%.3f,%.2f\n",
                cycleCount, (unsigned long)millis(), mode, label, SRC_NAME[source], r.tempC, r.tds, r.tdsMv,
                r.turbMv, pct, locked ? baseTds : -1.0, route, why, ruleBypass ? "BYPASS" : "RO",
                litres, pulses, pumpMs, flowEnd, litresRO, litresBypass, ntuEst(r));
}

void printHeader() {
  Serial.println("DATA,cycle,ms,mode,label,source,temp_c,tds_ppm,tds_mv,turb_mv,clear_pct,normal_tds,"
                 "route,reason,tds_rule,litres,pulses,pump_ms,flow_end,litres_ro_total,litres_bypass_total,ntu_est");
}

void runCycle(bool moveWater) {
  cycleCount++;
  Serial.printf("EVT,MEASURE,%lu,%s\n", cycleCount, moveWater ? "CYCLE" : "READ");
  if (uiFrame) drawStatus("", C_WHITE, moveWater ? "MEASURING..." : "READING...", C_YELLOW, cycleCount);
  else message("Measuring", moveWater ? "" : "(no water)");
  Reading r = measure();
  const char* why = decide(r);
  bool bypass = strcmp(why, "SAFE") == 0;
  bool ruleBypass = r.tds < RULE_LIMIT;
  Serial.printf("[%lu] %s | Temp %.2f C | TDS %.0f ppm | Clear %.0f mV | %s -> %s (%s) | TDS rule: %s\n",
                cycleCount, label, r.tempC, r.tds, r.turbMv, SRC_NAME[source],
                bypass ? "BYPASS" : "RO", why, ruleBypass ? "BYPASS" : "RO");
  show(r, bypass, why, ruleBypass, 0, moveWater ? "" : "READ ONLY");

  if (!moveWater) { logLine("READ", r, bypass ? "BYPASS" : "RO", why, ruleBypass, 0, 0, 0, "-"); return; }

  if (strcmp(why, "FAULT:TDS DRY") == 0) {   // jar may be empty: never run the pump dry
    Serial.println("EVT,PUMP_BLOCKED,TDS DRY");
    show(r, false, why, ruleBypass, 0, "BLOCKED");
    logLine("CYCLE", r, "RO", why, ruleBypass, 0, 0, 0, "PUMP BLOCKED");
    return;
  }
  pause(2500);                               // time to read the screen before water moves
  unsigned long pulses = 0, pumpMs = 0;
  const char* end = dispense(bypass ? BYPASS_RELAY : RO_RELAY, PUMP_RUN_MS, pulses, pumpMs);
  float litres = pulses / pulsesPerL;
  if (bypass) litresBypass += litres; else litresRO += litres;
  const char* flowMsg = strcmp(end, "NO FLOW") == 0 ? "NO FLOW" : strcmp(end, "FLOW STOPPED") == 0 ? "STOPPED"
                      : strcmp(end, "TIME LIMIT") == 0 ? "60s LIMIT" : "";
  show(r, bypass, why, ruleBypass, litres, flowMsg, pumpMs);
  logLine("CYCLE", r, bypass ? "BYPASS" : "RO", why, ruleBypass, litres, pulses, pumpMs, end);
}

unsigned long calPulses = 0;

void info() {
  Serial.printf("INFO,source=%s,normal=%s,normal_tds=%.1f,normal_turb=%.0f,normal_source=%s,learn=%d/%d\n",
                SRC_NAME[source], locked ? "LOCKED" : "LEARNING", baseTds, baseTurb, SRC_NAME[baseSrc],
                learnN, LEARN_SAMPLES);
  Serial.printf("INFO,clear_mv=%.0f,pulses_per_litre=%.1f,tds_factor=%.3f,dose_ml=%d,label=%s\n",
                clearMv, pulsesPerL, tdsFactor, doseMl, label);
  Serial.printf("INFO,limits,tds_max=%.0f,rise=max(%.0fppm,%.0f%%),fall=max(%.0fppm,%.0f%%),cloudy=%s\n",
                TDS_LIMIT, RISE_PPM, RISE_PCT, FALL_PPM, FALL_PCT, clearMv > 0 ? ">1.0NTU(lab curve)" : ">3%drop(no clear level)");
}

void command(char* c) {
  float val = atof(c + 1);
  switch (c[0]) {
    case 'n': runNow = true; break;
    case 'q': runCycle(false); break;
    case 's':
      if (c[1] >= '0' && c[1] <= '3') {
        source = c[1] - '0'; saveAll();
        Serial.printf("EVT,SOURCE,%s\n", SRC_NAME[source]);
        message("Source", SRC_NAME[source]);
      } else Serial.println("Use s1 corporation, s2 borewell, s3 tanker, s0 none");
      break;
    case 'c': {
      allOff();
      pause(1000);
      float mv = readMv(TURB_PIN);
      if (mv < 300 || mv > 3050) Serial.printf("EVT,CLEAR_REJECTED,%.0f\n", mv);
      else { clearMv = mv; saveAll(); Serial.printf("EVT,CLEAR_SAVED,%.0f\n", mv); }
      break;
    }
    case 'b':
      locked = false; learnN = 0; saveAll();
      Serial.println("EVT,RELEARN");
      message("Learning", "normal water");
      break;
    case 'l':
      strncpy(label, c[1] ? c + 1 : "-", sizeof(label) - 1);
      label[sizeof(label) - 1] = 0;
      for (char* p = label; *p; p++) if (*p == ',') *p = ';';
      Serial.printf("EVT,LABEL,%s\n", label);
      break;
    case 'd':
      Serial.println("d is not used: pump runs a fixed 10 s per cycle (PUMP_RUN_MS)");
      break;
    case 'k': {
      Serial.println("EVT,FLOW_CAL_START  (bypass jar; press BOOT or type x to stop)");
      message("Flow cal", "BOOT=stop");
      unsigned long pumpMs = 0;
      dispense(BYPASS_RELAY, 0, calPulses, pumpMs);
      Serial.printf("EVT,FLOW_CAL_PULSES,%lu  -> measure the water, then type v<ml>\n", calPulses);
      break;
    }
    case 'v':
      if (calPulses > 50 && val > 20) {
        pulsesPerL = calPulses * 1000.0 / val; saveAll();
        Serial.printf("EVT,FLOW_CAL_SAVED,%.1f pulses per litre\n", pulsesPerL);
      } else Serial.println("Run k first (more than 50 pulses), then v<ml>");
      break;
    case 'f':
      if (c[1] == 0) { tdsFactor = LAB_TDS_FACTOR; saveAll(); Serial.printf("EVT,TDS_FACTOR,%.3f (lab default)\n", tdsFactor); break; }
      if (val > 10 && val < 2000) {
        Reading r = measure();
        float raw = r.tds / tdsFactor;
        if (raw > 10) {
          tdsFactor = val / raw; saveAll();
          Serial.printf("EVT,TDS_FACTOR,%.3f (raw %.1f ppm, reference %.0f ppm)\n", tdsFactor, raw, val);
        } else Serial.println("TDS too low to calibrate: is the probe in water?");
      }
      break;
    case 'x':
      allOff();
      Serial.println("EVT,STOP");
      message("STOP", "all off");
      break;
    case 'r':
      litresRO = litresBypass = 0; cycleCount = 0;
      Serial.println("EVT,TOTALS_RESET");
      break;
    case 'i': info(); break;
    case 'h': printHeader(); break;
    case 'w': {                             // live fail-safe test: controller hangs while pumping to RO
      Serial.println("EVT,WATCHDOG_TEST  (pump and RO valve on, then the program freezes)");
      message("WATCHDOG", "TEST");
      relay(RO_RELAY, true);
      pause(300);
      relay(PUMP_RELAY, true);
      while (true) { delay(100); }          // no feed: watchdog restarts the ESP32
    }
    default:
      Serial.println("Commands: n q s1-3 c b l<name> d<ml> k v<ml> f<ppm> x r i h w");
  }
}

void setup() {
  relaySetup(PUMP_RELAY);                 // first: pump off, both valves closed
  relaySetup(RO_RELAY);
  relaySetup(BYPASS_RELAY);
  pinMode(TDS_POWER_PIN, OUTPUT);
  digitalWrite(TDS_POWER_PIN, LOW);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  esp_task_wdt_config_t wdt = { .timeout_ms = WDT_MS, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(NULL);

  Serial.begin(115200);
  pause(500);
  esp_reset_reason_t rr = esp_reset_reason();
  bool byWdt = rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT || rr == ESP_RST_PANIC;
  Serial.println("\n=== SOURCESENSE R3 ===");
  Serial.printf("EVT,BOOT,%s\n", byWdt ? "WATCHDOG" : rr == ESP_RST_BROWNOUT ? "BROWNOUT" : "NORMAL");

  prefs.begin("ss3", false);
  loadAll();

  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(TURB_PIN, ADC_11db);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tempSensor.begin();
  Serial.printf("EVT,DS18B20,%d\n", tempSensor.getDeviceCount());

  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), pulseCounter, FALLING);

  info();
  printHeader();
  if (byWdt) {
    message("Restarted", "by watchdog", "pump + valves are OFF");
  } else if (source == 0) message("Type s1/s2", "then BOOT");
  else message(locked ? "Ready" : "Learning", "Press BOOT");
}

void loop() {
  feed();
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (cmdLen > 0) { cmd[cmdLen] = 0; command(cmd); cmdLen = 0; }
    } else if (cmdLen < sizeof(cmd) - 1) cmd[cmdLen++] = ch;
  }
  bool btn = digitalRead(BUTTON_PIN);
  if (lastBtn && !btn) runNow = true;
  lastBtn = btn;
  if (runNow) { runNow = false; runCycle(true); }
  delay(10);
}
