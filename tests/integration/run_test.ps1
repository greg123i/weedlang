param(
    [string]$WeedFilePath,
    [int]$ExpectedExitCode
)

Write-Host "Running Integration Test: File='$WeedFilePath', ExpectedExitCode=$ExpectedExitCode"

if (-not $WeedFilePath) {
    Write-Error "Error: No .weed file path provided."
    exit 1
}

# Assume the compiler is already built and in the root
$compilerPath = "../../untitled2.exe"
if (!(Test-Path $compilerPath)) {
    Write-Error "Compiler '$compilerPath' not found. Please build it first."
    exit 1
}

# Run the compiler with the .weed file
Write-Host "Compiling $WeedFilePath..."
& $compilerPath $WeedFilePath
if ($LASTEXITCODE -ne 0) {
    Write-Error "Compiler failed for $WeedFilePath. Exit code: $LASTEXITCODE"
    exit 1
}

# Check if output.exe was generated
$outputExePath = "../../output.exe"
if (!(Test-Path $outputExePath)) {
    Write-Error "'$outputExePath' was not generated after compiling $WeedFilePath."
    exit 1
}

# Run the generated executable
Write-Host "Running $outputExePath..."
& $outputExePath
$ActualExitCode = $LASTEXITCODE

if ($ActualExitCode -eq $ExpectedExitCode) {
    Write-Host "SUCCESS: Result ($ActualExitCode) matches expected exit code ($ExpectedExitCode)."
    exit 0
} else {
    Write-Error "FAILURE: Expected $ExpectedExitCode, but got $ActualExitCode."
    exit 1
}
