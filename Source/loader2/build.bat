@echo off
setlocal
cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 ( echo [x] vcvars64 failed & exit /b 1 )

if not exist "payload_src.dll" (
  echo [x] payload_src.dll not found - build the cheat DLL first
  exit /b 1
)

echo [*] masking payload
powershell -NoProfile -ExecutionPolicy Bypass -File "mask_payload.ps1" "payload_src.dll" "payload.masked"
if errorlevel 1 ( echo [x] mask failed & exit /b 1 )

echo [*] extracting blob key
powershell -NoProfile -ExecutionPolicy Bypass -File "gen_blob_key.ps1" "C:\Users\ceram\AppData\Local\Temp\opencode\payload_key.h" "payload_blob_key.h"
if errorlevel 1 ( echo [x] blob key failed & exit /b 1 )

echo [*] compiling
rc /nologo payload.rc
if errorlevel 1 ( echo [x] rc failed & exit /b 1 )
rc /nologo loader.rc
if errorlevel 1 ( echo [x] rc failed & exit /b 1 )

cl /nologo /O2 /W4 /EHsc /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0603 ^
   main.cpp inject.cpp payload.res loader.res ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:PhoneExperienceHost.exe
set RC=%ERRORLEVEL%

del /q *.obj *.res payload.masked payload_blob_key.h 2>nul

if not "%RC%"=="0" ( echo [x] build failed & exit /b %RC% )
echo [ok] PhoneExperienceHost.exe
