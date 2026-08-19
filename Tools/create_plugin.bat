@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem New-plugin UI (create_plugin_ui.py) — engine Tools/python only.
call "%~dp0maho_python.bat" "%~dp0create_plugin_ui.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] create_plugin_ui.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
