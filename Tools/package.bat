@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Package UI — pick platform / config, ship to Packaged/<Platform>/<Config>/.
rem Entry contract: run the tools with the ENGINE-local Python only
rem (Tools\maho_pythonw.bat -> Tools\python). Optional args (e.g. a .cproject
rem path) are forwarded to package_ui.py.
rem GUI -> pythonw (no Python console); this .bat itself flashes cmd briefly.

call "%~dp0maho_pythonw.bat" "%~dp0package_ui.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch the package UI ^(exit %ERR%^).
	echo         Run Setup.bat if Tools\python is missing.
	pause
	exit /b %ERR%
)
exit /b 0
