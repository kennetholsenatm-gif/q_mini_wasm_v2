import os
import subprocess
import pytest
from pathlib import Path

# Test the DevSecOps workflow script
def test_devsecops_script_exists():
    script_path = Path("scripts/devsecops-workflow.sh")
    assert script_path.exists(), "DevSecOps workflow script should exist"
    assert script_path.stat().st_mode & 0o111, "Script should be executable"

def test_script_syntax():
    result = subprocess.run(["bash", "-n", "scripts/devsecops-workflow.sh"], capture_output=True)
    assert result.returncode == 0, "Script should have valid syntax"

def test_script_content():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "#!/bin/bash" in content, "Script should have shebang"
        assert "set -e" in content, "Script should have error handling"
        assert "log_info()" in content, "Script should have logging functions"
        assert "check_dependencies()" in content, "Script should have dependency check"
        assert "setup_environment()" in content, "Script should have environment setup"
        assert "run_complete_workflow()" in content, "Script should have main workflow"

def test_script_permissions():
    script_path = Path("scripts/devsecops-workflow.sh")
    assert oct(script_path.stat().st_mode & 0o777) in ["0o755", "0o775"], "Script should have correct permissions"

def test_script_structure():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert content.count("function") >= 3, "Script should have multiple functions"
        assert content.count("log_") >= 4, "Script should have logging functions"
        assert content.count("Phase") >= 9, "Script should have workflow phases"

def test_script_security():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "bandit" in content, "Script should include Bandit security scan"
        assert "safety check" in content, "Script should include Safety dependency scan"
        assert "semgrep" in content, "Script should include Semgrep code analysis"
        assert "trivy" in content, "Script should include Trivy container scan"
        assert "STIG" in content, "Script should include STIG compliance checks"

def test_script_workflow_phases():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        phases = [
            "Phase 1: Testing",
            "Phase 2: Security Scanning",
            "Phase 3: STIG Compliance Check",
            "Phase 4: Infrastructure Deployment",
            "Phase 5: Application Deployment",
            "Phase 6: Post-Deployment Testing",
            "Phase 7: Monitoring Setup",
            "Phase 8: Final Security Scan",
            "Phase 9: Compliance Report Generation"
        ]
        for phase in phases:
            assert phase in content, f"Script should include {phase}"

def test_script_logging():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "log_info" in content, "Script should have info logging"
        assert "log_success" in content, "Script should have success logging"
        assert "log_warning" in content, "Script should have warning logging"
        assert "log_error" in content, "Script should have error logging"

def test_script_error_handling():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "set -e" in content, "Script should exit on errors"
        assert "exit 1" in content, "Script should handle errors properly"

def test_script_color_codes():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "RED='\033[0;31m'" in content, "Script should have color codes"
        assert "GREEN='\033[0;32m'" in content, "Script should have color codes"
        assert "YELLOW='\033[1;33m'" in content, "Script should have color codes"
        assert "BLUE='\033[0;34m'" in content, "Script should have color codes"
        assert "NC='\033[0m'" in content, "Script should have color reset"

def test_script_main_function():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "main()" in content, "Script should have main function"
        assert "main \"\$@\"" in content, "Script should call main function"

def test_script_permissions_check():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "if [[ $EUID -eq 0 ]]" in content, "Script should check for root user"
        assert "This script should not be run as root" in content, "Script should warn about root user"

def test_script_dependency_check():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        dependencies = [
            "python3",
            "git",
            "docker",
            "docker-compose",
            "terraform",
            "pre-commit"
        ]
        for dep in dependencies:
            assert dep in content, f"Script should check for {dep} dependency"

def test_script_environment_setup():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "pip install --upgrade pip" in content, "Script should upgrade pip"
        assert "pip install -r requirements.txt" in content, "Script should install requirements"
        assert "pre-commit install" in content, "Script should install pre-commit hooks"

def test_script_compliance_reporting():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "compliance-report.md" in content, "Script should generate compliance report"
        assert "STIG Compliance Status" in content, "Script should include STIG status"
        assert "Security Configuration" in content, "Script should include security configuration"

def test_script_integration():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "pytest tests/integration/" in content, "Script should run integration tests"
        assert "docker-compose -f monitoring/docker-compose.yml" in content, "Script should setup monitoring"
        assert "terraform init" in content, "Script should initialize Terraform"
        assert "terraform apply -auto-approve" in content, "Script should apply Terraform"

def test_script_file_permissions():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "chmod 755" in content, "Script should set executable permissions"
        assert "chmod 644" in content, "Script should set read permissions"

def test_script_security_scans():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "bandit -r src/" in content, "Script should run Bandit scan"
        assert "safety check" in content, "Script should run Safety scan"
        assert "semgrep --config=auto ." in content, "Script should run Semgrep scan"
        assert "trivy:latest filesystem" in content, "Script should run Trivy scan"

def test_script_output_files():
    expected_files = [
        "stig-report.md",
        "compliance-report.md",
        "bandit-report.json",
        "bandit-report.xml",
        "safety-report.json",
        "semgrep-report.json",
        "trivy-report.json"
    ]
    for file in expected_files:
        assert file in open("scripts/devsecops-workflow.sh").read(), f"Script should generate {file}"

def test_script_exit_codes():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert "exit 1" in content, "Script should exit with error code on failure"
        assert "if [ $? -eq 0 ]" in content, "Script should check command success"

def test_script_comments():
    with open("scripts/devsecops-workflow.sh") as f:
        content = f.read()
        assert content.count("#") >= 20, "Script should have sufficient comments"
        assert "# Q-Mini-WASM DevSecOps Complete Workflow" in content, "Script should have header comment"
        assert "# Colors for output" in content, "Script should have color comment"
        assert "# Logging functions" in content, "Script should have logging comment"
        assert "# Check if running as root" in content, "Script should have root check comment"
        assert "# Check dependencies" in content, "Script should have dependencies comment"
        assert "# Setup environment" in content, "Script should have environment comment"
        assert "# Run complete workflow" in content, "Script should have workflow comment"
        assert "# Main function" in content, "Script should have main comment"
        assert "# Execute main function" in content, "Script should have execution comment"