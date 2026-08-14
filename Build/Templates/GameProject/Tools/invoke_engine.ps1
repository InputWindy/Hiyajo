# Requires -Version 5.0
param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("package")]
	[string] $Action,

	[Parameter(Mandatory = $true)]
	[string] $CProject,

	[Parameter(ValueFromRemainingArguments = $true)]
	[string[]] $PassThrough
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $CProject)) {
	Write-Error "cproject not found: $CProject"
	exit 1
}

$jsonText = Get-Content -LiteralPath $CProject -Raw -Encoding UTF8
$data = $jsonText | ConvertFrom-Json
$engineRaw = [string]$data.EngineDirectory
if ([string]::IsNullOrWhiteSpace($engineRaw)) {
	Write-Error "EngineDirectory missing in $CProject"
	exit 1
}

$projectDir = Split-Path -Parent $CProject
if ([System.IO.Path]::IsPathRooted($engineRaw)) {
	$engine = [System.IO.Path]::GetFullPath($engineRaw)
} else {
	$engine = [System.IO.Path]::GetFullPath((Join-Path $projectDir $engineRaw))
}

# Installer layout: Tools\python\python.exe ; venv: Tools\python\Scripts\python.exe
$localPy = Join-Path $engine "Tools\python\python.exe"
if (-not (Test-Path -LiteralPath $localPy)) {
	$localPy = Join-Path $engine "Tools\python\Scripts\python.exe"
}
if (-not (Test-Path -LiteralPath $localPy)) {
	Write-Error "Engine local Python missing under Tools\python (or Scripts).`nRun setup.bat in the Maho engine root first."
	exit 1
}

if ($Action -eq "package") {
	# GUI via WScript + pythonw — no Python console attached to this process.
	$vbs = Join-Path $engine "Tools\launch_package.vbs"
	if (-not (Test-Path -LiteralPath $vbs)) {
		Write-Error "Missing package launcher: $vbs"
		exit 1
	}
	$wscript = Join-Path $env:SystemRoot "System32\wscript.exe"
	$p = Start-Process -FilePath $wscript -ArgumentList @("//nologo", $vbs, $CProject) -PassThru -Wait
	exit $p.ExitCode
}

