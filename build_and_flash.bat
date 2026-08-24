@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "BUILD_PRESET=Debug"
set "BUILD_DIR=build\Debug"
set "PROGRAMMER="

where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake.exe was not found in PATH.
    pause
    exit /b 1
)

where ninja.exe >nul 2>&1
if errorlevel 1 (
    echo ERROR: ninja.exe was not found in PATH.
    pause
    exit /b 1
)

for /f "delims=" %%I in ('where STM32_Programmer_CLI.exe 2^>nul') do if not defined PROGRAMMER set "PROGRAMMER=%%I"
if not defined PROGRAMMER (
    set "PROGRAMMER=%ProgramFiles%\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
)
if not exist "%PROGRAMMER%" (
    echo ERROR: STM32_Programmer_CLI.exe was not found.
    echo Install STM32CubeProgrammer or add its bin directory to PATH.
    pause
    exit /b 1
)

cmake --preset %BUILD_PRESET%
if errorlevel 1 ( pause & exit /b %errorlevel% )

cmake --build --preset %BUILD_PRESET%
if errorlevel 1 ( pause & exit /b %errorlevel% )

if not "%~1"=="" set "STLINK_SN=%~1"
if not defined STLINK_SN (
    echo.
    "%PROGRAMMER%" -l stlink
    echo.
    set /p "STLINK_SN=Enter the ST-Link serial to flash, or leave blank when only one probe is connected: "
)

if defined STLINK_SN (
    "%PROGRAMMER%" -c port=SWD sn=%STLINK_SN% freq=100 -w "%BUILD_DIR%\Prj2008.elf" -v -rst
) else (
    "%PROGRAMMER%" -c port=SWD freq=100 -w "%BUILD_DIR%\Prj2008.elf" -v -rst
)
if errorlevel 1 ( pause & exit /b %errorlevel% )

echo Flashed %BUILD_DIR%\Prj2008.elf successfully.
pause
