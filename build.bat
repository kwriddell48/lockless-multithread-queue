@echo off
REM Build script for Windows using Cygwin 64 compiler (gcc)

set BUILD_SUCCESS=0

REM Check for gcc (Cygwin 64)
where gcc >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo Removing old object files
    rm -f *.o

    echo Using Cygwin 64 gcc compiler...
        gcc -Wall -Wextra -std=c11 -O2 -pthread queue.c -o queue.o
        echo Build successful! Binary created: queue.o
        set BUILD_SUCCESS=1
    ) else (
        echo Error: Failed to build queue.o
        exit /b 1
    )
    gcc -Wall -Wextra -std=c11 -O2 -pthread main.c queue.c -o queue_demo.exe
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: queue_demo.exe
        set BUILD_SUCCESS=1
    ) else (
        echo Error: Failed to build queue_demo.exe
        exit /b 1
    )
    gcc -Wall -Wextra -std=c11 -O2 -pthread thread_test.c queue.c -o thread_test.exe -latomic
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: thread_test.exe
    ) else (
        echo Error: Failed to build thread_test.exe

        exit /b 1
    )
    gcc -Wall -Wextra -std=c11 -O2 -pthread main.c queue.c -o main.exe -latomic
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: main.exe
    ) else (
        echo Error: Failed to build main.exe

        exit /b 1
    )
    gcc -Wall -Wextra -std=c11 -O2 -pthread quick_test.c queue.c -o quick_test.exe -latomic
    if %ERRORLEVEL% == 0 (
        echo Build successful! Binary created: quick_test.exe
    ) else (
        echo Error: Failed to build quick_test.exe

        exit /b 1
    )
    exit /b 0
) else (
    echo ERROR: Cygwin 64 gcc compiler not found!
    echo Please ensure Cygwin 64 is installed and gcc is in your PATH.
    exit /b 1
)
