@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Check all repository .md links (broken-link / stale-doc hygiene).
rem Run this after editing docs; also wired into update_docs.py.
call "%~dp0maho_python.bat" "%~dp0check_md_links.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] check_md_links.py found broken links ^(exit %ERR%^).
	exit /b %ERR%
)
exit /b 0
