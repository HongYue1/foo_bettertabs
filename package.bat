@echo off
rem Package foo_bettertabs as a .fb2k-component. Usage: package.bat
rem
rem A .fb2k-component is a plain zip: the 32-bit DLL at the root, the 64-bit one in x64\.
rem Both architectures are required for this component, so both are always built.
rem Builds are left to build.bat so there is one build path, not two.
setlocal

rem The exit code, not the DLL: a failed build leaves the previous DLL in place.
call build.bat Release x64
if errorlevel 1 (
  echo FAILED: x64 build failed - see build.log
  exit /b 1
)
call build.bat Release Win32
if errorlevel 1 (
  echo FAILED: x86 build failed - see build-Win32.log
  exit /b 1
)
if not exist "x64\Release\foo_bettertabs.dll" (
  echo FAILED: no x64 DLL - see build.log
  exit /b 1
)
if not exist "Win32\Release\foo_bettertabs.dll" (
  echo FAILED: no x86 DLL - see build-Win32.log
  exit /b 1
)

if exist dist rmdir /s /q dist
mkdir dist\stage\x64
mkdir dist\symbols
copy /y "x64\Release\foo_bettertabs.dll" "dist\stage\x64\foo_bettertabs.dll" >nul
copy /y "x64\Release\foo_bettertabs.pdb" "dist\symbols\foo_bettertabs-x64.pdb" >nul
copy /y "Win32\Release\foo_bettertabs.dll" "dist\stage\foo_bettertabs.dll" >nul
copy /y "Win32\Release\foo_bettertabs.pdb" "dist\symbols\foo_bettertabs-x86.pdb" >nul

rem 7-Zip, not Compress-Archive: Windows PowerShell 5 writes backslashes in entry names
rem (x64\foo_bettertabs.dll), which the zip format does not allow and some extractors mishandle.
set SEVENZIP=C:\Program Files\7-Zip\7z.exe
if not exist "%SEVENZIP%" (
  echo FAILED: 7z.exe not found at "%SEVENZIP%"
  exit /b 1
)
pushd dist\stage
"%SEVENZIP%" a -tzip -bso0 -bsp0 "..\foo_bettertabs.fb2k-component" * >nul
set ZIPERR=%ERRORLEVEL%
popd
if not "%ZIPERR%"=="0" (
  echo FAILED: 7z exited with %ZIPERR%
  exit /b 1
)
rmdir /s /q dist\stage

echo Packaged dist\foo_bettertabs.fb2k-component (x86 + x64); symbols in dist\symbols
