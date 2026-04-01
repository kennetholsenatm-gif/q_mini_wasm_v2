/* ============================================================
   QMiniWASM Training WUI — Mermaid Pipeline Visualizer
   ============================================================
   Training pipeline node graph using Mermaid.JS.
   Cognitive: holistic scan for experts, topological
   visualization of training flow.
   ============================================================ */

const QMW = window.QMW || {};

QMW.MermaidPipeline = function (container_id) {
  this.container = document.getElementById(container_id);
  this.current_config = null;
};

QMW.MermaidPipeline.prototype.render = function (config) {
  if (!this.container) return;
  this.current_config = config || {};

  const diagram = this._build_diagram(this.current_config);

  if (window.mermaid) {
    try {
      const id = "pipeline_" + Date.now();
      window.mermaid.render(id, diagram, function (svg_code) {
        this.container.innerHTML = svg_code;
      }.bind(this));
    } catch (e) {
      this.container.innerHTML =
        '<div class="hint">Mermaid render error: ' +
        QMW.escape_html(String(e.message || e)) +
        "</div>";
    }
  } else {
    // Fallback: render as preformatted text
    this.container.innerHTML =
      '<pre class="log-terminal" style="max-height:300px;overflow:auto">' +
      QMW.escape_html(diagram) +
      "</pre>";
  }
};

QMW.MermaidPipeline.prototype._build_diagram = function (cfg) {
  const d_model = cfg.d_model || 4096;
  const io_dim = cfg.io_d_model || 4096;
  const blocks = cfg.num_ternary_blocks || 1;
  const source = cfg.data_source || "mesh";
  const has_teacher = cfg.has_teacher || false;
  const sa_enabled = cfg.sa_enabled || false;
  const mopd_enabled = cfg.mopd_enabled || false;
  const cispo_enabled = cfg.cispo_enabled || false;

  let diag = "graph LR\n";

  // Data source
  diag += '  DS["Data\\n' + source + '"]\n';

  // Encoder
  diag += '  ENC["Encoder\\n' + io_dim + ' -> ' + d_model + '"]\n';

  // Ternary stack with optional SA
  if (sa_enabled) {
    diag += '  SA["SA Quantization\\nT: simulated annealing"]\n';
  }
  diag += '  TB["Ternary Stack\\n' + blocks + " blocks x " + d_model + '"]\n';

  // Head
  diag += '  HEAD["Head\\n' + d_model + " -> " + io_dim + '"]\n';

  // Teacher (if enabled)
  if (has_teacher) {
    diag += '  TCH["Teacher FP32\\n' + d_model + ' frozen"]\n';
    if (mopd_enabled) {
      diag += '  MOPD["MOPD\\nreverse-KL distillation"]\n';
    }
  }

  // Loss
  diag += '  LOSS["Loss"]\n';

  // RL policy (if CISPO)
  if (cispo_enabled) {
    diag += '  RL["Cascade RL\\nCISPO policy"]\n';
  }

  // Edges
  diag += "  DS --> ENC\n";
  if (sa_enabled) {
    diag += "  ENC --> SA\n";
    diag += "  SA --> TB\n";
  } else {
    diag += "  ENC --> TB\n";
  }
  diag += "  TB --> HEAD\n";
  diag += "  HEAD --> LOSS\n";

  if (has_teacher) {
    if (mopd_enabled) {
      diag += "  TCH -.-> MOPD\n";
      diag += "  TB -.-> MOPD\n";
      diag += "  MOPD --> LOSS\n";
    } else {
      diag += "  TCH -.-> TB\n";
    }
  }

  if (cispo_enabled) {
    diag += "  RL --> LOSS\n";
  }

  // Styles
  diag += '  style TB fill:#1e3d5c,stroke:#3a7ab8,color:#f0f0f0\n';
  diag += '  style ENC fill:#2d2d2d,stroke:#3a3a3a,color:#f0f0f0\n';
  diag += '  style HEAD fill:#2d2d2d,stroke:#3a3a3a,color:#f0f0f0\n';
  diag += '  style LOSS fill:#3a1e1e,stroke:#c43518,color:#f0f0f0\n';
  if (has_teacher) {
    diag += '  style TCH fill:#1e3d2e,stroke:#3a8f52,color:#f0f0f0\n';
    if (mopd_enabled) {
      diag += '  style MOPD fill:#1e3d2e,stroke:#3a8f52,color:#f0f0f0\n';
    }
  }
  if (sa_enabled) {
    diag += '  style SA fill:#3a2d1e,stroke:#c48600,color:#f0f0f0\n';
  }
  if (cispo_enabled) {
    diag += '  style RL fill:#2d1e3a,stroke:#8b5cf6,color:#f0f0f0\n';
  }

  return diag;
};

QMW.MermaidPipeline.prototype.update_phase = function (phase) {
  if (!this.container) return;

  // Add a phase indicator below the diagram
  let indicator = this.container.querySelector(".phase-indicator");
  if (!indicator) {
    indicator = document.createElement("div");
    indicator.className = "phase-indicator";
    indicator.style.cssText =
      "margin-top:var(--space-2);font-size:var(--text-sm);color:var(--text-secondary)";
    this.container.appendChild(indicator);
  }

  const labels = {
    teacher: "Phase 1: Teacher SFT",
    quantize: "Phase 2: SA Quantization",
    distill: "Phase 3: MOPD Distillation",
    route: "Phase 4: CISPO Routing",
  };

  indicator.innerHTML =
    '<span class="badge badge--ok">' +
    QMW.escape_html(labels[phase] || "Unknown phase") +
    "</span>";
};

window.QMW = QMW;
export default QMW.MermaidPipeline;