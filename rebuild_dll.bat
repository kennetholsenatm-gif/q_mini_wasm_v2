@echo off
echo Rebuilding q_training.dll with 65 parameters...

cd C:\GitHub\q_mini_wasm_v2\q_mini_wasm_v2

:: Use an existing build directory or create new
if exist build_final (
    cd build_final
) else if exist build (
    cd build
) else (
    mkdir build_rebuild
    cd build_rebuild
    cmake .. -DCMAKE_BUILD_TYPE=Release
)

:: Build just the training DLL
cmake --build . --target q_training --config Release

if %ERRORLEVEL% NEQ 0 (
    echo DLL BUILD FAILED
    exit /b 1
)

echo DLL BUILD SUCCESS
echo Copying q_training.dll to repo root...

:: Copy to locations where Go expects it
copy /Y q_training.dll C:\GitHub\q_mini_wasm_v2\
copy /Y q_training.dll C:\GitHub\q_mini_wasm_v2\native_runtime\

echo Done! q_training.dll now has 65 parameters.
pause
