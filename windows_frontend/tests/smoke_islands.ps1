param([Parameter(Mandatory = $true)][string]$Executable)

# Run against the bare build output, before staging the analysis runtimes.
# An interactive Windows desktop is required; inspect only our own process.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$appProcess = Start-Process -FilePath (Resolve-Path $Executable).Path -PassThru
try {
    $names = @()
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        $appProcess.Refresh()
        if ($appProcess.HasExited) { throw "Host exited early: $($appProcess.ExitCode). Inspect logs/host.log." }
        if ($appProcess.MainWindowHandle -eq 0) { continue }
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($appProcess.MainWindowHandle)
        $elements = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition)
        $names = @($elements | ForEach-Object { $_.Current.Name })
        if (($names -join "`n") -match 'Missing runtime file:') { break }
    }
    if (($names -join "`n") -notmatch 'Missing runtime file:') {
        throw "Expected XAML startup error was not rendered. Observed: $($names -join '; ')"
    }
    if (-not $appProcess.CloseMainWindow()) { throw 'Could not close native window' }
    if (-not $appProcess.WaitForExit(10000)) { throw 'Host shutdown timed out' }
    if ($appProcess.ExitCode -ne 0) { throw "Host exit code: $($appProcess.ExitCode)" }
    Write-Output 'PASS: XAML Island rendered its startup error and shut down cleanly.'
} finally {
    $appProcess.Refresh()
    if (-not $appProcess.HasExited) { Stop-Process -Id $appProcess.Id -Force }
}
