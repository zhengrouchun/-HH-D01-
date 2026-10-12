# Keep this terminal open so input errors and build failures remain readable.
$deploymentArguments = @($args)
$ErrorActionPreference = 'Stop'
try {
    $sdkDirectory = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..\..\..')).Path
    $pythonExecutable = Join-Path $sdkDirectory '..\..\tools\python\python.exe'
    if (-not (Test-Path -LiteralPath $pythonExecutable)) {
        throw 'SDK Python runtime was not found.'
    }
    Set-Location -LiteralPath $sdkDirectory
    & $pythonExecutable (Join-Path $PSScriptRoot 'deployment_setup.py') @deploymentArguments
    if ($LASTEXITCODE -ne 0) {
        Write-Host 'Setup did not complete. Read the error above and setup_last_run.json.'
    }
} catch {
    Write-Host ('Setup error: ' + $_.Exception.Message)
}
Write-Host 'This terminal stays open. Type exit or close the window when finished.'
