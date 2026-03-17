from pathlib import Path

from setuptools import setup, find_packages

setup(
    name="llm-pract",
    version="0.1.0",
    description="Q-Mini-WASM: Quantum-Classical Hybrid AI Architecture",
    long_description=(Path(__file__).parent / "README.md").read_text(encoding="utf-8"),
    long_description_content_type="text/markdown",
    author="Q-Mini-WASM Team",
    author_email="team@q-mini-wasm.org",
    url="https://github.com/kennetholsenatm-gif/LLM_Pract",
    packages=find_packages(),
    package_data={
        "": ["*.txt", "*.md", "*.yml", "*.yaml", "*.json"],
    },
    install_requires=[
        "torch>=2.0.0",
        "pennylane>=0.28",
        "wasmtime>=14.0.0",
        "pywasm>=2.0.0",
        "numpy>=1.24.0",
        "scipy>=1.10.0",
        "networkx>=3.1.0",
        "sympy>=1.12.0",
        "pandas>=2.0.0",
        "matplotlib>=3.7.0",
        "scikit-learn>=1.3.0",
        "tqdm>=4.65.0",
        "rich>=13.7.0",
        "click>=8.1.0",
        "python-dotenv>=1.0.0",
        "loguru>=0.7.0",
        "pydantic>=2.5.0",
        "pydantic-settings>=2.1.0",
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
        "quantum": [
            "pennylane>=0.28",
            "pennylane-qiskit>=0.28",
        ],
        "wasm": [
            "wasmtime>=14.0.0",
            "pywasm>=2.0.0",
        ],
        "gpu": [
            "dpctl>=0.15.0",
        ],
        "arc": [
            "dpctl>=0.15.0",
        ],
        "intel-quantum": [],
        "cloud": [
            "requests>=2.31",
            "boto3>=1.28",
            "google-cloud>=1.33",
            "azure-core>=1.28",
        ],
        "security": [
            "bandit>=1.7.0",
            "safety>=2.3.0",
            "pip-audit>=2.6.0",
        ],
    },
    # Entry point reserved until qminiwasm.cli is implemented
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
    keywords="quantum machine learning AI wasm hybrid architecture",
    project_urls={
        "Documentation": "https://github.com/kennetholsenatm-gif/LLM_Pract",
        "Source": "https://github.com/kennetholsenatm-gif/LLM_Pract",
        "Tracker": "https://github.com/kennetholsenatm-gif/LLM_Pract/issues",
    },
)
