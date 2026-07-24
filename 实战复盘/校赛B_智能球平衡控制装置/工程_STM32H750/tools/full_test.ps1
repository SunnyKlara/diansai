# full_test.ps1 - DATA COLLECTION ONLY (no parameter changes). Drives the complete exam
# sequence via cmd.txt (logger forwards). Only sends s/g/t<h> (stop/start/setpoint). Touches
# NO gains. Down-step (most likely to brownout) is placed LAST so earlier data survives a reset.
param([string]$CmdFile = "")
if ($CmdFile -eq "") { $CmdFile = Join-Path (Join-Path $PSScriptRoot "logs") "cmd.txt" }
function Q($line,$dwell){ Set-Content -Path $CmdFile -Value $line -Encoding ASCII; Write-Output ("[{0}] {1}  (hold {2}s)" -f (Get-Date -Format HH:mm:ss),$line,$dwell); Start-Sleep -Seconds $dwell }

Q "s"   5     # ensure stopped (ball at floor) for a clean startup measurement
Q "g"   22    # STARTUP: boost from floor -> default 15cm (measure time-to-stable)
Q "t10" 24    # 定高 10cm
Q "t15" 22    # 定高 15cm
Q "t20" 28    # 定高 20cm  (also dynamic 15->20 up-step)
Q "t10" 24    # dynamic 20->10 DOWN-step (last: brownout-risk)
Write-Output "full_test done (left at target 10)"
