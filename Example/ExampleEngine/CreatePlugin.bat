@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem CreatePlugin.bat — new-plugin UI for this project.
rem Entry contract: engine-local Python only (maho_pythonw.bat -> Tools/python).
rem No default directory is passed: the UI itself defaults to <cwd>/Plugins, which
rem is this project's plugins/ catalog (hand-filled .cplugin Dependencies, no UI).

call "../../Tools/maho_pythonw.bat" "../../Tools/create_plugin_ui.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch the plugin UI. Run Setup.bat in the engine root:
	echo         ../..
	pause
	exit /b %ERR%
)
exit /b 0
