@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem switch_engine — re-link a .cproject to another engine root (GUI).
rem Launched from the Explorer context menu (Maho -> 选择链接引擎) through
rem launch_switch_engine.vbs -> this .bat -> maho_pythonw.bat -> switch_engine.py.
rem The launcher runs this .bat HIDDEN, so there is no pause here (a hidden
rem console waiting on a keypress would never be dismissed); the GUI is the result.
rem Arg: the .cproject path.

call "%~dp0maho_pythonw.bat" "%~dp0switch_engine.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" exit /b %ERR%
exit /b 0
