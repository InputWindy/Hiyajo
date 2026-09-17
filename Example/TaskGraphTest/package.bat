@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Package UI — pick platform / config, ship to Packaged/<Platform>/<Config>/.
rem Entry contract: engine-local Python only (maho_pythonw.bat -> Tools/python);
rem this project's .cproject is forwarded to the engine's Tools/package_ui.py.

call "../../Tools/maho_pythonw.bat" "../../Tools/package_ui.py" "%~dp0TaskGraphTest.cproject" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch the package UI. Run Setup.bat in the engine root:
	echo         ../..
	pause
	exit /b %ERR%
)
exit /b 0
