/* ============================================================
   QMiniWASM Training WUI — Schema Introspection Engine
   ============================================================
   Context-Aware Schema Introspection and Completion
   Progressive Context Hinting
   Interactive Token Resolution

   Modeled after hierarchical network OS (IOS/CLI) where ?
   shows valid next commands at current position.

   Features:
     ?     → show valid keys/descriptions at cursor position
     Tab   → unambiguous prefix expansion or suggestion list
     Live  → inline schema hints as user types
   ============================================================ */

const QMW = window.QMW || {};

/**
 * SchemaIntrospection
 * Builds a table-key map from the /api/schema response,
 * then provides IOS-style contextual help and completion.
 *
 * @param {Object} schema - { fields: [{ id, section, key, type, default, options, description }] }
 */
QMW.SchemaIntrospection = function (schema) {
  this.raw_schema = schema;
  this.fields = (schema && schema.fields) || [];
  this.tables = this._build_table_map(this.fields);
};

/* ── Build table → key map ── */
QMW.SchemaIntrospection.prototype._build_table_map = function (fields) {
  const tables = {};
  for (const f of fields) {
    const section = f.section || "root";
    if (!tables[section]) tables[section] = {};
    tables[section][f.key] = {
      id: f.id,
      key: f.key,
      type: f.type,
      default: f.default,
      options: f.options || [],
      description: f.description || "",
    };
  }
  return tables;
};

/* ── Get all table names ── */
QMW.SchemaIntrospection.prototype.get_table_names = function () {
  return Object.keys(this.tables).sort();
};

/* ── Find which table the cursor is inside ── */
QMW.SchemaIntrospection.prototype.get_current_table = function (content, cursor_line) {
  if (!content) return "root";
  const lines = content.split("\n");
  let table = "root";
  for (let i = 0; i <= cursor_line && i < lines.length; i++) {
    const line = lines[i].trim();
    // Match [section] or [[section]]
    const m = line.match(/^\[{1,2}([^\]]+)\]{1,2}/);
    if (m) {
      table = m[1].trim();
    }
  }
  return table;
};

/* ── Get valid keys for a table ── */
QMW.SchemaIntrospection.prototype.get_valid_keys = function (table_name) {
  const t = this.tables[table_name];
  if (!t) {
    // Check for partial match (e.g., "huggingface" matches table "huggingface")
    for (const [name, keys] of Object.entries(this.tables)) {
      if (name === table_name || name.startsWith(table_name + ".")) {
        return Object.values(keys);
      }
    }
    return [];
  }
  return Object.values(t);
};

/* ── Find matching keys by prefix ── */
QMW.SchemaIntrospection.prototype.find_matching_keys = function (table_name, partial) {
  const valid = this.get_valid_keys(table_name);
  if (!partial) return valid;
  const lower = partial.toLowerCase();
  return valid.filter(function (k) {
    return k.key.toLowerCase().startsWith(lower);
  });
};

/* ── Is the prefix unambiguous? ── */
QMW.SchemaIntrospection.prototype.is_unambiguous = function (table_name, partial) {
  return this.find_matching_keys(table_name, partial).length === 1;
};

/* ── Get autocomplete result ── */
QMW.SchemaIntrospection.prototype.get_autocomplete = function (table_name, partial) {
  const matches = this.find_matching_keys(table_name, partial);
  if (matches.length === 0) {
    return { type: "none", matches: [] };
  }
  if (matches.length === 1) {
    return {
      type: "expand",
      text: matches[0].key + " = ",
      field: matches[0],
    };
  }
  return {
    type: "suggest",
    matches: matches,
  };
};

/* ── Get help data for a table ── */
QMW.SchemaIntrospection.prototype.get_help = function (table_name) {
  const keys = this.get_valid_keys(table_name);
  return {
    table: table_name,
    keys: keys.map(function (k) {
      return {
        key: k.key,
        type: k.type,
        default: k.default,
        description: k.description,
        options: k.options,
      };
    }),
  };
};

/* ── Render help panel HTML ── */
QMW.SchemaIntrospection.prototype.render_help_html = function (table_name) {
  const help = this.get_help(table_name);
  if (!help.keys.length) {
    return '<div class="hint">No schema keys found for <code>' + QMW.escape_html(table_name) + '</code></div>';
  }

  let html = '<div class="schema-help">';
  html += '<div class="schema-help-title">Available keys in [' + QMW.escape_html(table_name) + ']</div>';
  html += '<table class="table" style="font-size:var(--text-xs)">';
  html += "<thead><tr><th>Key</th><th>Type</th><th>Default</th><th>Description</th></tr></thead>";
  html += "<tbody>";

  for (const k of help.keys) {
    const def = k.default != null ? String(k.default) : "—";
    const desc = k.description || "—";
    const typeBadge = k.type ? '<span class="badge badge--neutral">' + QMW.escape_html(k.type) + "</span>" : "";
    html += "<tr>";
    html += '<td><code>' + QMW.escape_html(k.key) + "</code></td>";
    html += "<td>" + typeBadge + "</td>";
    html += '<td class="v">' + QMW.escape_html(def) + "</td>";
    html += "<td>" + QMW.escape_html(desc) + "</td>";
    html += "</tr>";
  }

  html += "</tbody></table>";
  html += '<div class="hint" style="margin-top:var(--space-2)">';
  html += "Press <span class=\"kbd\">Tab</span> to autocomplete unambiguous prefix. ";
  html += "Press <span class=\"kbd\">Esc</span> to dismiss.";
  html += "</div>";
  html += "</div>";
  return html;
};

/* ── Render suggestion dropdown HTML ── */
QMW.SchemaIntrospection.prototype.render_suggestions_html = function (matches, partial) {
  if (!matches || !matches.length) return "";

  let html = '<div class="schema-suggestions">';
  html += '<div class="schema-help-title">Matches for <code>' + QMW.escape_html(partial) + "</code></div>";
  html += '<ul style="list-style:none;padding:0;margin:0">';
  for (const m of matches) {
    const desc = m.description ? ' <span class="hint">' + QMW.escape_html(m.description) + "</span>" : "";
    html += "<li style=\"padding:var(--space-1) 0\"><code>" + QMW.escape_html(m.key) + "</code>" + desc + "</li>";
  }
  html += "</ul></div>";
  return html;
};

/* ── Export ── */
window.QMW = QMW;
export default QMW.SchemaIntrospection;