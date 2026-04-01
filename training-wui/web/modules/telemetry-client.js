/* ============================================================
   QMiniWASM Training WUI — Telemetry Client
   ============================================================
   WebSocket integration with Go backend telemetry stream.
   Connects to /api/runs/:id/ws and dispatches events
   for charts, metrics tables, and log updates.
   
   Cognitive: chunked telemetry display (3 metrics default),
   pre-attentive color coding for trends.
   ============================================================ */

const QMW = window.QMW || {};

QMW.TelemetryClient = function () {
  this.ws = null;
  this.run_id = null;
  this.charts = {};
  this.metric_count = 0;
  this.log_trend_state = {};
  
  // Metric classification
  this.LOG_LOWER_BETTER = new Set([
    "loss", "train_loss", "training_loss", "val_loss",
    "validation_loss", "eval_loss", "mse", "rmse", "mae",
    "nll", "perplexity", "ppl"
  ]);
  this.LOG_HIGHER_BETTER = new Set([
    "accuracy", "acc", "train_acc", "val_acc",
    "f1", "f1_score", "precision", "recall", "bleu", "auc"
  ]);
  this.LOG_NO_TREND = new Set(["lr", "learning_rate", "epoch"]);
  this.LOG_EPS = 1e-9;
};

QMW.TelemetryClient.prototype.init_charts = function () {
  if (typeof Chart === "undefined") return;

  const mseEl = document.getElementById("chartMSE");
  const retEl = document.getElementById("chartReturn");

  if (mseEl) {
    this.charts.mse = new Chart(mseEl.getContext("2d"), {
      type: "line",
      data: {
        labels: [],
        datasets: [{
          label: "Train MSE",
          data: [],
          borderColor: "#3b82f6",
          tension: 0.2,
          pointRadius: 2,
        }],
      },
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { labels: { color: "#f0f0f0" } } },
        scales: {
          x: { ticks: { color: "#9ca3af" }, grid: { color: "#2d2d2d" } },
          y: { ticks: { color: "#9ca3af" }, grid: { color: "#2d2d2d" } },
        },
      },
    });
  }

  if (retEl) {
    this.charts.ret = new Chart(retEl.getContext("2d"), {
      type: "line",
      data: {
        labels: [],
        datasets: [{
          label: "Mean Return",
          data: [],
          borderColor: "#10b981",
          tension: 0.2,
          pointRadius: 2,
        }],
      },
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        plugins: { legend: { labels: { color: "#f0f0f0" } } },
        scales: {
          x: { ticks: { color: "#9ca3af" }, grid: { color: "#2d2d2d" } },
          y: { ticks: { color: "#9ca3af" }, grid: { color: "#2d2d2d" } },
        },
      },
    });
  }
};

QMW.TelemetryClient.prototype.reset_charts = function () {
  for (const c of Object.values(this.charts)) {
    if (!c) continue;
    c.data.labels = [];
    c.data.datasets[0].data = [];
    c.update("none");
  }
  this.metric_count = 0;
  this.log_trend_state = {};
};

QMW.TelemetryClient.prototype.connect = function (run_id) {
  this.disconnect();
  if (!run_id) return;
  this.run_id = run_id;
  this.reset_charts();

  const status = document.getElementById("telemetryStatus");
  if (status) {
    status.textContent = "Connecting telemetry WebSocket...";
    status.className = "hint";
  }

  const proto = location.protocol === "https:" ? "wss" : "ws";
  const path = proto + "://" + location.host + "/api/runs/" + encodeURIComponent(run_id) + "/ws";
  const url = QMW.ws_url_with_token(path);

  this.ws = new WebSocket(url);
  const self = this;

  this.ws.onmessage = function (ev) {
    let msg = null;
    try { msg = JSON.parse(ev.data); } catch (_) { return; }
    if (!msg || String(msg.run_id || "") !== String(run_id)) return;
    self._handle_message(msg);
  };

  this.ws.onopen = function () {
    if (status) {
      status.textContent = "Receiving telemetry...";
      status.className = "hint";
    }
  };

  this.ws.onerror = function () {
    if (status) {
      status.textContent = "WebSocket error — check auth and network.";
      status.className = "hint";
    }
  };

  this.ws.onclose = function () {
    self.ws = null;
    if (status && self.metric_count === 0) {
      status.textContent = "Telemetry disconnected before any events.";
      status.className = "hint";
    }
  };
};

QMW.TelemetryClient.prototype.disconnect = function () {
  if (this.ws) {
    this.ws.close();
    this.ws = null;
  }
  this.run_id = null;
};

QMW.TelemetryClient.prototype._handle_message = function (msg) {
  if (msg.type === "metric") {
    this._push_metric(msg);
  } else if (msg.type === "train_throughput") {
    this._append_throughput_row(msg);
  } else if (msg.type === "lifecycle") {
    this._append_lifecycle_row(msg);
  }
};

QMW.TelemetryClient.prototype._push_metric = function (metric) {
  this.metric_count++;

  const status = document.getElementById("telemetryStatus");
  if (status) {
    status.textContent = "Receiving telemetry (" + this.metric_count + " points)";
    status.className = "hint";
  }

  // Update summary metrics (3-column chunk)
  const mseEl = document.getElementById("metricMSE");
  const retEl = document.getElementById("metricReturn");
  const lrEl = document.getElementById("metricLR");
  if (mseEl && metric.mean_mse != null) mseEl.textContent = String(metric.mean_mse);
  if (retEl && metric.mean_return != null) retEl.textContent = String(metric.mean_return);
  if (lrEl && metric.learning_rate != null) {
    const lr = Number(metric.learning_rate);
    lrEl.textContent = Number.isFinite(lr) ? lr.toExponential(2) : String(metric.learning_rate);
  }

  // Update charts
  const label = String(metric.epoch ?? "");
  if (this.charts.mse && metric.mean_mse != null) {
    this.charts.mse.data.labels.push(label);
    this.charts.mse.data.datasets[0].data.push(Number(metric.mean_mse));
    if (this.charts.mse.data.labels.length > 200) {
      this.charts.mse.data.labels.shift();
      this.charts.mse.data.datasets[0].data.shift();
    }
    this.charts.mse.update("none");
  }
  if (this.charts.ret && metric.mean_return != null) {
    this.charts.ret.data.labels.push(label);
    this.charts.ret.data.datasets[0].data.push(Number(metric.mean_return));
    if (this.charts.ret.data.labels.length > 200) {
      this.charts.ret.data.labels.shift();
      this.charts.ret.data.datasets[0].data.shift();
    }
    this.charts.ret.update("none");
  }

  // Update metrics table
  this._append_metric_row(metric);

  // Update sticky bar
  const phase = document.getElementById("stickyPhaseLabel");
  if (phase && metric.stage) {
    const bits = [metric.stage, metric.event_type].filter(Boolean);
    phase.textContent = bits.join(" · ");
  }

  // Dispatch event for other modules
  QMW.bus.emit("telemetry-metric", metric);
};

QMW.TelemetryClient.prototype._append_metric_row = function (metric) {
  const tb = document.getElementById("telemetryMetricBody");
  if (!tb) return;

  const tr = document.createElement("tr");
  const src = metric.telemetry_source || "Python";
  const pillClass = src === "Source: gRPC C++" ? "source-pill source-pill--cpp" : "source-pill source-pill--py";

  const ep = metric.epoch != null ? String(Number(metric.epoch) + 1) : "";
  const mse = metric.mean_mse != null ? String(metric.mean_mse) : "—";
  const ret = metric.mean_return != null ? String(metric.mean_return) : "—";
  const lr = metric.learning_rate != null ? Number(metric.learning_rate).toExponential(1) : "—";
  const sps = metric.samples_per_second != null ? Number(metric.samples_per_second).toFixed(1) : "—";
  const stage = metric.stage || metric.event_type || "—";

  tr.innerHTML =
    "<td>" + QMW.escape_html(ep) + "</td>" +
    "<td>" + QMW.escape_html(mse) + "</td>" +
    "<td>" + QMW.escape_html(ret) + "</td>" +
    "<td>" + QMW.escape_html(lr) + "</td>" +
    "<td>" + QMW.escape_html(sps) + "</td>" +
    "<td>" + QMW.escape_html(stage) + "</td>" +
    '<td><span class="' + pillClass + '">' + QMW.escape_html(src) + "</span></td>";

  tb.insertBefore(tr, tb.firstChild);
  while (tb.rows.length > 200) tb.removeChild(tb.lastChild);
};

QMW.TelemetryClient.prototype._append_throughput_row = function (msg) {
  QMW.bus.emit("telemetry-throughput", msg);
};

QMW.TelemetryClient.prototype._append_lifecycle_row = function (msg) {
  QMW.bus.emit("telemetry-lifecycle", msg);
};

QMW.TelemetryClient.prototype.get_trend_class = function (canonical, value) {
  if (this.LOG_NO_TREND.has(canonical)) {
    return { cls: "log-num-info", title: "Reference metric" };
  }
  const prev = this.log_trend_state[canonical];
  this.log_trend_state[canonical] = value;
  if (prev === undefined || prev === null || Number.isNaN(prev)) {
    return { cls: "log-num-new", title: "First value" };
  }
  const d = value - prev;
  if (Math.abs(d) < this.LOG_EPS) {
    return { cls: "log-num-same", title: "Unchanged" };
  }
  if (this.LOG_LOWER_BETTER.has(canonical)) {
    return d < 0
      ? { cls: "log-num-better", title: "Improved (lower)" }
      : { cls: "log-num-worse", title: "Worse (higher)" };
  }
  if (this.LOG_HIGHER_BETTER.has(canonical)) {
    return d > 0
      ? { cls: "log-num-better", title: "Improved (higher)" }
      : { cls: "log-num-worse", title: "Worse (lower)" };
  }
  this.log_trend_state[canonical] = prev;
  return { cls: "log-num-new", title: "" };
};

window.QMW = QMW;
export default QMW.TelemetryClient;