"""Build 5_Reported_data/ from the analysis workbook + the real SD header/footer lines.
Run from the repo root:  python3 5_Reported_data/build_reported_data.py
"""
import csv, hashlib, sys, datetime
from pathlib import Path
import openpyxl

XLSX = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("5_Reported_data/thrust_and_hybrid_runs_filled.xlsx")
SD   = Path("4_Calibration_and_final_runs/Reported runs")
OUT  = Path("5_Reported_data")
COLS = "ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh".split(",")
FMT  = dict(ms="{:.0f}", throttle_us="{:.0f}", net_counts="{:.0f}", power_W="{:.2f}", solar_W="{:.2f}",
            batt_W="{:.2f}", thrust_g="{:.1f}")           # everything else 3 dp
RUNS = {"B1":("Batt_data",67),"B2":("Batt_data",68),"B3":("Batt_data",69),"B4":("Batt_data",70),"B5":("Batt_data",71),
        "H1":("Hybrid_data",75),"H2":("Hybrid_data",76),"H3":("Hybrid_data",77),"H4":("Hybrid_data",78),"H5":("Hybrid_data",79)}

wb = openpyxl.load_workbook(XLSX, data_only=True)
# per-run rows
rows = {}
for sheet in ("Batt_data","Hybrid_data"):
    ws = wb[sheet]; hdr = [c.value for c in ws[1]][:16]
    assert hdr[0]=="run" and hdr[1:16]==COLS, hdr
    for r in ws.iter_rows(min_row=2, values_only=True):
        if r[0] and r[1] is not None: rows.setdefault(r[0], []).append(r[1:16])
# run log (Setup sheet)
log = {}
for r in wb["Setup"].iter_rows(values_only=True):
    if r[0] in RUNS: log[r[0]] = r

def sd_lines(n):
    L = (SD/f"RUN{n}.CSV").read_text().splitlines()
    head = [l for l in L if l.startswith("#") and "Wh:" not in l and "STOP" not in l]
    foot = [l for l in L if l.startswith("# STOP") or "Wh:" in l]
    return head, foot

def angle(v):
    # Excel turned some 'a/b/c' entries into dates (d/m/yy). Undo that and flag it.
    if isinstance(v, datetime.datetime): return f"{v.day}/{v.month}/{v.year%100}", "reconstructed from a date-formatted cell, confirm"
    return ("" if v in (None,"n/a") else v), ""

manifest = []
for lab,(sheet,n) in RUNS.items():
    data = rows[lab]; head, foot = sd_lines(n)
    text = [f"# run_label={lab} sd_run=RUN{n} date=2026-10-30"] + head[:1] + [
        "# data rows exported from thrust_and_hybrid_runs_filled.xlsx, sheet %s (serial-capture rows, '#' lines removed); header and footer lines are from the SD card file" % sheet,
        ] + head[1:] + [",".join(COLS)]
    for r in data:
        text.append(",".join(FMT.get(c,"{:.3f}").format(v) for c,v in zip(COLS,r)))
    text += [l for l in foot if l.startswith("# counts") or l.startswith("# STOP")] 
    sdfoot = {l.split(":")[0][2:]:float(l.split(":")[1]) for l in foot if "Wh:" in l}
    text += [f"# {k}: {v:.3f}" for k,v in sdfoot.items()]
    (OUT/f"{lab}_RUN{n}.csv").write_text("\n".join(text)+"\n")
    last = dict(zip(COLS, data[-1]))
    s = log[lab]
    manifest.append(dict(label=lab, sd_run=f"RUN{n}", file=f"{lab}_RUN{n}.csv",
        condition="battery-only (panels covered)" if lab[0]=="B" else "hybrid (panels uncovered, Victron 16.70 V)",
        date="2026-10-30", time_local="", data_rows=len(data), ms_first=int(data[0][0]), ms_last=int(data[-1][0]),
        pack_V_before=s[4], pack_V_after=s[5], cell1=s[6], cell2=s[7], cell3=s[8], cell4=s[9], start_bus_V_log=s[10], panel_angle_P1_P2_P3_deg=angle(s[11])[0], panel_angle_check=angle(s[11])[1], victron_max_charge_A=s[12] if s[12]!='n/a' else '',
        esc_Wh_rows=round(last["energy_Wh"],3), solar_Wh_rows=round(last["solar_Wh"],3), batt_Wh_rows=round(last["batt_Wh"],3),
        esc_Wh_sd=sdfoot["energy_Wh"], solar_Wh_sd=sdfoot["solar_Wh"], batt_Wh_sd=sdfoot["batt_Wh"],
        footer_check="match" if all(abs(a-b)<=0.0011 for a,b in [(last["energy_Wh"],sdfoot["energy_Wh"]),(last["solar_Wh"],sdfoot["solar_Wh"]),(last["batt_Wh"],sdfoot["batt_Wh"])]) else "MISMATCH",
        sha256=hashlib.sha256((OUT/f"{lab}_RUN{n}.csv").read_bytes()).hexdigest()[:16],
        notes=s[13]))
with open(OUT/"manifest.csv","w",newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(manifest[0])); w.writeheader(); w.writerows(manifest)
for m in manifest: print(m["label"], m["data_rows"], m["footer_check"], m["esc_Wh_rows"], m["esc_Wh_sd"])
