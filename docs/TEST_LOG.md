# Test log

Recorded on 4 November 2026 on the bench rig, in the same session as the demo video. Values are read from the TFT and the dashboard. Each row is one run. ORP is typed by hand in every run.

| Video time | Sample | TDS (ppm) | ORP typed (mV) | Clearness (%) | Temp (C) | Route | Reason |
|---|---|---|---|---|---|---|---|
| 0:18 | Turbid sample | 509 | 680 | 94.3 | 27.8 | RO | HIGH TDS |
| 0:42 | Fresh water, first run | 61 | 680 | 99.5 | 27.8 | RO | LEARNING |
| 1:02 | Normal water after learning | 95 | 680 | 100.2 | 28.0 | BYPASS | SAFE |
| 1:05 | After adding salt | 142 | 680 | 99.7 | 27.9 | RO | SOURCE CHANGE |
| 1:12 | ORP typed as low | 159 | 380 | 100.4 | 27.9 | RO | NO CHLORINE |
| 1:23 | Temperature probe failed | 154 | 680 | 99.4 | -127 | RO | FAULT:TEMP |

A separate photo from the same day shows 480 ppm, clearness 100.2%, RO, HIGH TDS.

## What these runs show

- A rise from 95 to 142 ppm is about +49%. A TDS > 300 rule would have bypassed it. SourceSense sent it to RO.
- A dead temperature probe forced RO instead of bypass.

## Known issues

- Each case has one recorded run. Ten repeats per case are still to be done.
- Clearness below the limit has only been recorded together with high TDS. A cloudy sample with low TDS needs its own run.
- The flow totals are not calibrated. One RO total in the video reads about 38 L, which cannot be real on this rig, so we suspect noise pulses on the flow input. To be checked with a measuring jar.
- The TDS reading has not been checked against a calibrated TDS pen.
