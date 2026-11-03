@echo off
rem Detergine developer tool launcher (Windows). See tools\dg.py or run "dg --help".
where py >nul 2>nul
if %ERRORLEVEL%==0 (py -3 "%~dp0tools\dg.py" %*) else (python "%~dp0tools\dg.py" %*)
exit /b %ERRORLEVEL%
