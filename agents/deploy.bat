@echo off
REM AnalysisAgent Deployment Script for q_mini_wasm_v2 Framework
REM Uses D:\ drive as Flash-CIM storage for end-to-end proof of concept

echo ========================================
echo AnalysisAgent Deployment
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

REM Build the deployment tool
echo Building deploy_analysis_agent...
go build -o deploy_analysis_agent.exe deploy_analysis_agent.go

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    exit /b 1
)

echo Build successful!
echo.

REM Run the deployment
echo Running deployment...
echo.
deploy_analysis_agent.exe

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Deployment failed
    exit /b 1
)

echo.
echo ========================================
echo Deployment Complete!
echo ========================================
echo.
echo Next steps:
echo 1. Review the deployment report at D:\analysis_agent\storage\deployment_report.txt
echo 2. Check the sample analysis at D:\analysis_agent\storage\analysis_results\sample_analysis.json
echo 3. Run the AnalysisAgent: go run analysis_agent.go analyze-file D:\analysis_agent\storage\code_samples\sample.cpp
echo.