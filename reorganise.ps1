# Run from the repo root: powershell -ExecutionPolicy Bypass -File .\reorganise.ps1
# Moves with git mv (history kept). Nothing is deleted or committed.
$ErrorActionPreference = "Stop"
if (-not (Test-Path platformio.ini)) { throw "Run this from the repository root" }

# Bring back the old flag-bearing logger (deleted in the working tree) so it can be moved.
git checkout 1ae8672~1 -- Prototyping/logger/main.cpp
if ($LASTEXITCODE) { throw "restoring the old logger from 1ae8672~1 failed" }

# Check the files are what I think they are.
$flags = 'HX_TIMING|NO_SD_ROWS|NO_SERIAL_ROWS|HOLD_PROFILE'
if (Select-String -Path src\logger\main.cpp -Pattern $flags -Quiet) { throw "src\logger\main.cpp still has variant flags: not the cleaned logger" }
if (-not (Select-String -Path src\logger\main.cpp -Pattern '215\.45' -Quiet)) { throw "src\logger\main.cpp has no 215.45: not the final logger" }
if (-not (Select-String -Path Prototyping\logger\main.cpp -Pattern 'NO_SD_ROWS' -Quiet)) { throw "restored old logger has no variant flags: unexpected" }

$F = '4_Calibration_and_final_runs'
$moves = @(
  @('Prototyping/esc_setup',  '1_Prototyping/esc_setup'),
  @('Prototyping/hx711_diag', '1_Prototyping/hx711_diag'),
  @('Prototyping/hx711_bits', '2_Redevelopment/hx711_bits'),
  @('Prototyping/ina_diag',   '2_Redevelopment/ina_diag'),
  @('Prototyping/pwm_check',  '2_Redevelopment/pwm_check'),
  @('Prototyping/ina_scan',   '2_Redevelopment/ina_scan'),
  @('Prototyping/logger',     '2_Redevelopment/logger_variants'),
  @('Prototyping/Debugging Runs/Run Logs', '2_Redevelopment/Run Logs'),
  @('logs/device-monitor-261023-153735.log', '2_Redevelopment/logs/device-monitor-261023-153735.log'),
  @('Prototyping/Prototyping Data', '3_Pilot_runs/Pilot run data'),
  @('data/README.md', '3_Pilot_runs/manifest/README.md'),
  @('data/runs.csv',  '3_Pilot_runs/manifest/runs.csv'),
  @('logs/device-monitor-261030-114328.log', "$F/logs/device-monitor-261030-114328.log")
)
foreach ($s in '261029-142053','261029-142430','261029-142454','261029-144028','261029-151205') {
  $moves += ,@("logs/device-monitor-$s.log", "3_Pilot_runs/logs/device-monitor-$s.log")
}
foreach ($n in (@(67..71) + @(75..79))) {
  $moves += ,@("DAta/RUN$n.CSV", "$F/Reported runs/RUN$n.CSV")
}

# Preflight: every source must be tracked by git, or git mv will fail halfway.
foreach ($m in $moves) { if (-not (git ls-files $m[0])) { throw "Not tracked by git: $($m[0])" } }

foreach ($m in $moves) {
  New-Item -ItemType Directory -Force (Split-Path $m[1]) | Out-Null
  git mv $m[0] $m[1]
  if ($LASTEXITCODE) { throw "git mv failed: $($m[0])" }
}

# The two new (untracked) files: plain move, then stage.
New-Item -ItemType Directory -Force "$F\calibrate", "$F\logger" | Out-Null
Move-Item src\calibrate.cpp "$F\calibrate\calibrate.cpp"
Move-Item src\logger\main.cpp "$F\logger\main.cpp"
git add -A

"--- Files left in the old locations (should list nothing):"
Get-ChildItem Prototyping, src, data, DAta, logs -Recurse -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
"--- Status, with the RUN/log renames hidden:"
git status --short | Where-Object { $_ -notmatch 'RUN\d+\.CSV|\.log' }