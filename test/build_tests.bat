@echo off
rem Builds and runs the offline tests (codec, strip layout, strip render, WM_SHOWWINDOW, z-order).
rem The cover colour code has its own tests in ..\fb2k-common\test.
rem Output: test\tests.out; render PNGs in test\out\render_*.png
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
set CL_FLAGS=/nologo /EHsc /std:c++latest /O2 /MT /W4 /WX /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /I..\..\fb2k-common\include /Fo:out\
cl %CL_FLAGS% /Fe:out\codec_test.exe codec_test.cpp ..\src\model\codec.cpp /link /SUBSYSTEM:CONSOLE > out\build_codec.txt 2>&1
if errorlevel 1 (type out\build_codec.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\layout_test.exe layout_test.cpp ..\src\strip\strip_layout.cpp /link /SUBSYSTEM:CONSOLE > out\build_layout.txt 2>&1
if errorlevel 1 (type out\build_layout.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\render_test.exe render_test.cpp ..\src\strip\strip_window.cpp ..\src\strip\strip_layout.cpp ..\src\platform\graphics.cpp /link /SUBSYSTEM:CONSOLE > out\build_render.txt 2>&1
if errorlevel 1 (type out\build_render.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\showwindow_test.exe showwindow_test.cpp /link /SUBSYSTEM:CONSOLE user32.lib > out\build_showwindow.txt 2>&1
if errorlevel 1 (type out\build_showwindow.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\zorder_test.exe zorder_test.cpp /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:compat.manifest user32.lib > out\build_zorder.txt 2>&1
if errorlevel 1 (type out\build_zorder.txt & exit /b 1)
echo == codec == > tests.out
out\codec_test.exe >> tests.out 2>&1
set E1=%ERRORLEVEL%
echo == layout == >> tests.out
out\layout_test.exe >> tests.out 2>&1
set E2=%ERRORLEVEL%
echo == render == >> tests.out
out\render_test.exe >> tests.out 2>&1
set E3=%ERRORLEVEL%
echo == showwindow == >> tests.out
out\showwindow_test.exe >> tests.out 2>&1
set E4=%ERRORLEVEL%
echo == zorder == >> tests.out
out\zorder_test.exe >> tests.out 2>&1
set E5=%ERRORLEVEL%
echo EXIT=%E1% %E2% %E3% %E4% %E5% >> tests.out
type tests.out
