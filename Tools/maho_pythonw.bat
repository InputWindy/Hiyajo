@echo off
setlocal EnableExtensions
rem GUI helper: run a script with the local pythonw only (never system Python).
rem Usage: maho_pythonw.bat Tools\create_project.py [args...]
rem Fire-and-forget: the GUI keeps running after this .bat returns, so the caller's
rem console window closes at once (no lingering black box).

set "MAHO_TOOLS=%~dp0"
set "MAHO_PYTHONW=%MAHO_TOOLS%python\pythonw.exe"
set "MAHO_PYTHON=%MAHO_TOOLS%python\python.exe"
if not exist "%MAHO_PYTHONW%" set "MAHO_PYTHONW=%MAHO_TOOLS%python\Scripts\pythonw.exe"
if not exist "%MAHO_PYTHON%" set "MAHO_PYTHON=%MAHO_TOOLS%python\Scripts\python.exe"

if exist "%MAHO_PYTHONW%" (
	start "" "%MAHO_PYTHONW%" %*
	exit /b 0
)

if exist "%MAHO_PYTHON%" (
	echo [WARN] pythonw.exe not found under Tools\python; using local python.exe
	start "" "%MAHO_PYTHON%" %*
	exit /b 0
)

echo [ERROR] Local Python not found under Tools\python
echo [ERROR] From the Maho engine root, run Setup.bat first.
echo [ERROR] Do not use a system-installed Python for Maho tools.
exit /b 1
