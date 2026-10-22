# Solar-hybrid propulsion testbed firmware

Freenove Control Board V5 (Uno R4 WiFi layout, 5 V logic).

| Environment | What it is |
|---|---|
| `logger` | Phase 1 battery-only logger: INA260 + HX711 + SD, sweep over serial commands `g x t c<grams>` |
| `esc_setup` | ESC calibration and stick programming from the keyboard: `u` = 2000 us, `d` = 1000 us. Prop off |

```
pio run -e logger                 # build
pio run -e logger -t upload       # build and upload
pio device monitor                # Serial Monitor, 115200
```

Run logs (`RUNnn.CSV` from the SD card) go in `data/`.
