/* ============================================================
   QMiniWASM Training WUI — Expert TOML Editor
   ============================================================
   Context-aware TOML editor with IOS-style:
     ?   → show valid keys at cursor position
     Tab → unambiguous prefix expansion
   
   Integrates SchemaIntrospection for live schema hints.
   Cognitive: replaces scattered form fields with single
   focused editor (reduces Hick's Law decision overhead).
   ============================================================ */

const QMW = window.QMW || {};

QMW.ExpertEditor = function (container_id, introspection) {
  this.container = document.getElementById(container_id);
  this.introspection = introspection;
  this.help_panel = null;
  this.editor = null;
  this._init();
};

QMW.ExpertEditor.prototype._init = function () {
  if (!this.container) return;

  // Build editor shell
  const shell = document.createElement("div");
  shell.className = "expert-editor";
  shell.innerHTML =
    '<div class="expert-editor-toolbar">' +
    '<span class="hint" style="margin:0;font-size:var(--text-xs)">' +
    '<span class="kbd">?</span> schema help &nbsp; <span class="kbd">Tab</span> autocomplete &nbsp; ' +
    '<span class="kbd">Ctrl+S</span> save' +
    "</span>" +
    "</div>" +
    '<textarea class="textarea expert-editor-textarea" id="expertTomlEditor" ' +
    'spellcheck="false" wrap="off" ' +
    'placeholder="# Enter TOML configuration here&#10;# Press ? at any cursor position for valid keys&#10;# Press Tab to autocomplete" ' +
    'style="min-height:420px;font-size:var(--text-sm);line-height:1.6;tab-size:2">' +
    "</textarea>" +
    '<div id="schemaHelpPanel" class="schema-help-panel" hidden></div>';

  this.container.appendChild(shell);

  this.editor = document.getElementById("expertTomlEditor");
  this.help_panel = document.getElementById("schemaHelpPanel");

  if (this.editor) {
    this._bind_events();
  }
};

QMW.ExpertEditor.prototype._bind_events = function () {
  const self = this;

  this.editor.addEventListener("keydown", function (e) {
    if (e.key === "?" && !e.ctrlKey && !e.metaKey) {
      e.preventDefault();
      self._show_contextual_help();
    }
    if (e.key === "Tab" && !e.shiftKey) {
      e.preventDefault();
      self._handle_tab();
    }
    if (e.key === "Escape") {
      self._hide_help();
    }
  });

  // Live hint on typing
  this.editor.addEventListener(
    "input",
    QMW.debounce(function () {
      self._update_inline_hint();
    }, 300)
  );
};

QMW.ExpertEditor.prototype._get_cursor_line = function () {
  const val = this.editor.value;
  const pos = this.editor.selectionStart;
  const before = val.substring(0, pos);
  return before.split("\n").length - 1;
};

QMW.ExpertEditor.prototype._get_current_line_text = function () {
  const val = this.editor.value;
  const pos = this.editor.selectionStart;
  const before = val.substring(0, pos);
  const lines = before.split("\n");
  return lines[lines.length - 1] || "";
};

QMW.ExpertEditor.prototype._get_partial_token = function () {
  const line = this._get_current_line_text();
  // Get text after the last whitespace/tab
  const m = line.match(/[\s]*([a-zA-Z_][a-zA-Z0-9_.]*)$/);
  return m ? m[1] : "";
};

QMW.ExpertEditor.prototype._show_contextual_help = function () {
  if (!this.introspection || !this.help_panel) return;

  const content = this.editor.value;
  const cursor_line = this._get_cursor_line();
  const table = this.introspection.get_current_table(content, cursor_line);
  const html = this.introspection.render_help_html(table);

  this.help_panel.innerHTML = html;
  this.help_panel.hidden = false;
  this.help_panel.style.display = "block";
};

QMW.ExpertEditor.prototype._handle_tab = function () {
  if (!this.introspection) return;

  const content = this.editor.value;
  const cursor_line = this._get_cursor_line();
  const table = this.introspection.get_current_table(content, cursor_line);
  const partial = this._get_partial_token();

  if (!partial) return;

  const result = this.introspection.get_autocomplete(table, partial);

  if (result.type === "expand") {
    // Unambiguous: insert the completion
    this._insert_at_cursor(result.text);
    this._hide_help();
  } else if (result.type === "suggest") {
    // Ambiguous: show suggestions
    const html = this.introspection.render_suggestions_html(result.matches, partial);
    if (this.help_panel) {
      this.help_panel.innerHTML = html;
      this.help_panel.hidden = false;
      this.help_panel.style.display = "block";
    }
  }
};

QMW.ExpertEditor.prototype._insert_at_cursor = function (text) {
  const pos = this.editor.selectionStart;
  const val = this.editor.value;
  this.editor.value = val.substring(0, pos) + text + val.substring(pos);
  this.editor.selectionStart = this.editor.selectionEnd = pos + text.length;
  this.editor.focus();
};

QMW.ExpertEditor.prototype._update_inline_hint = function () {
  if (!this.introspection || !this.help_panel) return;

  const line = this._get_current_line_text().trim();
  // Only show hint if line looks like a partial key (no = yet)
  if (line.includes("=") || line.startsWith("#") || line.startsWith("[") || !line) {
    this._hide_help();
    return;
  }

  const content = this.editor.value;
  const cursor_line = this._get_cursor_line();
  const table = this.introspection.get_current_table(content, cursor_line);
  const partial = this._get_partial_token();

  if (!partial || partial.length < 2) {
    this._hide_help();
    return;
  }

  const result = this.introspection.get_autocomplete(table, partial);
  if (result.type === "expand") {
    // Show single match hint
    const field = result.field;
    let hint = '<code>' + QMW.escape_html(field.key) + '</code>';
    if (field.description) hint += ' — ' + QMW.escape_html(field.description);
    if (field.type) hint += ' <span class="badge badge--neutral">' + QMW.escape_html(field.type) + '</span>';
    this.help_panel.innerHTML = '<div class="schema-inline-hint">' + hint + '</div>';
    this.help_panel.hidden = false;
    this.help_panel.style.display = "block";
  } else if (result.type === "suggest" && result.matches.length <= 5) {
    const html = this.introspection.render_suggestions_html(result.matches, partial);
    this.help_panel.innerHTML = html;
    this.help_panel.hidden = false;
    this.help_panel.style.display = "block";
  } else {
    this._hide_help();
  }
};

QMW.ExpertEditor.prototype._hide_help = function () {
  if (this.help_panel) {
    this.help_panel.hidden = true;
    this.help_panel.style.display = "none";
  }
};

QMW.ExpertEditor.prototype.get_value = function () {
  return this.editor ? this.editor.value : "";
};

QMW.ExpertEditor.prototype.set_value = function (val) {
  if (this.editor) this.editor.value = val || "";
};

QMW.ExpertEditor.prototype.focus = function () {
  if (this.editor) this.editor.focus();
};

window.QMW = QMW;
export default QMW.ExpertEditor;