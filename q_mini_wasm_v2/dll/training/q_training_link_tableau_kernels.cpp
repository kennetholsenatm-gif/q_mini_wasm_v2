// Intel oneAPI DPC++: device code for SYCL kernels is finalized at the *last* link step.
// q_training.dll links q_mini_wasm_v2_core.lib with /WHOLEARCHIVE, but tableau offload blobs can
// still fail runtime registration ("No kernel named ..."). Compiling tableau_kernels.cpp again
// in this TU guarantees those kernels are part of the DLL link unit.
//
// Duplicate host symbols vs the archive are resolved with /FORCE:MULTIPLE on the DLL link line.
#include "../../sycl/tableau_kernels.cpp"
