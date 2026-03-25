# Packed ternary SYCL kernel (stub)

Build with Intel oneAPI (``icpx -fsycl``) when targeting Intel Arc / Xe.

- ``ternary_dot_stub.cpp`` — placeholder queue and USM buffers; extend with
  ``sycl::ext::intel::experimental::matrix`` or manual VNNI-friendly packing
  matching ``qminiwasm.enclave.trit_pack`` (MSB-first, five trits per byte).

### Python integration (optional CPU extension)

```bash
pip install -e ".[native]"
export QMINIWASM_BUILD_NATIVE=1   # Unix
# Windows PowerShell: $env:QMINIWASM_BUILD_NATIVE=1
pip install -e .
```

Editable installs may skip compiling the extension; if `is_native_available()` is false, run:

`python setup.py build_ext --inplace` with the same `QMINIWASM_BUILD_NATIVE` and a C++17 toolchain.

Import: `from qminiwasm.hardware.native_ternary import dot_u8_i8, is_native_available`.

SYCL / VNNI: extend `ternary_dot_stub.cpp` or call into this path from
`qminiwasm.hardware.sycl_hardware` when a GPU queue is available.
