@echo off
echo Building qminiwasm server...

cd C:\GitHub\q_mini_wasm_v2\cmd\qminiwasm

set CGO_ENABLED=0
go build -o ..\..\qminiwasm.exe .

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED
    exit /b 1
)

echo BUILD SUCCESS
echo Starting server...
cd ..\..
start qminiwasm.exe
