/*
  Relay test: ESP32 GPIO26 = RO relay IN, GPIO25 = BYPASS relay IN. Serial Monitor 115200.
  Disconnect nothing. Watch the relay LEDs and listen for clicks, 4 seconds per step.
  HIGH is never driven here: a pin is either pulled LOW or released (open drain).
*/
#include <driver/gpio.h>
#define RO_RELAY 26
#define BYPASS_RELAY 25

void setPins(bool roLow, bool bypassLow, const char* text) {
  digitalWrite(RO_RELAY, roLow ? LOW : HIGH);
  digitalWrite(BYPASS_RELAY, bypassLow ? LOW : HIGH);
  Serial.println(text);
  delay(4000);
}

void setup() {
  gpio_set_level((gpio_num_t)RO_RELAY, 1);
  gpio_set_level((gpio_num_t)BYPASS_RELAY, 1);
  pinMode(RO_RELAY, OUTPUT_OPEN_DRAIN);
  pinMode(BYPASS_RELAY, OUTPUT_OPEN_DRAIN);
  digitalWrite(RO_RELAY, HIGH);
  digitalWrite(BYPASS_RELAY, HIGH);
  Serial.begin(115200);
}

void loop() {
  setPins(false, false, "STEP 1: both released -> both relays should be OFF");
  setPins(true,  false, "STEP 2: GPIO26 LOW    -> only RO relay should be ON");
  setPins(false, true,  "STEP 3: GPIO25 LOW    -> only BYPASS relay should be ON");
}
