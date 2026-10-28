@echo off
REM ESP-IDF build wrapper - the entry point that VS Code tasks call.
REM
REM Goes through powershell -ExecutionPolicy Bypass so it works even when the
REM machine-wide policy is Restricted, and forwards every argument to idf.ps1.
REM
REM Usage:  idf.cmd build
REM         idf.cmd -p COM9 flash

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0idf.ps1" %*
exit /b %ERRORLEVEL%
