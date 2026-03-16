"""Setup script for SYCL hardware extension"""

from setuptools import setup, Extension

# Check if dpctl is available
try:
    import dpctl

    dpctl_available = True
except ImportError:
    dpctl_available = False

# Define the extension module
ext_modules = []

if dpctl_available:
    ext_modules = [
        Extension(
            "qminiwasm.hardware.sycl.sycl_hardware",
            sources=["sycl_hardware.py"],
            include_dirs=dpctl.get_include() if hasattr(dpctl, "get_include") else None,
            libraries=dpctl.get_libraries() if hasattr(dpctl, "get_libraries") else None,
            library_dirs=dpctl.get_library_dirs() if hasattr(dpctl, "get_library_dirs") else None,
            extra_compile_args=(
                dpctl.get_compile_flags() if hasattr(dpctl, "get_compile_flags") else None
            ),
            extra_link_args=dpctl.get_link_flags() if hasattr(dpctl, "get_link_flags") else None,
        )
    ]

setup(
    name="qminiwasm-sycl",
    version="0.1.0",
    description="SYCL hardware acceleration for Q-Mini-WASM",
    author="Q-Mini-WASM Team",
    author_email="team@qminiwasm.org",
    packages=["qminiwasm.hardware.sycl"],
    ext_modules=ext_modules,
    install_requires=[
        "dpctl>=0.15.0",
        "numpy>=1.22.0",  # CVE: numpy < 1.22 incomplete string comparison in numpy.core
    ],
    python_requires=">=3.8",
)
