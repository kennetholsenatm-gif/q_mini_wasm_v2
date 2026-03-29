# Quantum sidecar (optional)

Qiskit and PennyLane have no practical pure Go or C++ replacement in-tree. When quantum-assisted routing is required, run a **small Python service** here (or in your own image) and point the rest of the stack at it over HTTP or gRPC.

## Contract (directional)

- The main repo keeps **training and inference** on Go + C++.
- This sidecar holds **only** quantum SDK dependencies (`qiskit`, `pennylane`, …).
- Configure the host or WUI to call the sidecar when `quantum_backend` policies require it (wiring is deployment-specific).

## Layout

- Add your FastAPI or gRPC entrypoint under this directory.
- Pin versions in `requirements.txt` next to this README when you add code.

No Python is required for default native training or for the Go inference stub.
