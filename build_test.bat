@echo off  
echo Lua 5.1.5 Bytecode Test  
echo.  
cl /nologo /W3 /O2 /MD /D_CRT_SECURE_NO_WARNINGS /Fetests\bin\test_bytecode.exe tests\test_bytecode.c src\*.c  
if errorlevel 1 exit /b 1  
del *.obj >nul 2>&1  
tests\bin\test_bytecode.exe 
