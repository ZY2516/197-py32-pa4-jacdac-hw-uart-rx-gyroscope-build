$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Parent = Split-Path -Parent $Root
$Source = Join-Path $Root "Src\py32_jacdac_runtime.c"
$Asm = Join-Path $Root "MDK-ARM\py32_jacdac_rx_fastpath.s"
$BaselineAsm = Join-Path $Parent "119-py32-qmi8656-jacdac-tim14-rx-asm-phase-edge-diagnostic-build\MDK-ARM\py32_jacdac_rx_fastpath.s"
$Failures = [System.Collections.Generic.List[string]]::new()
$Text = Get-Content -Raw -LiteralPath $Source

function Require-Text([string]$Pattern, [string]$Description) {
    if ($Text -notmatch $Pattern) {
        $Failures.Add("Missing: $Description")
    }
}

Require-Text 'APP_JACDAC_RX_TIMING_SLOTS\s*=\s*8u' 'reduced timing-only slots'
Require-Text 'APP_JACDAC_RX_PHASE_DIAG_EDGE_SLOTS\s*=\s*24u' '24 phase diagnostic edges'
Require-Text 'APP_JACDAC_WAIT_UNTIL\(start_tick\);' 'wait until predicted first start before edge capture'

$FunctionStart = $Text.IndexOf('static void APP_JacdacRxPhaseDiagnosticCapture')
$WaitIndex = $Text.IndexOf('APP_JACDAC_WAIT_UNTIL(start_tick);', $FunctionStart)
$InitialReadIndex = $Text.IndexOf('last_level = APP_JACDAC_BUS_IS_HIGH();', $FunctionStart)
if ($FunctionStart -lt 0 -or $WaitIndex -lt 0 -or $InitialReadIndex -lt 0 -or $WaitIndex -gt $InitialReadIndex) {
    $Failures.Add('Capture must wait for start_tick before reading the initial bus level')
}

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $Asm).Hash -ne
    (Get-FileHash -Algorithm SHA256 -LiteralPath $BaselineAsm).Hash) {
    $Failures.Add('ARMASM RX main path changed from 119')
}

if ($Failures.Count -ne 0) {
    Write-Host 'Phase window diagnostic verification FAILED:' -ForegroundColor Red
    $Failures | ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host 'Phase window diagnostic verification PASSED' -ForegroundColor Green
Write-Host '  Capture starts at predicted first start and stores 24 edges'
Write-Host '  ARMASM RX main path is unchanged from 119'
