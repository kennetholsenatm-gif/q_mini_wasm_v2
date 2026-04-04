@echo off
setlocal enabledelayedexpansion

:: =============================================================
:: q-mini-wasm-v2-general Training Launcher
:: DEPRECATED: Python training has been replaced by the native C++/SYCL WUI
:: =============================================================

title q-mini-wasm-v2-general Training Manager
cls

echo =============================================================
echo q-mini-wasm-v2-general Training System
echo =============================================================
echo.
echo WARNING: This CLI-based training script has been DEPRECATED.
echo.
echo In accordance with the Cognitive Ergonomics and Clifford Entanglement
echo AI Protocols, training is now performed NATIVELY using the Web UI.
echo.
echo The new training backend is fully C++/GO/DLL based and optimized 
echo for SYCL, eliminating the need for Python scripts.
echo.
echo =============================================================
echo NEXT STEPS:
echo 1. Ensure the Inference Server is running:
echo    cd .. ^&^& go run general_inference_server.go
echo 2. Open your web browser to: http://localhost:3486/
echo 3. Click the "Training" capability on the left sidebar
echo 4. Configure parameters and click "Start Native SYCL Training"
echo =============================================================
echo.

pause
