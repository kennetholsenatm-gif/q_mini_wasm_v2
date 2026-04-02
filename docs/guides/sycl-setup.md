# SYCL Setup Guide

## Intel oneAPI

1. Download Intel oneAPI Base Toolkit from intel.com
2. Install with default options
3. Source the environment:

```bash
source /opt/intel/oneapi/setvars.sh
```

## Build with SYCL

```bash
cmake -DUSE_SYCL=ON -DCMAKE_CXX_COMPILER=icpx ..
cmake --build . -j$(nproc)
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