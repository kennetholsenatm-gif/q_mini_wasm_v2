@echo off
setlocal enabledelayedexpansion

set "file=C:\Users\kenne\.cline\worktrees\bf565\q_mini_wasm_v2\q_mini_wasm_v2\CMakeLists.txt"
set "temp=%file%.tmp"

(
    for /f "usebackq delims=" %%a in ("%file%") do (
        set "line=%%a"
        set "line=!line:\\$<$<=$<!"
        set "line=!line:\\$<$<NOT:\\$<$<CXX_COMPILER_ID:MSVC>>=-Wall -Wextra -Wpedantic>!"
        echo !line!
    )
) > "%temp%"

move /y "%temp%" "%file%" >nul
echo File fixed successfully!