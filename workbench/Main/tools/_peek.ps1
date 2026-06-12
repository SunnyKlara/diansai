# peek: downsampled H/P/R trace over a wall-clock window (read-only, shared access)
param([string]$From="10:25:06",[string]$To="10:25:36",[double]$EveryS=0.8)
$Csv = Join-Path (Join-Path $PSScriptRoot "logs") "live_session.csv"
$fs = New-Object System.IO.FileStream($Csv,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite)
$sr = New-Object System.IO.StreamReader($fs)
$hdr = $sr.ReadLine().Split(',')
$iW=[array]::IndexOf($hdr,'wall'); $iH=[array]::IndexOf($hdr,'H'); $iP=[array]::IndexOf($hdr,'P'); $iR=[array]::IndexOf($hdr,'R'); $iRS=[array]::IndexOf($hdr,'RS')
$iD=[array]::IndexOf($hdr,'D'); $iA=[array]::IndexOf($hdr,'A'); $iF=[array]::IndexOf($hdr,'F')
$last=-999.0
while($null -ne ($line=$sr.ReadLine())){
  $p=$line.Split(','); if($p.Length -lt $hdr.Length){continue}
  $w=$p[$iW]
  if($w -lt $From -or $w -gt $To){continue}
  # parse seconds-of-minute.fff from HH:mm:ss.fff for spacing
  $t=[double]($w.Substring(3,2))*60 + [double]$w.Substring(6)
  if($t-$last -lt $EveryS){continue}
  $last=$t
  Write-Output ("{0}  H={1,5}  RS={2,5}  R={3,5}  D={4,6}  A={5,4}  F={6,5}  P={7,5}" -f $w,$p[$iH],$p[$iRS],$p[$iR],$p[$iD],$p[$iA],$p[$iF],$p[$iP])
}
$sr.Close(); $fs.Close()
