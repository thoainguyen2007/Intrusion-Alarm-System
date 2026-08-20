@echo off
if not exist "build\build.ninja" (
    cmake -B build -G "Ninja" -DCMAKE_TOOLCHAIN_FILE="cmake/gcc-arm-none-eabi.cmake"
)
cmake --build build
if %errorlevel% neq 0 ( pause & exit /b %errorlevel% )
arm-none-eabi-objcopy -O ihex build/Prj2008.elf build/Prj2008.hex
arm-none-eabi-objcopy -O binary build/Prj2008.elf build/Prj2008.bin
STM32_Programmer_CLI.exe -c port=SWD mode=UR -w build/Prj2008.elf -v -rst
pause
