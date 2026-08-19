@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Scan .cplugin manifests → module dependency / build order JSON.
call "%~dp0maho_python.bat" "%~dp0scan_plugins.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] scan_plugins.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
