# sweep_targets.ps1 - DATA COLLECTION ONLY (no control-parameter changes).
# Steps the closed-loop TARGET through a list by dropping t<height> commands into cmd.txt,
# which the running serial_logger forwards to the board. The always-on logger captures
# everything; analyze afterwards with analyze_log.ps1 -Segments.
# Sends ONLY 't<height>' (safe bounded closed-loop setpoints). Touches NO PID/feedforward gains.
param(
  [string]$Targets = "10,12,14,16,18,20",  # cm, comma separated
  [int]$DwellS = 20,                        # seconds to hold each target
  [double]$RestoreTo = 15.0,                # final safe hold target
  [string]$CmdFile = ""
)
if ($CmdFile -eq "") { $CmdFile = Join-Path (Join-Path $PSScriptRoot "logs") "cmd.txt" }
function Q($line){ Set-Content -Path $CmdFile -Value $line -Encoding ASCII }

$list = $Targets.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
foreach($t in $list){
  Q ("t" + $t)
  Write-Output ("[{0}] target -> {1}cm, hold {2}s" -f (Get-Date -Format HH:mm:ss), $t, $DwellS)
  Start-Sleep -Seconds $DwellS
}
Q ("t" + $RestoreTo)
Write-Output ("done; restored target -> {0}cm" -f $RestoreTo)
