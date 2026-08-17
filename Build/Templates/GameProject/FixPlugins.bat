@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Batch-fix the PROJECT's plugins only (never the engine's).

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

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Tools\invoke_engine.ps1" -Action fix-plugins -CProject "%CPROJECT%"
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Fix plugins failed with exit code %ERR%
	pause
	exit /b %ERR%
)
echo.
pause
exit /b 0
