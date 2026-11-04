# Changes since the abstract

| In the abstract | On the rig now | Reason |
|---|---|---|
| ORP electrode | ORP typed by hand, marked MAN | Kit cost too much this round and the department unit was not available |
| 860 nm and 275 nm optics in a printed flow cell | Analog turbidity sensor in the tank jar, 275 nm moved to Round 3 | Cost and time. Gives relative clearness, not calibrated NTU |
| Gradient boosted model | Fixed rules on the ESP32 | No real dataset yet |
| ADS1115, B0505S DC-DC supplies | ESP32 ADC, TDS board powered only while reading | Cost, and it works for this stage |
| 12 V pump | Submersible mini pump on 5 V with a hand switch | Part available to us |
| Valves normally open to RO | Normally closed on both paths | Bench rig dispenses nothing if power fails |
| Skip limit 300 ppm | 280 ppm | Margin for probe error |
| YF-S401 flow sensor | YF-S201 | Part available to us |

## Delay

The abstract planned about 5 weeks to a working prototype. Round 2 gave about 9 days after shortlisting on 26 October. Real samples, the blind test and model training move to Round 3.

## Revised claim for this round

On our bench rig, water goes to RO when TDS rises sharply against this home's learned water, when TDS is above 280 ppm, or when a sensor fails. The abstract's targets for chlorine detection (95% against DPD) and turbidity (10% or 0.3 NTU against kaolin) are not met yet and move to Round 3.
