@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Validate engine plugins — report missing headers that would break compilation.
call "%~dp0maho_python.bat" "%~dp0check_plugins.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] check_plugins.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
