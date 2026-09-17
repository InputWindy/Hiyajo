@echo off
setlocal EnableExtensions

rem .cplugin double-click -> validate + auto-fix one plugin (engine Tools/python only).
rem %* is the path Explorer passes for the double-clicked file (absolute).
rem Console tool: the fix log must stay visible, so it pauses at the end.

call "%~dp0maho_python.bat" "%~dp0fix_plugin.py" %*
set "ERR=%ERRORLEVEL%"
rem Pause ONLY on failure: a double-click must show the error, while a scripted
rem caller (CI, another tool) must not be blocked waiting for a keypress.
if not "%ERR%"=="0" (
	echo.
	echo [ERROR] fix_plugin.py failed with exit code %ERR%
	echo.
	echo Press any key to close . . .
	pause >nul
)
exit /b %ERR%
