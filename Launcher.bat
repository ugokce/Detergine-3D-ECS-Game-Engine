@echo off
rem Double-click in Explorer to open the Detergine Launcher (Windows).
cd /d "%~dp0"
where py >nul 2>nul
if %ERRORLEVEL%==0 (start "" pyw -3 tools\dg.py gui) else (start "" pythonw tools\dg.py gui)
