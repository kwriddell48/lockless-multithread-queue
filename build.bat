@echo off
REM Build script for Windows
REM Try to find a C compiler

REM Check for gcc (MinGW)
where gcc >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Using gcc...
    gcc -Wall -Wextra -std=c11 -O2 -pthread main.c queue.c -o queue_demo.exe
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: queue_demo.exe
        exit /b 0
    )
)

REM Check for cl (MSVC)
where cl >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Using MSVC cl...
    cl /W4 /std:c11 /O2 main.c queue.c /Fe:queue_demo.exe /link
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: queue_demo.exe
        exit /b 0
    )
)

REM Check for clang
where clang >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Using clang...
    clang -Wall -Wextra -std=c11 -O2 -pthread main.c queue.c -o queue_demo.exe
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: queue_demo.exe
        exit /b 0
    )
)

echo ERROR: No C compiler found!
echo Please install one of the following:
echo   - MinGW-w64 (gcc)
echo   - Microsoft Visual Studio (cl)
echo   - LLVM/Clang
echo.
echo Or add the compiler to your PATH and try again.
exit /b 1
