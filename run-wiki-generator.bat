@echo off
echo ========================================
echo q_mini_wasm_v2 Wiki Generator
echo ========================================
echo.

echo This script will generate wiki files from docs/
echo.

REM Check if Python is available
python --version >nul 2>&1
if errorlevel 1 (
    echo Python is not installed or not in PATH
    echo Please install Python 3.6+ and try again
    pause
    exit /b 1
)

echo Running wiki generator...
python docs/wiki-pipeline/generate_wiki.py

if errorlevel 1 (
    echo.
    echo Wiki generation failed!
    echo Trying simple script instead...
    python generate_wiki_simple.py
)

echo.
echo ========================================
echo Wiki generation complete!
echo ========================================
echo.
echo Files created in wiki-output/ directory:
dir /b wiki-output
echo.
echo Next steps:
echo 1. Review the generated files
echo 2. Push to GitHub Wiki:
echo    git clone https://github.com/kennetholsenatm-gif/q_mini_wasm_v2.wiki.git
echo    cp wiki-output/* q_mini_wasm_v2.wiki/
echo    cd q_mini_wasm_v2.wiki
echo    git add .
echo    git commit -m "Populate wiki with research documentation"
echo    git push
echo.
pause