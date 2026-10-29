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
- B1: the capture starts after the run's `# logging to` line, so its SD run number is inferred. The next two boots in the capture are RUN41 and RUN42, so B1 is taken to be RUN40.
- Hybrid runs: both captures start part-way through the idle readout, so their run numbers are not recorded.
- Firmware: no commit matches the firmware that produced these runs (7-step sweep with `COUNTS_PER_G = 0.0`). Tag the commit once that is restored and record it in `runs.csv`.
- Still to fill in `runs.csv` for each run: `time_local`, and for hybrid runs `sky` and `panel_tilt`.
