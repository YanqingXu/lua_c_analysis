@echo off
REM ==================================================================
REM Lua 5.1.5 Bytecode Dumper (lua_c_analysis)
REM ------------------------------------------------------------------
REM Usage:
REM   dump_bytecode.bat <script.lua> [full]
REM
REM This script:
REM   1. Builds tests\bytecode_dumper.c into tests\bin\bytecode_dumper.exe
REM      (linked against bin\lua.lib and src\print.c)
REM   2. Runs the dumper on the given Lua source file
REM   3. Saves output to <script>.ref.txt next to the source file
REM ==================================================================

if "%~1"=="" (
    echo Usage: %~nx0 ^<script.lua^> [full]
    exit /b 1
)

setlocal

REM ------------------------------------------------------------------
REM Set up MSVC build environment (optional)
REM If vcvarsall.bat is not found, assume cl is already on PATH
REM ------------------------------------------------------------------
set VCVARSALL=D:\VS2026\2026\VC\Auxiliary\Build\vcvarsall.bat
if exist "%VCVARSALL%" (
    echo [INFO] Setting up MSVC environment...
    call "%VCVARSALL%" x64 >nul 2>&1
)

set INPUT=%~1
set OUTPUT=%~dpn1.ref.txt
set FULL_FLAG=
if not "%~2"=="" set FULL_FLAG=full

if not exist "tests" mkdir "tests"
if not exist "tests\bin" mkdir "tests\bin"

echo [INFO] Building bytecode_dumper.exe ...

cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS ^
    /I src ^
    /Fetests\bin\bytecode_dumper.exe ^
    tests\bytecode_dumper.c ^
    src\print.c ^
    bin\lua.lib

if errorlevel 1 (
    echo [ERROR] Failed to build bytecode_dumper.exe
    endlocal
    exit /b 1
)

REM Clean intermediate object files (keep bin/*.lib and executables)
for %%F in (*.obj) do del "%%F" >nul 2>&1

echo [INFO] Dumping bytecode for "%INPUT%" ...

if "%FULL_FLAG%"=="" (
    tests\bin\bytecode_dumper.exe "%INPUT%" > "%OUTPUT%"
) else (
    tests\bin\bytecode_dumper.exe "%INPUT%" full > "%OUTPUT%"
)

if errorlevel 1 (
    echo [ERROR] Failed to dump bytecode for "%INPUT%"
    endlocal
    exit /b 1
)

echo [OK] Reference bytecode saved to: "%OUTPUT%"

echo.
endlocal
exit /b 0

