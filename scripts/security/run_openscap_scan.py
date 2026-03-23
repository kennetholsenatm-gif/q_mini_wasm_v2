#!/usr/bin/env python3
"""
OpenSCAP security and compliance scan wrapper for Q-Mini-WASM.

Runs oscap xccdf eval against the local host or container, produces HTML report
and ARF XML, and exits non-zero if critical compliance violations are found
(e.g. kernel parameters or memory management relevant to Intel driver paging).

Usage:
  python scripts/security/run_openscap_scan.py [OPTIONS]
  python scripts/security/run_openscap_scan.py --content-path /usr/share/scap-security-guide --profile xccdf_org.ssgproject.content_profile_cis
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    import click
except ImportError:
    click = None

try:
    from loguru import logger
except ImportError:
    import logging

    logging.basicConfig(level=logging.INFO)
    logger = logging.getLogger(__name__)


# Namespaces in ARF/XCCDF output
NS = {
    "arf": "http://scap.nist.gov/schema/asset-reporting-format/1.1",
    "xccdf": "http://checklists.nist.gov/xccdf/1.2",
    "cvss": "http://scap.nist.gov/schema/cvss-v2/0.2",
}

# Keywords that indicate kernel/memory rules (critical for Intel driver paging)
KERNEL_MEMORY_KEYWORDS = re.compile(
    r"\b(kernel|sysctl|memory|vm\.|mmap|paging|swap|ptrace|execmem)\b",
    re.IGNORECASE,
)


def find_oscap() -> str | None:
    """Return path to oscap binary or None if not found."""
    import shutil

    return shutil.which("oscap")


def find_scap_content(content_path: str | None) -> Path | None:
    """Locate a usable XCCDF datastream. Returns Path or None."""
    candidates = []
    if content_path and os.path.isdir(content_path):
        base = Path(content_path)
        for ext in ("-ds.xml", ".xml"):
            for f in base.rglob(f"*{ext}"):
                if "xccdf" in f.name.lower() or "ds" in f.name:
                    candidates.append(f)
    if not candidates:
        for d in ("/usr/share/scap-security-guide", "/usr/share/xml/scap/ssg/content"):
            if os.path.isdir(d):
                for f in Path(d).rglob("*-ds.xml"):
                    candidates.append(f)
    for p in sorted(candidates, key=lambda x: (len(x.name), x.name)):
        try:
            tree = ET.parse(p)
            root = tree.getroot()
            if root.tag.endswith("Benchmark") or "xccdf" in root.tag or "data-stream" in root.tag:
                return p
        except ET.ParseError:
            continue
    return candidates[0] if candidates else None


def run_oscap(
    datastream: Path,
    profile: str,
    results_arf: Path,
    report_html: Path,
) -> int:
    """Run oscap xccdf eval. Returns oscap exit code (0 = pass)."""
    cmd = [
        "oscap",
        "xccdf",
        "eval",
        "--profile",
        profile,
        "--results",
        str(results_arf),
        "--report",
        str(report_html),
        str(datastream),
    ]
    logger.info("Running: {}", " ".join(cmd))
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=600,
        )
        if result.stdout:
            logger.debug("oscap stdout: {}", result.stdout[:500])
        if result.returncode != 0 and result.stderr:
            logger.warning("oscap stderr: {}", result.stderr[:500])
        return result.returncode
    except FileNotFoundError:
        logger.error(
            "oscap binary not found. Install OpenSCAP (e.g. libopenscap8, openscap-utils) and SCAP content."
        )
        return 2
    except subprocess.TimeoutExpired:
        logger.error("oscap scan timed out (600s)")
        return 3


def parse_arf_for_critical_failures(arf_path: Path) -> list[dict]:
    """Parse ARF XML and return list of failed rules that are critical (kernel/memory or high severity)."""
    failures = []
    try:
        tree = ET.parse(arf_path)
        root = tree.getroot()
    except (ET.ParseError, OSError) as e:
        logger.warning("Could not parse ARF file {}: {}", arf_path, e)
        return failures

    # ARF contains report-request with results; XCCDF result is often under report/content
    for elem in root.iter():
        if elem.tag.endswith("rule-result") or "rule-result" in elem.tag:
            result_attr = elem.get("result")
            if result_attr and result_attr.lower() not in (
                "pass",
                "notselected",
                "notapplicable",
                "informational",
            ):
                rule_id = elem.get("id", "")
                severity = ""
                title = ""
                for child in elem:
                    if (
                        child.tag.endswith("result")
                        and child.text
                        and child.text.strip().lower() == "fail"
                    ):
                        pass
                    if child.tag.endswith("severity"):
                        severity = (child.text or "").strip().lower()
                    if child.tag.endswith("title") or "title" in (child.tag or ""):
                        title = (child.text or "").strip()
                if result_attr.lower() == "fail":
                    is_critical = (
                        severity in ("high", "critical", "important")
                        or bool(KERNEL_MEMORY_KEYWORDS.search(rule_id))
                        or bool(KERNEL_MEMORY_KEYWORDS.search(title))
                    )
                    failures.append(
                        {
                            "id": rule_id,
                            "result": result_attr,
                            "severity": severity,
                            "title": title[:200] if title else "",
                            "critical": is_critical,
                        }
                    )
    return failures


def main_cli(
    content_path: str | None = None,
    profile: str = "xccdf_org.ssgproject.content_profile_cis",
    results_arf: str = "openscap_results.arf.xml",
    report_html: str = "openscap_report.html",
    fail_on_critical: bool = True,
) -> int:
    """Main entry: find content, run scan, parse ARF, exit non-zero if required."""
    content_path = content_path or os.environ.get("OSCAP_CONTENT_PATH")
    ds = find_scap_content(content_path)
    if not ds:
        logger.error(
            "No SCAP datastream found. Set OSCAP_CONTENT_PATH or install scap-security-guide (e.g. in docker/Dockerfile.inference)."
        )
        return 1

    results_arf_path = Path(results_arf)
    report_html_path = Path(report_html)

    exit_code = run_oscap(ds, profile, results_arf_path, report_html_path)
    if exit_code != 0:
        logger.warning("oscap returned exit code {}", exit_code)

    critical_failures = []
    if results_arf_path.exists():
        critical_failures = [
            f for f in parse_arf_for_critical_failures(results_arf_path) if f.get("critical")
        ]
        if critical_failures:
            logger.error("Critical or kernel/memory-related failures ({}):", len(critical_failures))
            for f in critical_failures[:10]:
                logger.error("  {} | {} | {}", f["id"], f["severity"], (f["title"] or "")[:80])

    if report_html_path.exists():
        logger.info("HTML report written to {}", report_html_path.resolve())

    if fail_on_critical and critical_failures:
        return 4
    if exit_code != 0 and not critical_failures:
        return exit_code
    return 0


if click is not None:

    @click.command()
    @click.option(
        "--content-path",
        envvar="OSCAP_CONTENT_PATH",
        type=click.Path(exists=True, file_okay=False),
        help="Directory containing SCAP datastreams (e.g. scap-security-guide).",
    )
    @click.option(
        "--profile",
        default="xccdf_org.ssgproject.content_profile_cis",
        help="XCCDF profile to evaluate (e.g. cis, stig).",
    )
    @click.option("--results-arf", default="openscap_results.arf.xml", help="Output ARF XML path.")
    @click.option("--report-html", default="openscap_report.html", help="Output HTML report path.")
    @click.option(
        "--fail-on-critical/--no-fail-on-critical",
        default=True,
        help="Exit non-zero on critical/kernel failures.",
    )
    def cli(content_path, profile, results_arf, report_html, fail_on_critical):
        """Run OpenSCAP XCCDF evaluation and optionally fail on critical violations."""
        sys.exit(main_cli(content_path, profile, results_arf, report_html, fail_on_critical))

else:

    def cli():
        import argparse

        p = argparse.ArgumentParser(description="Run OpenSCAP XCCDF evaluation.")
        p.add_argument(
            "--content-path",
            default=os.environ.get("OSCAP_CONTENT_PATH"),
            help="SCAP content directory",
        )
        p.add_argument(
            "--profile", default="xccdf_org.ssgproject.content_profile_cis", help="XCCDF profile"
        )
        p.add_argument("--results-arf", default="openscap_results.arf.xml", help="ARF output path")
        p.add_argument("--report-html", default="openscap_report.html", help="HTML report path")
        p.add_argument(
            "--no-fail-on-critical",
            action="store_true",
            help="Do not exit non-zero on critical failures",
        )
        args = p.parse_args()
        sys.exit(
            main_cli(
                args.content_path,
                args.profile,
                args.results_arf,
                args.report_html,
                fail_on_critical=not args.no_fail_on_critical,
            )
        )


if __name__ == "__main__":
    if click is not None:
        cli()
    else:
        cli()
