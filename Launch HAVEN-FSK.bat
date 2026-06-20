@echo off
title HAVEN-FSK
cd /d "%~dp0"
"%~dp0venv\Scripts\python.exe" "%~dp0haven_fsk.py"
if errorlevel 1 (
    echo.
    echo HAVEN-FSK exited with an error.
    pause
)
