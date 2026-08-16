# Purge a stale/broken per-user python.org Windows install of a given version.
# Used by Setup.bat before silent TargetDir install.
# Does NOT touch Miniconda / other distributions.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\purge_stale_python_install.ps1 -Version 3.12.8

param(
	[Parameter(Mandatory = $true)]
	[string] $Version
)

$ErrorActionPreference = 'Continue'
$displayName = "Python $Version (64-bit)"
$majorMinor = ($Version -split '\.')[0..1] -join '.'
Write-Host "[Maho] Purging stale per-user registration for $displayName (if any)..."

function Remove-TreeLiteral([string] $Path)
{
	if ([string]::IsNullOrWhiteSpace($Path)) { return }
	if (Test-Path -LiteralPath $Path)
	{
		Write-Host "[Maho]   remove: $Path"
		Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
	}
}

# 1) Per-user ARP / Burn bundle (this is what forces WixBundleInstalled=1 + Modify).
$uninstallRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
Get-ChildItem $uninstallRoot -ErrorAction SilentlyContinue | ForEach-Object {
	$props = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
	if ($null -eq $props) { return }
	if ($props.DisplayName -ne $displayName) { return }

	$guid = $_.PSChildName
	$cacheExe = $props.BundleCachePath
	Write-Host "[Maho]   found ARP $guid ($($props.DisplayName))"

	if ($cacheExe -and (Test-Path -LiteralPath $cacheExe))
	{
		Write-Host "[Maho]   quiet uninstall via Package Cache..."
		$p = Start-Process -FilePath $cacheExe -ArgumentList '/quiet','/uninstall' -Wait -PassThru -WindowStyle Hidden
		Write-Host "[Maho]   uninstall exit=$($p.ExitCode)"
	}

	# Burn sometimes exits 0 without removing a ghost entry (TargetDir already gone).
	Remove-Item -LiteralPath $_.PSPath -Recurse -Force -ErrorAction SilentlyContinue
	$cacheDir = Join-Path $env:LOCALAPPDATA "Package Cache\$guid"
	Remove-TreeLiteral $cacheDir
}

# 2) Dead HKCU PythonCore InstallPath (points at deleted engine tree, e.g. old Catty).
$coreKey = "HKCU:\Software\Python\PythonCore\$majorMinor"
$installPathKey = Join-Path $coreKey 'InstallPath'
if (Test-Path -LiteralPath $installPathKey)
{
	$exePath = (Get-ItemProperty -LiteralPath $installPathKey -ErrorAction SilentlyContinue).ExecutablePath
	if ($exePath)
	{
		$exePath = $exePath.Trim('"')
		if (-not (Test-Path -LiteralPath $exePath))
		{
			Write-Host "[Maho]   dead PythonCore $majorMinor ExecutablePath: $exePath"
			Remove-Item -LiteralPath $coreKey -Recurse -Force -ErrorAction SilentlyContinue
		}
	}
}

# 3) Orphan HKLM MSI features for this exact version (need elevation to uninstall).
#    When present without a usable python.exe, the Burn bundle for the same
#    version often plans "Modify" and never writes a new TargetDir.
$orphanHkLm = @()
foreach ($root in @(
	'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
	'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
))
{
	Get-ChildItem $root -ErrorAction SilentlyContinue | ForEach-Object {
		$props = Get-ItemProperty -LiteralPath $_.PSPath -ErrorAction SilentlyContinue
		if ($null -eq $props) { return }
		if ($props.DisplayName -like "Python $Version*")
		{
			$orphanHkLm += $props.DisplayName
		}
	}
}
$orphanHkLm = $orphanHkLm | Select-Object -Unique
if ($orphanHkLm.Count -gt 0)
{
	Write-Host "[Maho]   WARNING: machine-wide (HKLM) leftover packages for ${Version}:"
	foreach ($n in $orphanHkLm)
	{
		Write-Host "[Maho]     - $n"
	}
	Write-Host "[Maho]   These require Admin to uninstall (Settings -> Apps)."
	Write-Host "[Maho]   If silent TargetDir install still fails, Setup.bat uses another Python minor or venv."
}

Write-Host "[Maho] Stale-registration purge finished."
exit 0
