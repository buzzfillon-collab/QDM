param(
  [Parameter(Mandatory = $true)]
  [string]$Executable
)

$exe = (Resolve-Path $Executable).Path
$start = New-Object System.Diagnostics.ProcessStartInfo
$start.FileName = $exe
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardInput = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true

$process = [System.Diagnostics.Process]::Start($start)
try {
  $process.StandardInput.WriteLine('{"command":"ping"}')
  $line = $process.StandardOutput.ReadLine()
  if ([string]::IsNullOrWhiteSpace($line)) {
    $stderr = $process.StandardError.ReadToEnd()
    throw "Torrent helper produced no handshake response. $stderr"
  }

  $response = $line | ConvertFrom-Json
  if (-not $response.ok -or $response.engine -ne "libtorrent") {
    throw "Unexpected torrent helper handshake: $line"
  }

  $process.StandardInput.WriteLine('{"command":"shutdown"}')
  $process.StandardInput.Close()
  if (-not $process.WaitForExit(10000)) {
    $process.Kill()
    throw "Torrent helper did not exit after shutdown."
  }
  if ($process.ExitCode -ne 0) {
    throw "Torrent helper exited with code $($process.ExitCode)."
  }
  Write-Host "libtorrent helper smoke test passed: $($response.version)"
}
finally {
  if (-not $process.HasExited) { $process.Kill() }
  $process.Dispose()
}
