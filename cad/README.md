# SOLIDWORKS model

SOLIDWORKS 2026, units mm. 21 parts, a 26 component assembly with 0 rebuild errors, an exploded assembly, an A3 drawing with BOM and jar lid hole table, STEP and STL exports.

View it in a browser: https://sanmugavel1.github.io/SourceSense-atomQUEST2026/

| Path | Contents |
|---|---|
| `assemblies/SS_10_Assembly.SLDASM` | Main assembly |
| `assemblies/SS_11_Assembly_Exploded.SLDASM` | Exploded layout |
| `drawings/SS_10_Assembly.pdf` | A3 drawing, 2 sheets |
| `SS_10_Assembly.step` | Neutral export |
| `stl/` | One STL per part. GitHub shows these in 3D when you open them |
| `renders/` | Still images |

## This is the planned design, not the built rig

The model is from 29 October. It was used to check the layout and water path and to keep electronics raised away from water. Dimensions are first-pass values, not yet measured from our parts.

It still shows the ORP electrode, a 12 V pump, ADS1115, OLED and a 4 channel relay. The built rig has a turbidity sensor, a hand-switched submersible pump, ESP32 ADC, a TFT and a relay module for two valves. Components are placed at fixed layout positions, not mated. Updating the model to measured parts is a Round 3 task.
