@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Auto-fix a single plugin's missing generated headers (Api.h + .gen.h).
call "%~dp0maho_python.bat" "%~dp0fix_plugin.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] fix_plugin.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
