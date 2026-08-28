@echo off
setlocal
cd /d "%~dp0"

rem Package UI — pick platform / config, ship to Packaged/<Platform>/<Config>/.
rem Forwards to the engine's package_ui.py (same GUI as Tools/package.bat).

set "PYW=../../Tools/python/pythonw.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/Scripts/pythonw.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/python.exe"
if not exist "%PYW%" set "PYW=../../Tools/python/Scripts/python.exe"

if not exist "%PYW%" (
	echo [ERROR] Engine-local Python missing. Run Setup.bat in the engine root:
	echo         ../..
	pause
	exit /b 1
)

start "" "%PYW%" "../../Tools/package_ui.py" "%~dp0ExampleEngine.cproject"
exit /b 0
