@echo off
setlocal enabledelayedexpansion

set GCC_BIN=D:\arm-gcc-toolchain\xpack-arm-none-eabi-gcc-14.2.1-1.1\bin
set PATH=%GCC_BIN%;%PATH%

set BUILD_DIR=Project
if not exist %BUILD_DIR% mkdir %BUILD_DIR%

set C_DEFS=-DPY32F002Bx5 -DUSE_FULL_LL_DRIVER
set DRV_DIR=../../../REF/PY32F002B_Firmware_V1.2.1/Drivers
set C_INCLUDES=-I../Inc -I%DRV_DIR%/PY32F002B_HAL_Driver/Inc -I%DRV_DIR%/CMSIS/Device/PY32F0xx/Include -I%DRV_DIR%/CMSIS/Include
set CFLAGS=-mcpu=cortex-m0plus -mthumb %C_DEFS% %C_INCLUDES% -Og -Wall -fdata-sections -ffunction-sections -g -gdwarf-2 -MMD -MP

echo === Compiling ===

arm-none-eabi-gcc -c %CFLAGS% ../Src/main.c -o %BUILD_DIR%/main.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/app.c -o %BUILD_DIR%/app.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/seg.c -o %BUILD_DIR%/seg.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/debug.c -o %BUILD_DIR%/debug.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/key.c -o %BUILD_DIR%/key.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_it.c -o %BUILD_DIR%/py32f002b_it.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/system_py32f002b.c -o %BUILD_DIR%/system_py32f002b.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_ll_utils.c -o %BUILD_DIR%/py32f002b_ll_utils.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_ll_gpio.c -o %BUILD_DIR%/py32f002b_ll_gpio.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_ll_usart.c -o %BUILD_DIR%/py32f002b_ll_usart.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_ll_rcc.c -o %BUILD_DIR%/py32f002b_ll_rcc.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% ../Src/py32f002b_ll_pwr.c -o %BUILD_DIR%/py32f002b_ll_pwr.o
if %ERRORLEVEL% neq 0 goto error
arm-none-eabi-gcc -c %CFLAGS% startup_py32f002bxx.s -o %BUILD_DIR%/startup_py32f002bxx.o
if %ERRORLEVEL% neq 0 goto error

echo === Linking ===
set LDFLAGS=-mcpu=cortex-m0plus -mthumb -specs=nano.specs -Tpy32f002bx5.ld -Wl,-Map=%BUILD_DIR%/Project.map,--cref -Wl,--gc-sections -lc -lm -lnosys

arm-none-eabi-gcc %BUILD_DIR%/main.o %BUILD_DIR%/app.o %BUILD_DIR%/seg.o %BUILD_DIR%/debug.o %BUILD_DIR%/key.o %BUILD_DIR%/py32f002b_it.o %BUILD_DIR%/system_py32f002b.o %BUILD_DIR%/py32f002b_ll_utils.o %BUILD_DIR%/py32f002b_ll_gpio.o %BUILD_DIR%/py32f002b_ll_usart.o %BUILD_DIR%/py32f002b_ll_rcc.o %BUILD_DIR%/py32f002b_ll_pwr.o %BUILD_DIR%/startup_py32f002bxx.o %LDFLAGS% -o %BUILD_DIR%/Project.elf
if %ERRORLEVEL% neq 0 goto error

arm-none-eabi-objcopy -O ihex %BUILD_DIR%/Project.elf %BUILD_DIR%/Project.hex
arm-none-eabi-objcopy -O binary -S %BUILD_DIR%/Project.elf %BUILD_DIR%/Project.bin

echo === BUILD SUCCESS ===
arm-none-eabi-size %BUILD_DIR%/Project.elf
goto end

:error
echo === BUILD FAILED ===
exit /b 1

:end