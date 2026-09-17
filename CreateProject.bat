@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem CreateProject — new-project UI (Maho project wizard).
rem Entry contract: cd to this script's folder, run the tools with the ENGINE-local
rem Python only (Tools\maho_pythonw.bat -> Tools\python), forward %* to Tools\create_project.py.
rem GUI -> pythonw (no Python console); this .bat itself flashes cmd briefly.

call "%~dp0Tools\maho_pythonw.bat" "%~dp0Tools\create_project.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch the CreateProject UI ^(exit %ERR%^).
	echo         Run Setup.bat if Tools\python is missing.
	pause
	exit /b %ERR%
)
exit /b 0
