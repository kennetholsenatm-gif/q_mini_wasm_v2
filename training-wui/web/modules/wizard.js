/* ============================================================
   QMiniWASM Training WUI — Wizard Steps Integration
   ============================================================
   Populates wizardContent from existing training logic.
   Delegates to the original index.html training engine
   for actual configuration building and model facts.
   
   Cognitive: Miller's Law (5 steps max), progressive
   disclosure, event boundaries between steps.
   ============================================================ */

const QMW = window.QMW || {};

QMW.Wizard = function () {
  this.current_step = 1;
  this.total_steps = 5;
  this.step_ids = [
    "wizardStep1",
    "wizardStep2",
    "wizardStep3",
    "wizardStep4",
    "wizardStep5",
  ];
  this.step_titles = [
    "1 · Dataset mix",
    "2 · Model & device",
    "3 · Data source",
    "4 · Training params",
    "5 · Save, check, launch",
  ];
};

QMW.Wizard.prototype.init = function () {
  this._build_wizard_content();
  this._bind_nav();
  this._apply_step(1);
};

QMW.Wizard.prototype._build_wizard_content = function () {
  const container = document.getElementById("wizardContent");
  if (!container) return;

  container.innerHTML = `
    <div id="wizardStep1" class="wizard-pane">
      <h3>1 · Dataset Mix</h3>
      <p class="hint">Select Hugging Face datasets for training. This populates <code>data.path</code> and <code>huggingface.extra_specs</code> in the TOML.</p>
      <div id="wizardDatasetGrid"></div>
      <div class="wizard-nav">
        <button class="btn btn-ghost" id="wizardPrev1" disabled>Back</button>
        <button class="btn btn-primary" id="wizardNext1">Next: Model</button>
      </div>
    </div>
    <div id="wizardStep2" class="wizard-pane" hidden>
      <h3>2 · Model & Device</h3>
      <p class="hint">Configure model architecture and accelerator. This populates <code>[model]</code> and <code>hardware.accelerator</code>.</p>
      <div id="wizardModelForm"></div>
      <div class="wizard-nav">
        <button class="btn btn-ghost" id="wizardPrev2">Back</button>
        <button class="btn btn-primary" id="wizardNext2">Next: Data</button>
      </div>
    </div>
    <div id="wizardStep3" class="wizard-pane" hidden>
      <h3>3 · Data Source</h3>
      <p class="hint">Configure data source, paths, and HuggingFace settings. This populates <code>[data]</code> and <code>[huggingface]</code>.</p>
      <div id="wizardDataForm"></div>
      <div class="wizard-nav">
        <button class="btn btn-ghost" id="wizardPrev3">Back</button>
        <button class="btn btn-primary" id="wizardNext3">Next: Training</button>
      </div>
    </div>
    <div id="wizardStep4" class="wizard-pane" hidden>
      <h3>4 · Training Parameters</h3>
      <p class="hint">Set epochs, batch size, learning rate, and advanced options. This populates <code>[training]</code>.</p>
      <div id="wizardTrainForm"></div>
      <div class="wizard-nav">
        <button class="btn btn-ghost" id="wizardPrev4">Back</button>
        <button class="btn btn-primary" id="wizardNext4">Next: Launch</button>
      </div>
    </div>
    <div id="wizardStep5" class="wizard-pane" hidden>
      <h3>5 · Save & Launch</h3>
      <p class="hint">Review configuration, save to TOML, and launch training.</p>
      <div id="wizardReviewForm"></div>
      <div class="wizard-nav">
        <button class="btn btn-ghost" id="wizardPrev5">Back</button>
        <button class="btn btn-primary" id="wizardSave">Save working config</button>
        <button class="btn btn-primary" id="wizardLaunch">Launch training</button>
      </div>
    </div>
  `;
};

QMW.Wizard.prototype._bind_nav = function () {
  const self = this;

  for (let i = 1; i <= this.total_steps; i++) {
    const next_btn = document.getElementById("wizardNext" + i);
    if (next_btn) {
      next_btn.onclick = function () {
        self.go_to_step(i + 1);
      };
    }

    const prev_btn = document.getElementById("wizardPrev" + i);
    if (prev_btn) {
      prev_btn.onclick = function () {
        self.go_to_step(i - 1);
      };
    }
  }

  // Hook into existing navigation
  const prev_btn = document.getElementById("btnWizardPrev");
  const next_btn = document.getElementById("btnWizardNext");
  if (prev_btn) {
    prev_btn.onclick = function () {
      self.go_to_step(self.current_step - 1);
    };
  }
  if (next_btn) {
    next_btn.onclick = function () {
      self.go_to_step(self.current_step + 1);
    };
  }
};

QMW.Wizard.prototype.go_to_step = function (n) {
  if (n < 1 || n > this.total_steps) return;
  this._apply_step(n);
};

QMW.Wizard.prototype._apply_step = function (n) {
  this.current_step = n;

  // Hide all steps
  for (const id of this.step_ids) {
    const el = document.getElementById(id);
    if (el) el.hidden = true;
  }

  // Show current step
  const current = document.getElementById(this.step_ids[n - 1]);
  if (current) current.hidden = false;

  // Update wizard pip indicators
  document.querySelectorAll(".wizard-pip").forEach(function (pip) {
    const pip_step = parseInt(pip.getAttribute("data-lego"), 10);
    pip.classList.remove("active", "done");
    if (pip_step === n) pip.classList.add("active");
    else if (pip_step < n) pip.classList.add("done");
    pip.setAttribute("aria-selected", pip_step === n ? "true" : "false");
  });

  // Update nav buttons
  const prev_btn = document.getElementById("btnWizardPrev");
  const next_btn = document.getElementById("btnWizardNext");
  if (prev_btn) prev_btn.disabled = (n <= 1);
  if (next_btn) {
    next_btn.style.display = (n >= this.total_steps) ? "none" : "inline-block";
    next_btn.textContent = (n >= this.total_steps - 1) ? "Go to save & launch" : "Next step";
  }

  // Fire event for other modules
  QMW.bus.emit("wizard-step-changed", { step: n, title: this.step_titles[n - 1] });
};

QMW.Wizard.prototype.get_current_step = function () {
  return this.current_step;
};

QMW.Wizard.prototype.get_step_title = function () {
  return this.step_titles[this.current_step - 1] || "";
};

window.QMW = QMW;
export default QMW.Wizard;