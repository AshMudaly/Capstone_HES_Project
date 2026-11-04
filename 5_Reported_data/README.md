# 5. Reported data

Per-run data for every run analysed in the report (30 October 2026). Use the label (B1, H1 …), not the run number: run numbers restart whenever the SD card is cleared.

**Start with [`manifest.csv`](manifest.csv).** One row per run: label, SD run number, condition, rows, pack and cell voltages before and after, energy totals and a checksum.

## Files

| Label | File | SD run | Condition | Report use |
|---|---|---|---|---|
| B1 | [B1_RUN67.csv](B1_RUN67.csv) | RUN67 | Battery only, panels covered | Baseline |
| B2 | [B2_RUN68.csv](B2_RUN68.csv) | RUN68 | Battery only, panels covered | Baseline |
| B3 | [B3_RUN69.csv](B3_RUN69.csv) | RUN69 | Battery only, panels covered | Baseline |
| B4 | [B4_RUN70.csv](B4_RUN70.csv) | RUN70 | Battery only, panels covered | Baseline |
| B5 | [B5_RUN71.csv](B5_RUN71.csv) | RUN71 | Battery only, panels covered | Baseline |
| H1 | [H1_RUN75.csv](H1_RUN75.csv) | RUN75 | Hybrid, Victron 16.70 V | Main hybrid result |
| H2 | [H2_RUN76.csv](H2_RUN76.csv) | RUN76 | Hybrid, Victron 16.70 V | Main hybrid result |
| H3 | [H3_RUN77.csv](H3_RUN77.csv) | RUN77 | Hybrid, Victron 16.70 V | Main hybrid result |
| H4 | [H4_RUN78.csv](H4_RUN78.csv) | RUN78 | Hybrid, Victron 16.70 V | Main hybrid result |
| H5 | [H5_RUN79.csv](H5_RUN79.csv) | RUN79 | Hybrid, Victron 16.70 V | Main hybrid result |

Analysis workbook: [thrust_and_hybrid_runs_filled.xlsx](thrust_and_hybrid_runs_filled.xlsx). Every figure and table in the results chapter comes from it.

## Where these rows came from

The final logger does not write data rows to the SD card, so the SD files in [`4_Calibration_and_final_runs/Reported runs`](../4_Calibration_and_final_runs/Reported%20runs) hold only header and footer lines. The data rows were captured from the Serial Monitor and pasted into the workbook (sheets `Batt_data` and `Hybrid_data`). These CSVs rebuild each run as the logger would have written it:

- the header and footer lines are copied from the SD card file;
- the data rows are exported from the workbook, with the `#` lines removed;
- `kcl_residual_A`, a derived column in the workbook, is not included.

[`build_reported_data.py`](build_reported_data.py) regenerates every file here. It also checks the last cumulative energy value of each run against the SD card footer. All ten runs agree to 0.001 Wh (H1 ESC energy reads 0.208 Wh in the rows and 0.209 Wh in the footer, a rounding difference).

```
python3 5_Reported_data/build_reported_data.py        # needs: pip install openpyxl
```

## Columns

`ms, throttle_us, bus_V, current_A, power_W, energy_Wh, net_counts, thrust_g, solar_A, solar_W, solar_Wh, batt_V, batt_A, batt_W, batt_Wh`

- `bus_V`, `current_A`, `power_W`, `energy_Wh`: ESC branch (INA260 0x40)
- `solar_*`: solar branch (INA260 0x41), positive when the Victron feeds the bus
- `batt_*`: battery branch (INA260 0x44), positive when the pack discharges
- `net_counts`, `thrust_g`: HX711 raw counts and thrust, with `COUNTS_PER_G` = 215.45 (sign flipped in the sketch so thrust reads positive)
- Rows with `throttle_us` = 1000 are arming rows. Steps run 1200 to 1700 µs for 5 s each.
- Load a file in pandas with `pd.read_csv(path, comment="#")`.

## Not in this folder

| Item | Status |
|---|---|
| H6 (RUN80), H7 (RUN81) | Hybrid runs kept outside the workbook. SD footers are in `3_Pilot_runs/Pilot run data/hybrid/`. |
| RUN72 | Excluded: Victron setpoint 16.60 V, below the pack voltage, so solar delivered nothing |
| RUN73 | Idle check, not a sweep |

## Known gaps

- Time of day for each run was not recorded in the workbook.
- Irradiance was not measured.
- Panel angles for H1, H3, H4 and H5 were stored by Excel as dates; they are reconstructed in the manifest and flagged `panel_angle_check`.
