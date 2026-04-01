/* ============================================================
   QMiniWASM Training WUI — Panel Integration Modules
   ============================================================
   RunPod/Infra, Quantum, and Registry panels.
   Delegates to existing index.html training engine
   for all infrastructure management.
   ============================================================ */

const QMW = window.QMW || {};

QMW.InfraPanel = {
  init: function () {
    const btnRefresh = document.getElementById("btnInfraRefresh");
    if (btnRefresh) btnRefresh.onclick = () => QMW.api_fetch("/api/runpod/status").then(r => r.json()).then(d => this.render_status(d)).catch(e => QMW.add_toast(String(e.message || e), "error"));
  },
  render_status: function (data) {
    const rp = data.runpod || {};
    const badges = document.getElementById("infraBadges");
    if (!badges) return;
    badges.innerHTML = "";
    [["RUNPOD_TOKEN", !!rp.token_present], ["infra/runpod", !!rp.infra_dir_exists], ["OpenTofu", !!rp.tofu_usable]].forEach(([l, ok]) => {
      const s = document.createElement("span");
      s.className = "infra-badge " + (ok ? "ok" : "bad");
      s.textContent = l + ": " + (ok ? "OK" : "no");
      badges.appendChild(s);
    });
  }
};

QMW.QuantumPanel = {
  init: function () {
    const btnRefresh = document.getElementById("btnQuantumRefresh");
    if (btnRefresh) btnRefresh.onclick = () => QMW.api_fetch("/api/quantum/topology").then(r => r.json()).then(d => this.render_topology(d)).catch(e => QMW.add_toast(String(e.message || e), "error"));
  },
  render_topology: function (data) {
    const q = data.quantum_topology || {};
    const kv = document.getElementById("quantumTopologyKv");
    if (!kv) return;
    kv.innerHTML = "";
    [["Active run", q.run_id ? String(q.run_id).slice(0, 8) : "none"], ["Ternary pruning", q.pruned_from_nodes && q.pruned_to_nodes ? `${q.pruned_from_nodes} -> ${q.pruned_to_nodes}` : "not emitted yet"]].forEach(([k, v]) => {
      const dk = document.createElement("div"); dk.className = "k"; dk.textContent = k;
      const dv = document.createElement("div"); dv.className = "v"; dv.textContent = String(v);
      kv.appendChild(dk); kv.appendChild(dv);
    });
  }
};

QMW.RegistryPanel = {
  init: function () {
    const btnRefresh = document.getElementById("btnRegistryRefresh");
    if (btnRefresh) btnRefresh.onclick = () => QMW.api_fetch("/api/artifacts").then(r => r.json()).then(d => this.render_artifacts(d)).catch(e => QMW.add_toast(String(e.message || e), "error"));
  },
  render_artifacts: function (data) {
    const body = document.getElementById("registryBody");
    const msg = document.getElementById("registryMsg");
    if (!body) return;
    body.innerHTML = "";
    const rows = data.artifacts || [];
    rows.forEach((a) => {
      const tr = document.createElement("tr");
      tr.innerHTML = `<td style="padding:6px;border-bottom:1px solid var(--border-default)"><code>${QMW.escape_html(a.path || a.name || "")}</code></td><td style="padding:6px;border-bottom:1px solid var(--border-default);text-align:right">${QMW.fmt_bytes(a.size_bytes)}</td><td style="padding:6px;border-bottom:1px solid var(--border-default)">${QMW.escape_html(a.modified || "")}</td><td style="padding:6px;border-bottom:1px solid var(--border-default)"><a href="/api/artifacts/download?path=${encodeURIComponent(a.path)}">Download</a></td>`;
      body.appendChild(tr);
    });
    if (msg) { msg.textContent = `${rows.length} checkpoint artifact(s)`; msg.className = "hint"; }
  }
};

QMW.init_panels = function () {
  QMW.InfraPanel.init();
  QMW.QuantumPanel.init();
  QMW.RegistryPanel.init();
};

window.QMW = QMW;
export default QMW;