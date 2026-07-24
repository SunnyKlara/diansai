# sweep_pwm.ps1 - DATA ONLY (no code change). Find the lift-off PWM: drop ball to floor,
# then step manual PWM up in small increments; logger captures H vs PWM. Read with _peek.
# Sends only s / m<pwm> (stop / manual open-loop PWM). Caps at a safe max (won't reach tube top).
param(
  [string]$Pwms = "3000,3600,3700,3800,3850,3900,3950,4000,4050,4100,4150",
  [int]$FirstDwell = 8,   # first step longer to clear fan cold-start (~5s dead)
  [double]$DwellS = 2.5,
  [double]$RestoreTarget = 15.0,
  [string]$CmdFile = ""
)
if ($CmdFile -eq "") { $CmdFile = Join-Path (Join-Path $PSScriptRoot "logs") "cmd.txt" }
function Q($line){ Set-Content -Path $CmdFile -Value $line -Encoding ASCII }

Q "s"; Write-Output ("[{0}] s (drop to floor)" -f (Get-Date -Format HH:mm:ss)); Start-Sleep -Seconds 6
$list = $Pwms.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
$first = $true
foreach($p in $list){
  Q ("m" + $p)
  $d = if($first){ $FirstDwell } else { $DwellS }
  Write-Output ("[{0}] m{1}  (hold {2}s)" -f (Get-Date -Format HH:mm:ss),$p,$d)
  Start-Sleep -Seconds $d
  $first = $false
}
Q ("t" + $RestoreTarget)
Write-Output ("done; restored closed-loop t{0}" -f $RestoreTarget)
