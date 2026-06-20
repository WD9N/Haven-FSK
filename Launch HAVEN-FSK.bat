@echo off
title HAVEN-FSK
cd /d "%~dp0"
"C:\Users\wd9nr\Downloads\Haven-FSK\venv\Scripts\python.exe" robustmfsk.py
if errorlevel 1 (
    echo.
    echo HAVEN-FSK exited with an error.
    pause
)
