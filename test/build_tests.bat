@echo off
rem Builds and runs the offline tests (codec, strip layout). Output: test\tests.out
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
set CL_FLAGS=/nologo /EHsc /std:c++latest /O2 /MT /W4 /WX /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /Fo:out\
cl %CL_FLAGS% /Fe:out\codec_test.exe codec_test.cpp ..\src\model\codec.cpp /link /SUBSYSTEM:CONSOLE > out\build_codec.txt 2>&1
if errorlevel 1 (type out\build_codec.txt & exit /b 1)
cl %CL_FLAGS% /Fe:out\layout_test.exe layout_test.cpp ..\src\strip\strip_layout.cpp /link /SUBSYSTEM:CONSOLE > out\build_layout.txt 2>&1
if errorlevel 1 (type out\build_layout.txt & exit /b 1)
echo == codec == > tests.out
out\codec_test.exe >> tests.out 2>&1
set E1=%ERRORLEVEL%
echo == layout == >> tests.out
out\layout_test.exe >> tests.out 2>&1
set E2=%ERRORLEVEL%
echo EXIT=%E1% %E2% >> tests.out
type tests.out
