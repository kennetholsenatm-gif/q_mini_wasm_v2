"""Test the DevSecOps workflow (PowerShell script)."""

import subprocess
import pytest
from pathlib import Path

SCRIPT_PATH = Path("scripts/devsecops-workflow.ps1")


def test_devsecops_script_exists():
    assert SCRIPT_PATH.exists(), "DevSecOps workflow script (PowerShell) should exist"


def test_script_content():
    content = SCRIPT_PATH.read_text()
    assert "Log-Info" in content, "Script should have logging functions"
    assert "Check-Dependencies" in content, "Script should have dependency check"
    assert "Setup-Environment" in content, "Script should have environment setup"
    assert "Run-CompleteWorkflow" in content, "Script should have main workflow"


def test_script_structure():
    content = SCRIPT_PATH.read_text()
    assert content.count("function ") >= 3, "Script should have multiple functions"
    assert content.count("Log-") >= 4, "Script should have logging functions"
    assert content.count("Phase ") >= 9, "Script should have workflow phases"


def test_script_security():
    content = SCRIPT_PATH.read_text()
    assert "bandit" in content, "Script should include Bandit security scan"
    assert "safety check" in content, "Script should include Safety dependency scan"
    assert "semgrep" in content, "Script should include Semgrep code analysis"
    assert "trivy" in content.lower(), "Script should include Trivy scan"
    assert "STIG" in content, "Script should include STIG compliance checks"


def test_script_workflow_phases():
    content = SCRIPT_PATH.read_text()
    phases = [
        "Phase 1: Testing",
        "Phase 2: Security Scanning",
        "Phase 3: STIG Compliance Check",
        "Phase 4: Infrastructure Deployment",
        "Phase 5: Application Deployment",
        "Phase 6: Post-Deployment Testing",
        "Phase 7: Monitoring Setup",
        "Phase 8: Final Security Scan",
        "Phase 9: Compliance Report Generation",
    ]
    for phase in phases:
        assert phase in content, f"Script should include {phase}"


def test_script_logging():
    content = SCRIPT_PATH.read_text()
    assert "Log-Info" in content, "Script should have info logging"
    assert "Log-Success" in content, "Script should have success logging"
    assert "Log-Warning" in content, "Script should have warning logging"
    assert "Log-Error" in content, "Script should have error logging"


def test_script_error_handling():
    content = SCRIPT_PATH.read_text()
    assert "exit 1" in content, "Script should handle errors properly"
    assert (
        "Administrator" in content or "admin" in content.lower()
    ), "Script should check for admin run"


def test_script_main_function():
    content = SCRIPT_PATH.read_text()
    assert (
        "function Main" in content or "function Main " in content
    ), "Script should have main function"
    assert "Run-CompleteWorkflow" in content, "Script should call workflow"


def test_script_dependency_check():
    content = SCRIPT_PATH.read_text()
    dependencies = ["python3", "git", "docker", "docker-compose", "pre-commit", "requirements.txt"]
    for dep in dependencies:
        assert dep in content, f"Script should check for {dep} dependency"


def test_script_environment_setup():
    content = SCRIPT_PATH.read_text()
    assert "pip" in content and "requirements.txt" in content, "Script should install requirements"
    assert "pre-commit" in content or "pre_commit" in content, "Script should reference pre-commit"


def test_script_compliance_reporting():
    content = SCRIPT_PATH.read_text()
    assert "compliance-report.md" in content, "Script should generate compliance report"
    assert "STIG Compliance Status" in content, "Script should include STIG status"
    assert "Security Configuration" in content, "Script should include security configuration"


def test_script_security_scans():
    content = SCRIPT_PATH.read_text()
    assert "bandit" in content and "qminiwasm" in content, "Script should run Bandit on qminiwasm"
    assert "safety check" in content, "Script should run Safety scan"
    assert "semgrep" in content, "Script should run Semgrep scan"
    assert "trivy" in content.lower(), "Script should run Trivy scan"


def test_script_output_files():
    content = SCRIPT_PATH.read_text()
    expected = [
        "stig-report.md",
        "compliance-report.md",
        "security-report.md",
        "bandit_report",
        "safety_report",
        "semgrep_report",
    ]
    for name in expected:
        assert name in content, f"Script should generate or reference {name}"


def test_script_exit_codes():
    content = SCRIPT_PATH.read_text()
    assert "exit 1" in content, "Script should exit with error code on failure"


def test_script_header_comment():
    content = SCRIPT_PATH.read_text()
    assert "Q-Mini-WASM" in content and "DevSecOps" in content, "Script should have header comment"


def test_script_help_option():
    """Script should support --help (skipped if pwsh not available)."""
    import shutil

    pwsh = shutil.which("pwsh") or shutil.which("powershell")
    if not pwsh:
        pytest.skip("pwsh/powershell not found")
    result = subprocess.run(
        [pwsh, "-NoProfile", "-File", str(SCRIPT_PATH), "--help"],
        capture_output=True,
        text=True,
        timeout=10,
    )
    assert result.returncode == 0, "Script --help should succeed"
    assert "Usage" in result.stdout or "help" in result.stdout.lower()
