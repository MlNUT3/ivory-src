@echo off
rem Pushes the source backup to a NEW PRIVATE GitHub repo.
rem First time only: run "gh auth login" in a terminal and follow the prompts,
rem then double-click this file.
setlocal
cd /d "%~dp0"
where gh >nul 2>&1 || (set "gh=C:\Program Files\GitHub CLI\gh.exe") || (echo gh not found & pause & exit /b 1)
gh auth status >nul 2>&1 || (echo not logged in - run: gh auth login & pause & exit /b 1)
gh repo create ivory-src --private --source . --remote origin --push
if errorlevel 1 (echo push failed - check the message above) else (echo done - private repo created and pushed)
pause
