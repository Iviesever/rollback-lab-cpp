$processes = @(Get-Process -Name rollback_lab -ErrorAction SilentlyContinue)
if ($processes.Count -ne 0) {
    $processes | Select-Object Id, ProcessName, StartTime, Path
    throw "$($processes.Count) rollback_lab process(es) remain."
}
Write-Output "No rollback_lab child processes remain."

