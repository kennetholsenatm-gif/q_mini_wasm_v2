import os
from pathlib import Path

from setuptools import find_packages, setup

_ext_modules = []
_cmdclass = {}
if os.environ.get("QMINIWASM_BUILD_NATIVE", "").strip().lower() in (
    "1",
    "true",
    "yes",
    "on",
):
    try:
        from pybind11.setup_helpers import Pybind11Extension, build_ext

        _ext_modules = [
            Pybind11Extension(
                "qminiwasm._native_ternary",
                ["ternary_packed/bindings.cpp"],
                cxx_std=17,
            ),
        ]
        _cmdclass["build_ext"] = build_ext
    except ImportError:
        print("WARNING: pybind11 not found; install pybind11 to build native ternary extension.")

_setup_kw = dict(
    name="llm-pract",
    version="0.1.0",
    description="Q-Mini-WASM: ternary-quantized models with local WASM execution",
    long_description=(Path(__file__).parent / "README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    author="Q-Mini-WASM Team",
    author_email="[REDACTED]",
    url="https://github.com/kennetholsenatm-gif/LLM_Pract",
    packages=find_packages(),
    package_data={
        "": ["*.txt", "*.md", "*.yml", "*.yaml", "*.json"],
    },
    install_requires=[
        "torch>=2.0.0",
        "pennylane>=0.28",
        "qiskit>=1.0",
        "qiskit-ibm-runtime>=0.20",
        "wasmtime>=14.0.0",
        "pywasm>=2.0.0",
        "numpy>=1.24.0",
        "scipy>=1.10.0",
        "cryptography>=46.0.5",
    ],
    extras_require={
        "dev": [
            "pytest>=7.4",
            "pytest-cov>=4.1",
            "bandit>=1.7",
            "black>=23.7.0",
            "flake8>=6.0.0",
            "mypy>=1.4.0",
            "mkdocs>=1.5.0",
            "mkdocs-material>=9.0.0",
        ],
        "serve": [
            "fastapi>=0.109.0",
            "uvicorn[standard]>=0.27.0",
            "pydantic>=2.5.0",
            "python-dotenv>=1.0.0",
        ],
        "wasm": [
            "wasmtime>=14.0.0",
            "pywasm>=2.0.0",
        ],
        "training": [
            "datasets>=2.14.0",
            "python-dotenv>=1.0.0",
            "pydantic>=2.5.0",
            "tomli>=2.0.0; python_version<'3.11'",
        ],
        "gpu": [
            "dpctl>=0.15.0",
        ],
        "arc": [
            "dpctl>=0.15.0",
        ],
        "native": [
            "pybind11>=2.11.0",
        ],
        "scripts": [
            "click>=8.1.0",
            "python-dotenv>=1.0.0",
            "loguru>=0.7.0",
        ],
        "security": [
            "bandit>=1.7.0",
            "safety>=2.3.0",
            "pip-audit>=2.6.0",
        ],
    },
    python_requires=">=3.8",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Science/Research",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
        "Topic :: Scientific/Engineering :: Quantum Computing",
        "Topic :: Software Development :: Build Tools",
    ],
    license="MIT",
    keywords="quantum machine learning wasm ternary quantization",
    project_urls={
        "Documentation": "https://github.com/kennetholsenatm-gif/LLM_Pract",
        "Source": "https://github.com/kennetholsenatm-gif/LLM_Pract",
        "Tracker": "https://github.com/kennetholsenatm-gif/LLM_Pract/issues",
    },
)
if _ext_modules:
    _setup_kw["ext_modules"] = _ext_modules
    _setup_kw["cmdclass"] = _cmdclass
setup(**_setup_kw)
