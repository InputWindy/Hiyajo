@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Internal: generate the Visual Studio .sln from a .cproject.
rem Also the target of the Windows .cproject file association — the user
rem double-clicks the .cproject, which launches this .bat with the file as %1.
rem Console tool: cmake/generate output must stay visible, so it pauses at the end.

call "%~dp0maho_python.bat" "%~dp0generateProject.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo.
	echo [ERROR] generateProject.py failed with exit code %ERR%
)
echo.
pause
exit /b %ERR%
