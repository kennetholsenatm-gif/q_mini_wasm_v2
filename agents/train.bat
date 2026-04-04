@echo off
REM TestAgent Training Script for q_mini_wasm_v2 Framework
REM Uses D:\ drive as Flash-CIM storage for training

echo ========================================
echo TestAgent Training
echo q_mini_wasm_v2 Framework
echo ========================================
echo.

REM Check if Go is installed
where go >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Go is not installed or not in PATH
    echo Please install Go from https://golang.org/dl/
    exit /b 1
)

echo Go version:
go version
echo.

REM Build the training tool
echo Building train_test_agent...
go build -o train_test_agent.exe train_test_agent.go

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

echo Build successful!
echo.

REM Run the training
echo Running training...
echo.
train_test_agent.exe

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Training failed
    exit /b 1
)

echo.
echo ========================================
echo Training Complete!
echo ========================================
echo.
echo Next steps:
echo 1. Review the training report at D:\test_agent\training\training_report.txt
echo 2. Check training templates at D:\test_agent\training\test_cases\
echo 3. Run the TestAgent: go run test_agent.go generate sample.cpp Go
echo.