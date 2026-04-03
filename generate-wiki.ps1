# Wiki Generator for q_mini_wasm_v2
# This script generates wiki files from docs/ directory

$WIKI_OUTPUT = "wiki-output"

Write-Host "Generating wiki from docs/..."

# Create output directory
if (Test-Path $WIKI_OUTPUT) {
    Remove-Item -Recurse -Force $WIKI_OUTPUT
}
New-Item -ItemType Directory -Path $WIKI_OUTPUT | Out-Null

# Copy Home.md from README
Copy-Item "README.md" "$WIKI_OUTPUT/Home.md"

# Create Sidebar
@"
* [Home](Home.md)
**Architecture**
  * [Overview](Architecture-Overview.md)
  * [Ternary State Space](Architecture-Ternary-State-Space.md)
  * [Stabilizer Tableau](Architecture-Stabilizer-Tableau.md)
  * [MoE Routing](Architecture-MoE-Routing.md)
  * [Forward-Forward](Architecture-Forward-Forward.md)
  * [SYCL Acceleration](Architecture-SYCL-Acceleration.md)
**API Reference**
  * [Core API](API-Core-Reference.md)
**Guides**
  * [Building](Guides-Building.md)
  * [SYCL Setup](Guides-SYCL-Setup.md)
  * [Contributing](Guides-Contributing.md)
**Research**
  * [Cognitive Ergonomics](Research-Cognitive-Ergonomics.md)
  * [Clifford Entanglement](Research-Clifford-Entanglement.md)
  * [Framework Synthesis](Research-Framework-Synthesis.md)
**Decisions**
  * [ADR-001: Ternary](Decisions-ADR-001-Ternary.md)
"@ | Out-File -FilePath "$WIKI_OUTPUT/_Sidebar.md" -Encoding UTF8

# Copy architecture docs
Copy-Item "docs/architecture/overview.md" "$WIKI_OUTPUT/Architecture-Overview.md"
Copy-Item "docs/architecture/ternary-state-space.md" "$WIKI_OUTPUT/Architecture-Ternary-State-Space.md"
Copy-Item "docs/architecture/stabilizer-tableau.md" "$WIKI_OUTPUT/Architecture-Stabilizer-Tableau.md"
Copy-Item "docs/architecture/moe-routing.md" "$WIKI_OUTPUT/Architecture-MoE-Routing.md"
Copy-Item "docs/architecture/forward-forward.md" "$WIKI_OUTPUT/Architecture-Forward-Forward.md"
Copy-Item "docs/architecture/sycl-acceleration.md" "$WIKI_OUTPUT/Architecture-SYCL-Acceleration.md"

# Copy API docs
Copy-Item "docs/api/core-reference.md" "$WIKI_OUTPUT/API-Core-Reference.md"

# Copy guides
Copy-Item "docs/guides/building.md" "$WIKI_OUTPUT/Guides-Building.md"
Copy-Item "docs/guides/sycl-setup.md" "$WIKI_OUTPUT/Guides-SYCL-Setup.md"
Copy-Item "docs/guides/contributing.md" "$WIKI_OUTPUT/Guides-Contributing.md"

# Copy research papers
Copy-Item "docs/research/Cognitive Ergonomics Model Protocol Research.md" "$WIKI_OUTPUT/Research-Cognitive-Ergonomics.md"
Copy-Item "docs/research/Enhancing Framework with Clifford Entanglement.md" "$WIKI_OUTPUT/Research-Clifford-Entanglement.md"
Copy-Item "docs/research/QMINIWASM_ Quantum-Classical Framework Synthesis.md" "$WIKI_OUTPUT/Research-Framework-Synthesis.md"

# Copy decisions
Copy-Item "docs/decisions/adr-001-ternary-over-binary.md" "$WIKI_OUTPUT/Decisions-ADR-001-Ternary.md"

Write-Host "Wiki generated: $WIKI_OUTPUT directory"
Write-Host "Files created:"
Get-ChildItem $WIKI_OUTPUT | ForEach-Object { Write-Host "  - $($_.Name)" }