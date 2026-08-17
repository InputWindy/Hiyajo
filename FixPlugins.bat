@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Batch-fix every engine plugin — equivalent to double-clicking each .cplugin:
rem regenerate missing Api.h + starter <Name>.md / <Name>.html docs.

call "%~dp0Tools\maho_python.bat" "%~dp0Tools\fix_plugins.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] fix_plugins.py failed with exit code %ERR%
	pause
	exit /b %ERR%
)
echo.
pause
exit /b 0
