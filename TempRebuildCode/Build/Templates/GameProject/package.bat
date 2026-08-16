@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Resolve EngineDirectory from *.cproject, then run engine Tools via local Python.
rem No system Python required — uses engine Tools\python via maho_python.bat.

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

powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0Tools\invoke_engine.ps1" -Action package -CProject "%CPROJECT%"
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Package failed with exit code %ERR%
	pause
	exit /b %ERR%
)
exit /b 0
