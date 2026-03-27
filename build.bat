@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d C:\Users\produ\legion-stage
"C:\Program Files\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 exit /b %errorlevel%
"C:\Program Files\CMake\bin\cmake.exe" --build build --config Release
