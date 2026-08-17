@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem New-plugin UI — creates plugins in the project's Plugins/ dir.

set "CPROJECT="
for %%F in ("%~dp0*.cproject") do (
	set "CPROJECT=%%~fF"
	goto :have_cproject
)
:have_cproject
if not defined CPROJECT (
	echo [ERROR] No .cproject in %~dp0
	pause
	exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Tools\invoke_engine.ps1" -Action create-plugin -CProject "%CPROJECT%"
exit /b %ERRORLEVEL%
