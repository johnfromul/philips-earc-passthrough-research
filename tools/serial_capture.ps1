param(
  [ValidatePattern('^COM[1-9][0-9]*$')]
  [string]$Port = 'COM5',
  [ValidateRange(1, 600)]
  [int]$Seconds = 120
)
# Receive-only UART capture. This script never writes to the serial port.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$captureRoot = Join-Path $repoRoot 'private\uart_captures'
$ts = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$dir = Join-Path $captureRoot "c9k-cp2102-$ts"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$bin = Join-Path $dir 'capture.bin'
$txt = Join-Path $dir 'capture.txt'

$p = New-Object System.IO.Ports.SerialPort($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$p.Handshake = 'None'
$p.ReadTimeout = 300
$fs = $null
$total = 0
try {
  $p.Open()
  $fs = [System.IO.File]::Open($bin,'Create','Write')
  Write-Output "RECORDING port=$Port dir=$dir"

  $buf = New-Object byte[] 8192
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline) {
    try {
      $n = $p.Read($buf,0,$buf.Length)
      if ($n -gt 0) { $fs.Write($buf,0,$n); $fs.Flush(); $total += $n }
    } catch [System.TimeoutException] { }
  }
} finally {
  if ($null -ne $fs) { $fs.Dispose() }
  if ($p.IsOpen) { $p.Close() }
  $p.Dispose()
}

# Printable-ASCII decode (keep CR/LF/TAB, replace other non-printables with '.')
$bytes = [System.IO.File]::ReadAllBytes($bin)
$sb = New-Object System.Text.StringBuilder
foreach ($b in $bytes) {
  if ($b -eq 9 -or $b -eq 10 -or $b -eq 13 -or ($b -ge 32 -and $b -le 126)) {
    [void]$sb.Append([char]$b)
  } else { [void]$sb.Append('.') }
}
[System.IO.File]::WriteAllText($txt, $sb.ToString())
Write-Output "DONE total=$total bin=$bin txt=$txt"
