@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Internal: generate Visual Studio .sln from .cproject (or engine workspace).
rem Also the target of Windows .cproject file association — users double-click .cproject, not this bat.
call "%~dp0maho_python.bat" "%~dp0generateProject.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] generateProject.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
