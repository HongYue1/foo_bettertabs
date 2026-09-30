@echo off
rem Build foo_bettertabs. Usage: build.bat [Release|Debug] [x64|Win32]
rem Read results from build.log / build-Win32.log, not stdout.
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
set PLAT=%2
if "%PLAT%"=="" set PLAT=x64

rem The SDK libs' /MT flavour is a separate configuration; a plain Release build of them is /MD
rem and gives LNK2038 against this component. Debug is /MDd on both sides, so it pairs with Debug.
if /I "%CFG%"=="Release" (set SDKCFG=Release-Static) else (set SDKCFG=Debug)

if /I "%PLAT%"=="Win32" (
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
) else (
  call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
)

set LOGFILE=build.log
if /I "%PLAT%"=="Win32" set LOGFILE=build-Win32.log
if exist %LOGFILE% del %LOGFILE%
set SDK=..\SDK-2026-09-17
set MSB=msbuild /nologo /m /v:minimal /p:Platform=%PLAT%
set LOG=/fileLogger "/flp:logfile=%LOGFILE%;verbosity=normal;append"

for %%P in (
  "%SDK%\pfc\pfc.vcxproj"
  "%SDK%\foobar2000\SDK\foobar2000_SDK.vcxproj"
  "%SDK%\foobar2000\helpers\foobar2000_sdk_helpers.vcxproj"
  "%SDK%\libPPUI\libPPUI.vcxproj"
  "%SDK%\foobar2000\foobar2000_component_client\foobar2000_component_client.vcxproj"
) do (
  %MSB% %%P /p:Configuration=%SDKCFG% %LOG%
  if errorlevel 1 (
    echo FAILED building %%P - see %LOGFILE%
    exit /b 1
  )
)

rem The Columns UI SDK has no Release-Static configuration; our copy sets RuntimeLibrary itself,
rem so its plain Release is already /MT. Build it with %CFG%, not %SDKCFG%.
%MSB% "%SDK%\columns_ui-sdk\columns_ui-sdk-public.vcxproj" /p:Configuration=%CFG% %LOG%
if errorlevel 1 (
  echo FAILED building columns_ui-sdk-public - see %LOGFILE%
  exit /b 1
)

%MSB% "foo_bettertabs.vcxproj" /p:Configuration=%CFG% %LOG%
set BUILDERR=%ERRORLEVEL%
if not "%BUILDERR%"=="0" (
  echo EXITCODE=%BUILDERR%
  exit /b %BUILDERR%
)

rem Windows 7 is the floor. A static import of a post-Win7 API makes the DLL fail to load there
rem with no useful message, so fail the build instead. Extend the list when a new API is used.
set DLL=%PLAT%\%CFG%\foo_bettertabs.dll
dumpbin /nologo /imports "%DLL%" > "%PLAT%\%CFG%\imports.txt"
findstr /I /C:"ForDpi" /C:"GetDpiForWindow" /C:"GetDpiForMonitor" /C:"DpiAwareness" /C:"shcore.dll" /C:"dcomp.dll" /C:"D2D1CreateDevice" /C:"GetSystemTimePreciseAsFileTime" /C:"WaitOnAddress" /C:"WakeByAddress" /C:"api-ms-win-core-synch-l1-2" "%PLAT%\%CFG%\imports.txt"
if not errorlevel 1 (
  echo FAILED: post-Windows 7 import in %DLL% - see %PLAT%\%CFG%\imports.txt
  echo EXITCODE=2
  exit /b 2
)
echo Imports OK (no post-Windows 7 APIs)
echo EXITCODE=0
exit /b 0
