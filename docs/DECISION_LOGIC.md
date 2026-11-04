# Decision logic and fail-safe

The firmware checks in this order and stops at the first failure. Only a run that gets through all of them reaches BYPASS. Limits are constants at the top of the sketch.

| Order | Check | Screen shows | Route |
|---|---|---|---|
| 1 | Temperature outside 1 to 60 C (a dead DS18B20 reads -127) | FAULT:TEMP | RO |
| 2 | TDS below 10 ppm (probe dry or unplugged) | FAULT:TDS DRY | RO |
| 3 | TDS above 2000 ppm or signal above 3000 mV | FAULT:TDS RNG | RO |
| 4 | Turbidity signal below 300 mV or above 3050 mV | FAULT:TURB | RO |
| 5 | TDS above 280 ppm | HIGH TDS | RO |
| 6 | No ORP value entered | NO ORP VALUE | RO |
| 7 | ORP below 550 mV | NO CHLORINE | RO |
| 8 | Clearness more than 3% below the saved clear level | CLOUDY WATER | RO |
| 9 | Still learning this water (needs two matching runs) | LEARNING | RO |
| 10 | Clearness more than 3% below learned water | CLOUDY WATER | RO |
| 11 | TDS over 25% above learned water, or ORP more than 150 mV below it | SOURCE CHANGE | RO |
| 12 | Everything passed | SAFE | BYPASS |

After a SAFE run the learned values follow the water slowly (90% old, 10% new), so seasonal drift does not trigger a false alarm, but a sudden change does.

## Why 280 ppm and not 300

The cheap TDS probe has error. Keeping a margin below the 300 ppm limit means probe error alone is less likely to let high TDS water through.

## Why the order matters

The turbid sample in our video read 509 ppm, so the screen gave HIGH TDS even though clearness had also dropped to 94.3%. Limits are checked before clearness, so the first failure is the one shown.

## Hardware fail-safe

- Both valves are normally closed. Power off means no flow to either output.
- The firmware closes the valve by itself after 60 seconds if nobody ends the cycle.
- The pump is on a hand switch and its own 5 V, so the ESP32 cannot stop it. With the valves shut it has no outlet. This is a limit of the present rig and moves to relay control in Round 3.

## Not built yet

The 45 day calibration check, the low confidence rule (no model in this build) and the UVC reed switch (no UVC LED in this build).
