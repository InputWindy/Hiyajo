@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem Bootstrap Maho local Python (not added to PATH).
rem Usage:
rem   Setup.bat           install / repair Tools\python junction
rem   Setup.bat --force   wipe and recreate
rem
rem Why NOT install python.org into the engine tree:
rem   The Windows installer registers TargetDir in HKCU/ARP. If that path is
rem   under the repo and you rename/delete the folder, the next silent install
rem   becomes "Modify", exits 0, and writes nothing. Bumping 3.12-^>3.13 only
rem   repeats the same failure once THAT version is poisoned too.
rem
rem Stable layout ^(rename-safe^):
rem   %LOCALAPPDATA%\Maho\python\tooling\   real files ^(outside the repo^)
rem   <engine>\Tools\python\                junction -^> tooling
rem
rem Bootstrap order:
rem   1) venv from any host Python with tkinter  — no Burn/MSI registration
rem   2) else python.org installer into tooling  — after purging ghost ARP
rem Renaming the engine folder: re-run Setup.bat only to recreate the junction.

set "ROOT=%~dp0"
set "PY_LINK=%ROOT%Tools\python"
set "CACHE=%ROOT%Tools\_cache"
set "PY_HOME=%LOCALAPPDATA%\Maho\python\tooling"
set "PY_EXE=%PY_HOME%\python.exe"
set "PY_EXE_VENV=%PY_HOME%\Scripts\python.exe"
rem Installer fallback version only ^(unused when venv succeeds^).
set "PY_VER=3.13.13"
set "INSTALLER=%CACHE%\python-%PY_VER%-amd64.exe"
set "URL=https://www.python.org/ftp/python/%PY_VER%/python-%PY_VER%-amd64.exe"
set "INSTALL_LOG=%CACHE%\python-install.log"
set "PURGE_PS1=%ROOT%Tools\purge_stale_python_install.ps1"
set "ERR=0"
set "STATUS=OK"
set "ALREADY=0"

if /I "%~1"=="--force" (
	echo [Maho] --force: removing Tools\python link and LocalAppData tooling...
	call :remove_python_link
	if exist "%PY_HOME%" (
		echo         %PY_HOME%
		rmdir /s /q "%PY_HOME%"
	)
	rem Previous layout used a versioned folder — drop it too.
	if exist "%LOCALAPPDATA%\Maho\python\3.13.13" (
		echo         %LOCALAPPDATA%\Maho\python\3.13.13
		rmdir /s /q "%LOCALAPPDATA%\Maho\python\3.13.13"
	)
	if exist "%LOCALAPPDATA%\Maho\python\3.12.8" (
		rmdir /s /q "%LOCALAPPDATA%\Maho\python\3.12.8"
	)
)

rem Only treat as "already OK" when the stable LocalAppData home works.
call :resolve_home_python
if defined LOCAL_PY (
	"%LOCAL_PY%" -c "import tkinter, sys; print(sys.version.split()[0])" 2>nul
	if not errorlevel 1 (
		call :ensure_python_link
		if not errorlevel 1 (
			set "ALREADY=1"
			goto :success
		)
	)
	echo [Maho] Existing tooling looks broken. Recreating...
	call :remove_python_link
	if exist "%PY_HOME%" rmdir /s /q "%PY_HOME%"
)

if not exist "%LOCALAPPDATA%\Maho\python" mkdir "%LOCALAPPDATA%\Maho\python" 2>nul
if not exist "%CACHE%" mkdir "%CACHE%" 2>nul

echo [Maho] Prefer venv into stable LocalAppData path ^(no engine-tree registration^):
echo         %PY_HOME%
set "HOST_PY="
call :find_host_python
if defined HOST_PY goto :do_venv

echo [Maho] No host Python with tkinter — trying python.org installer into tooling...
goto :do_installer

:do_venv
echo [Maho] Host: %HOST_PY%
if exist "%PY_HOME%" rmdir /s /q "%PY_HOME%"
"%HOST_PY%" -m venv "%PY_HOME%"
if errorlevel 1 (
	echo [Maho] venv failed — trying python.org installer...
	goto :do_installer
)
call :resolve_home_python
if not defined LOCAL_PY (
	echo [Maho] venv missing python.exe — trying installer...
	goto :do_installer
)
"%LOCAL_PY%" -c "import tkinter, sys; print(sys.version.split()[0])" 2>nul
if errorlevel 1 (
	echo [Maho] venv lacks tkinter — trying installer...
	if exist "%PY_HOME%" rmdir /s /q "%PY_HOME%"
	goto :do_installer
)
echo maho-local-venv> "%PY_HOME%\.maho_managed"
call :ensure_python_link
if errorlevel 1 goto :fail_link
goto :success

:do_installer
if not exist "%CACHE%" mkdir "%CACHE%"
if not exist "%INSTALLER%" (
	echo [Maho] Downloading Python %PY_VER% installer...
	echo         %URL%
	where curl >nul 2>&1
	if errorlevel 1 (
		powershell -NoProfile -ExecutionPolicy Bypass -Command ^
			"Invoke-WebRequest -Uri '%URL%' -OutFile '%INSTALLER%'"
	) else (
		curl.exe -L --fail -o "%INSTALLER%" "%URL%"
	)
	if errorlevel 1 goto :fail_no_python
	if not exist "%INSTALLER%" goto :fail_no_python
	echo [Maho] Download OK.
)

echo [Maho] Purging stale python.org %PY_VER% registration ^(ghost ARP / dead paths^)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PURGE_PS1%" -Version "%PY_VER%"

echo [Maho] Installing into %PY_HOME% ^(still NOT under the engine tree^)
if exist "%INSTALL_LOG%" del /q "%INSTALL_LOG%" >nul 2>&1
if exist "%PY_HOME%" rmdir /s /q "%PY_HOME%"
"%INSTALLER%" /quiet InstallAllUsers=0 TargetDir="%PY_HOME%" PrependPath=0 Include_launcher=0 Include_test=0 Include_doc=0 Shortcuts=0 AssociateFiles=0 Include_pip=1 Include_tcltk=1 /log "%INSTALL_LOG%"

call :resolve_home_python
if not defined LOCAL_PY (
	echo [Maho] Installer left no python.exe — see %INSTALL_LOG%
	echo [Maho] Often HKLM leftovers of the same version force Modify mode.
	echo [Maho] Fix: Settings -^> Apps, uninstall broken "Python %PY_VER%", then Setup.bat --force
	goto :fail_no_python
)
"%LOCAL_PY%" -c "import tkinter, sys; print(sys.version.split()[0])" 2>nul
if errorlevel 1 (
	set "STATUS=FAILED"
	set "ERR=1"
	echo [FAILED] tkinter import failed for %LOCAL_PY%
	goto :finish
)
echo maho-local> "%PY_HOME%\.maho_managed"
call :ensure_python_link
if errorlevel 1 goto :fail_link
goto :success

:fail_link
set "STATUS=FAILED"
set "ERR=1"
echo [FAILED] Could not create Tools\python junction.
goto :finish

:fail_no_python
set "STATUS=FAILED"
set "ERR=1"
echo.
echo ======================================================================
echo  [FAILED] Could not bootstrap Maho Python tooling.
echo  Wanted  : %PY_HOME%
echo  Need    : a host Python 3.10+ with tkinter ^(miniconda / python.org^),
echo            OR a clean python.org silent install into LocalAppData.
echo ======================================================================
goto :finish

:success
call :ensure_python_link
call :resolve_local_python
echo.
echo ======================================================================
if "!ALREADY!"=="1" (
	echo  [SUCCESS] Maho local Python is already installed and verified.
) else (
	echo  [SUCCESS] Maho local Python ready.
)
echo ----------------------------------------------------------------------
echo  Real  : %PY_HOME%
echo  Link  : %PY_LINK%
echo  Exe   : %LOCAL_PY%
echo  Note  : Files live under LocalAppData. Renaming the engine folder is OK;
echo          re-run Setup.bat afterwards only to recreate Tools\python junction.
echo  Next  : Run CreateProject.bat
echo ======================================================================
set "STATUS=OK"
set "ERR=0"

:finish
echo.
if "!STATUS!"=="FAILED" (
	echo [Maho] Setup did NOT succeed. See [FAILED] details above.
) else (
	echo [Maho] Setup finished successfully. You can close this window.
)
pause
exit /b !ERR!

rem ---------------------------------------------------------------------------
:resolve_home_python
set "LOCAL_PY="
if exist "%PY_EXE%" set "LOCAL_PY=%PY_EXE%"
if not defined LOCAL_PY if exist "%PY_EXE_VENV%" set "LOCAL_PY=%PY_EXE_VENV%"
exit /b 0

:resolve_local_python
set "LOCAL_PY="
if exist "%PY_LINK%\python.exe" set "LOCAL_PY=%PY_LINK%\python.exe"
if not defined LOCAL_PY if exist "%PY_LINK%\Scripts\python.exe" set "LOCAL_PY=%PY_LINK%\Scripts\python.exe"
if not defined LOCAL_PY call :resolve_home_python
exit /b 0

:remove_python_link
rem Dangling junctions fail "if exist" but still block mklink — always try rmdir.
rem Junction / symlink: rmdir (no /s) removes the link only.
rmdir "%PY_LINK%" >nul 2>&1
rem Real directory (legacy install under the tree): wipe contents.
if exist "%PY_LINK%" rmdir /s /q "%PY_LINK%" >nul 2>&1
rem Last resort ^(locked / odd reparse^).
if exist "%PY_LINK%" (
	powershell -NoProfile -ExecutionPolicy Bypass -Command ^
		"Remove-Item -LiteralPath '%PY_LINK%' -Force -Recurse -ErrorAction SilentlyContinue"
)
exit /b 0

:ensure_python_link
if not exist "%PY_HOME%\python.exe" if not exist "%PY_HOME%\Scripts\python.exe" (
	echo [Maho] Cannot link Tools\python — install missing at:
	echo         %PY_HOME%
	exit /b 1
)
if exist "%PY_LINK%\python.exe" exit /b 0
if exist "%PY_LINK%\Scripts\python.exe" exit /b 0
call :remove_python_link
if not exist "%ROOT%Tools" mkdir "%ROOT%Tools"
echo [Maho] Linking Tools\python =^> %PY_HOME%
mklink /J "%PY_LINK%" "%PY_HOME%"
if errorlevel 1 (
	echo [FAILED] mklink /J failed — Tools\python still present after remove.
	exit /b 1
)
exit /b 0

:find_host_python
set "HOST_PY="
if exist "D:\miniconda\python.exe" (
	"D:\miniconda\python.exe" -c "import tkinter" 2>nul
	if not errorlevel 1 (
		set "HOST_PY=D:\miniconda\python.exe"
		exit /b 0
	)
)
if exist "%LocalAppData%\Programs\Python\Python313\python.exe" (
	"%LocalAppData%\Programs\Python\Python313\python.exe" -c "import tkinter" 2>nul
	if not errorlevel 1 (
		set "HOST_PY=%LocalAppData%\Programs\Python\Python313\python.exe"
		exit /b 0
	)
)
if exist "%LocalAppData%\Programs\Python\Python312\python.exe" (
	"%LocalAppData%\Programs\Python\Python312\python.exe" -c "import tkinter" 2>nul
	if not errorlevel 1 (
		set "HOST_PY=%LocalAppData%\Programs\Python\Python312\python.exe"
		exit /b 0
	)
)
where py >nul 2>&1
if not errorlevel 1 (
	py -3 -c "import tkinter" 2>nul
	if not errorlevel 1 (
		for /f "delims=" %%P in ('py -3 -c "import sys; print(sys.executable)" 2^>nul') do (
			echo %%P | findstr /I /C:"WindowsApps" >nul
			if errorlevel 1 (
				set "HOST_PY=%%P"
				exit /b 0
			)
		)
	)
)
where python >nul 2>&1
if not errorlevel 1 (
	for /f "delims=" %%P in ('where python 2^>nul') do (
		echo %%P | findstr /I /C:"WindowsApps" >nul
		if errorlevel 1 (
			"%%P" -c "import tkinter" 2>nul
			if not errorlevel 1 (
				set "HOST_PY=%%P"
				exit /b 0
			)
		)
	)
)
exit /b 0
