@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Launch the CreateProject UI via WScript + pythonw (no Python console).
rem Double-clicking this .bat briefly flashes cmd — that is expected.

set "VBS=%~dp0Tools\launch_create_project.vbs"
if not exist "%VBS%" (
	echo [ERROR] Missing %VBS%
	pause
	exit /b 1
)

wscript //nologo "%VBS%"
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch CreateProject UI ^(exit %ERR%^).
	echo         Run Setup.bat first if Tools\python is missing.
	pause
	exit /b %ERR%
)
exit /b 0
