@echo off
setlocal
set "ROOT=%~dp0"
set "VS_DEVCMD=E:\Data\VS\Common7\Tools\VsDevCmd.bat"

if not exist "%VS_DEVCMD%" (
  echo Visual Studio developer environment not found: "%VS_DEVCMD%"
  exit /b 1
)

call "%VS_DEVCMD%" -arch=amd64
if errorlevel 1 exit /b %errorlevel%

cmake -S "%ROOT%Native" -B "%ROOT%Native\build-vs" -G "Visual Studio 18 2026" -A x64
if errorlevel 1 exit /b %errorlevel%

cmake --build "%ROOT%Native\build-vs" --config Release --target OHook -- /m > "%ROOT%Native\build-vs\msbuild-build.log" 2>&1
if errorlevel 1 (
  findstr /R /C:"error C[0-9][0-9][0-9][0-9]" /C:"error MSB[0-9][0-9][0-9][0-9]" /C:"FAILED" "%ROOT%Native\build-vs\msbuild-build.log"
  exit /b 1
)

for /r "%ROOT%Native\build-vs" %%F in (*.exe) do if exist "%%F" del /f /q "%%F"
for /r "%ROOT%Native\build-vs" %%F in (*.pdb) do if exist "%%F" del /f /q "%%F"
for /r "%ROOT%Menu" %%F in (*.exe) do if exist "%%F" del /f /q "%%F"
for /r "%ROOT%Menu" %%F in (*.pdb) do if exist "%%F" del /f /q "%%F"
if exist "%ROOT%Menu\bin" rmdir /s /q "%ROOT%Menu\bin"
if exist "%ROOT%Menu\obj" rmdir /s /q "%ROOT%Menu\obj"
if exist "%ROOT%Native\build-vs\ManagedPayload\RONMenu.UI.dll" del /f /q "%ROOT%Native\build-vs\ManagedPayload\RONMenu.UI.dll"
if exist "%ROOT%Native\build-vs\Release\OHook.dll" del /f /q "%ROOT%Native\build-vs\Release\OHook.dll"
if exist "%ROOT%dist\Release" rmdir /s /q "%ROOT%dist\Release"
if exist "%ROOT%dist\OHook.loader.json" del /f /q "%ROOT%dist\OHook.loader.json"

echo Single deliverable DLL: "%ROOT%dist\OHook.dll"
exit /b %errorlevel%
