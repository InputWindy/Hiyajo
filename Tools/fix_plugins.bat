@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Batch-fix all engine plugins: regenerate missing Api.h + .gen.h headers.
call "%~dp0maho_python.bat" "%~dp0fix_plugins.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] fix_plugins.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
