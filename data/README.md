# Thesis run data

Raw serial-monitor captures of the sweep runs, one file per run, plus `runs.csv` (the manifest).
Firmware: env `logger_nosd` (SD row writes off, so the serial rows are the data record).

## Layout

| Folder | Contents |
|---|---|
| `battery-only/` | B1 to B5. Pack only, no solar contribution (MPPT left on the bus, about 0.02 A standby draw) |
| `hybrid/` | Pack and solar string. File names follow the labels used when the logs were first reviewed (`run2`, `run3`) |
| `excluded/` | Runs that do not count, with the reason in `runs.csv`. Kept for the record |

A run counts only if it ended with `# STOP: sequence complete`.

## Columns of each run file

`ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh`

`bus_V`, `current_A`, `power_W`, `energy_Wh` are the ESC branch. Solar current is positive when the MPPT feeds the bus. Battery current is positive when the pack discharges. `net_counts` is the load cell, which is not usable and is not analysed (`thrust_g` is NA). Lines starting with `#` are comments. Only rows after `# arming` are data.

## Provenance

- Each run file is a verbatim slice of a serial-monitor capture as it was pasted for review. Nothing in it was edited. `runs.csv` holds the SHA-256 of each run file and of the capture it came from. The original `log2file` captures on the logging laptop remain the master copies. Add them to `logs/` if they differ from these.
- Run numbers: each run's SD file on `DAta/` (`RUNnn.CSV`, header and footer only) carries the same energy totals as its serial capture. `sd_footer_check` in `runs.csv` records that comparison. It fixed B1 as RUN40 and the two hybrid captures as RUN37 and RUN38. The captures of B1 and the hybrid runs start part-way through the idle readout, so their own `# logging to` lines are missing.
- RUN39 completed as a hybrid run (SD footer: ESC 0.307 Wh, solar 0.074 Wh, pack 0.234 Wh), but its serial rows were not in the captures reviewed. Add its capture to `hybrid/`.
- RUN41, RUN44 and RUN47 are boots with no sweep (empty SD files).
- Firmware: no commit matches the firmware that produced these runs (7-step sweep with `COUNTS_PER_G = 0.0`). Tag the commit once that is restored and record it in `runs.csv`.
- Still to fill in `runs.csv` for each run: `time_local`, and for hybrid runs `sky` and `panel_tilt`.
