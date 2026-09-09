@echo off
cd /d "%~dp0"
".venv\Scripts\python.exe" -X utf8 tools\launch_demo.py
if errorlevel 1 pause
