# SYCL Setup Guide

## Intel oneAPI

1. Download Intel oneAPI Base Toolkit from intel.com
2. Install with default options
3. Source the environment:

```bash
source /opt/intel/oneapi/setvars.sh
```

## Build with SYCL

From the **repository root** (after `source /opt/intel/oneapi/setvars.sh`):

```bash
cmake -S q_mini_wasm_v2 -B q_mini_wasm_v2/build_sycl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=icpx -DCMAKE_C_COMPILER=icx \
  -DBUILD_TESTS=ON
cmake --build q_mini_wasm_v2/build_sycl -j$(nproc)
```

## Verify SYCL

```bash
./q_mini_wasm_v2_tests --gtest_filter="*sycl*"
```

## Troubleshooting

| Issue | Solution |
|---|---|
| `IntelSYCL not found` | Source `setvars.sh` first |
| GPU not detected | Check `sycl-ls` output |
| Link errors | Ensure DPC++ runtime installed |