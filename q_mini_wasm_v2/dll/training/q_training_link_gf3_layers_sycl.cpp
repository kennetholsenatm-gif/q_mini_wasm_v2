// Same rationale as q_training_link_tableau_kernels.cpp: GF3 FF batched kernels must finalize in the
// q_training.dll link unit on Windows IntelLLVM; /WHOLEARCHIVE on core.lib is not sufficient for all SYCL kernels.
#include "../../sycl/gf3_layers_sycl.cpp"
