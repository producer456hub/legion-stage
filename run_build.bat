@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d C:\Users\produ\legion-stage
echo === CMAKE CONFIGURE === >> build_log.txt 2>&1
"C:\Program Files\CMake\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release >> build_log.txt 2>&1
if %errorlevel% neq 0 (
    echo CMAKE CONFIGURE FAILED >> build_log.txt
    exit /b %errorlevel%
)
echo === CMAKE BUILD === >> build_log.txt 2>&1
"C:\Program Files\CMake\bin\cmake.exe" --build build --config Release >> build_log.txt 2>&1
if %errorlevel% neq 0 (
    echo CMAKE BUILD FAILED >> build_log.txt
    exit /b %errorlevel%
)
echo === BUILD COMPLETE === >> build_log.txt
