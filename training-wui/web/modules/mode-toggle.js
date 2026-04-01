/* ============================================================
   QMiniWASM Training WUI — Mode Toggle
   ============================================================
   Expert / Novice mode switching.
   Cognitive: respects expert/novice scanning asymmetry.
   - Novice: 5-step wizard (progressive disclosure)
   - Expert: TOML editor + schema introspection
   ============================================================ */

const QMW = window.QMW || {};

QMW.ModeToggle = function () {
  this.current_mode = "novice";
  this.storage_key = "qmw_interface_mode_v1";
  this._restore();
};

QMW.ModeToggle.prototype._restore = function () {
  try {
    const saved = localStorage.getItem(this.storage_key);
    if (saved === "expert" || saved === "novice") {
      this.current_mode = saved;
    }
  } catch (_) {}
};

QMW.ModeToggle.prototype.save = function () {
  try {
    localStorage.setItem(this.storage_key, this.current_mode);
  } catch (_) {}
};

QMW.ModeToggle.prototype.set_mode = function (mode) {
  if (mode !== "expert" && mode !== "novice") return;
  this.current_mode = mode;
  this.save();
  this._apply();
  QMW.bus.emit("mode-changed", { mode: mode });
};

QMW.ModeToggle.prototype.get_mode = function () {
  return this.current_mode;
};

QMW.ModeToggle.prototype._apply = function () {
  const is_expert = this.current_mode === "expert";

  // Toggle pill buttons
  document.querySelectorAll(".mode-toggle-pill").forEach(function (btn) {
    const m = btn.getAttribute("data-mode");
    btn.setAttribute("aria-pressed", m === this.current_mode ? "true" : "false");
  }.bind(this));

  // Show/hide wizard
  const wizard = document.getElementById("wizardSection");
  if (wizard) wizard.style.display = is_expert ? "none" : "block";

  // Show/hide expert editor
  const expert = document.getElementById("expertEditorSection");
  if (expert) expert.style.display = is_expert ? "block" : "none";
};

QMW.ModeToggle.prototype.render = function () {
  const div = document.createElement("div");
  div.className = "mode-toggle";
  div.setAttribute("role", "radiogroup");
  div.setAttribute("aria-label", "Interface mode");

  const novice_btn = document.createElement("button");
  novice_btn.className = "mode-toggle-pill";
  novice_btn.setAttribute("data-mode", "novice");
  novice_btn.textContent = "\uD83E\uDDED Wizard (Guided)";
  novice_btn.setAttribute("aria-pressed", this.current_mode === "novice" ? "true" : "false");

  const expert_btn = document.createElement("button");
  expert_btn.className = "mode-toggle-pill";
  expert_btn.setAttribute("data-mode", "expert");
  expert_btn.textContent = "\u26A1 Expert (TOML)";
  expert_btn.setAttribute("aria-pressed", this.current_mode === "expert" ? "true" : "false");

  const self = this;
  novice_btn.onclick = function () { self.set_mode("novice"); };
  expert_btn.onclick = function () { self.set_mode("expert"); };

  div.appendChild(novice_btn);
  div.appendChild(expert_btn);

  return div;
};

window.QMW = QMW;
export default QMW.ModeToggle;