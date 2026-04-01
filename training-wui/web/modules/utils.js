/* ============================================================
   QMiniWASM Training WUI — Utilities
   ============================================================
   Shared helpers: API fetch, HTML escaping, toast,
   clipboard, debounce, throttle, event bus.
   ============================================================ */

const QMW = window.QMW || {};
window.QMW = QMW;

/* ── API helpers ── */
QMW.WUI_TOKEN_SESSION_KEY = "qmw_wui_token_session";

QMW.get_wui_token = function () {
  try {
    return sessionStorage.getItem(QMW.WUI_TOKEN_SESSION_KEY) || "";
  } catch (_) {
    return "";
  }
};

QMW.set_wui_token = function (tok) {
  try {
    if (tok) sessionStorage.setItem(QMW.WUI_TOKEN_SESSION_KEY, tok);
    else sessionStorage.removeItem(QMW.WUI_TOKEN_SESSION_KEY);
  } catch (_) {}
};

QMW.bootstrap_token_from_url = function () {
  try {
    const u = new URL(location.href);
    const t = u.searchParams.get("wui_token");
    if (t && String(t).trim()) {
      QMW.set_wui_token(String(t).trim());
      u.searchParams.delete("wui_token");
      history.replaceState({}, "", u.pathname + u.search + u.hash);
    }
  } catch (_) {}
};

QMW.api_fetch = function (url, opts) {
  const o = opts ? { ...opts } : {};
  const headers = new Headers(o.headers || undefined);
  const tok = QMW.get_wui_token();
  if (tok) headers.set("X-QMW-WUI-Token", tok);
  o.headers = headers;
  return fetch(url, o);
};

QMW.ws_url_with_token = function (path) {
  const tok = QMW.get_wui_token();
  if (!tok) return path;
  const sep = path.includes("?") ? "&" : "?";
  return path + sep + "wui_token=" + encodeURIComponent(tok);
};

/* ── HTML escaping ── */
QMW.escape_html = function (s) {
  if (!s) return "";
  return String(s).replace(/[&<>"']/g, function (c) {
    return { "&": "&", "<": "<", ">": ">", '"': """, "'": "&#39;" }[c];
  });
};

/* ── Toast notifications ── */
QMW.add_toast = function (text, kind) {
  kind = kind || "ok";
  const host = document.getElementById("toastHost");
  if (!host) return;
  const node = document.createElement("div");
  node.className = "toast toast--" + (kind === "error" ? "err" : "ok");
  node.textContent = text;
  host.appendChild(node);
  setTimeout(function () {
    node.style.opacity = "0";
    node.style.transform = "translateY(8px)";
    setTimeout(function () { node.remove(); }, 200);
  }, 3200);
};

/* ── Clipboard ── */
QMW.copy_to_clipboard = async function (text, ok_msg) {
  try {
    await navigator.clipboard.writeText(text);
    if (ok_msg) QMW.add_toast(ok_msg, "ok");
    return true;
  } catch (_) {
    QMW.add_toast("Copy failed", "error");
    return false;
  }
};

/* ── DOM helpers ── */
QMW.$ = function (id) {
  return document.getElementById(id);
};

QMW.el = function (tag, attrs, children) {
  const el = document.createElement(tag);
  if (attrs) {
    for (const [k, v] of Object.entries(attrs)) {
      if (k === "className") el.className = v;
      else if (k === "dataset") {
        for (const [dk, dv] of Object.entries(v)) el.dataset[dk] = dv;
      } else if (k === "style" && typeof v === "object") {
        Object.assign(el.style, v);
      } else if (k.startsWith("on") && typeof v === "function") {
        el.addEventListener(k.slice(2).toLowerCase(), v);
      } else {
        el.setAttribute(k, v);
      }
    }
  }
  if (children) {
    if (typeof children === "string") el.innerHTML = children;
    else if (Array.isArray(children)) {
      children.forEach(function (c) {
        if (typeof c === "string") el.appendChild(document.createTextNode(c));
        else if (c) el.appendChild(c);
      });
    }
  }
  return el;
};

/* ── Debounce ── */
QMW.debounce = function (fn, ms) {
  let timer = null;
  return function () {
    const args = arguments;
    const ctx = this;
    if (timer) clearTimeout(timer);
    timer = setTimeout(function () {
      timer = null;
      fn.apply(ctx, args);
    }, ms);
  };
};

/* ── Throttle ── */
QMW.throttle = function (fn, ms) {
  let last = 0;
  return function () {
    const now = Date.now();
    if (now - last >= ms) {
      last = now;
      fn.apply(this, arguments);
    }
  };
};

/* ── Event bus (pub/sub) ── */
QMW.bus = (function () {
  const listeners = {};
  return {
    on: function (evt, fn) {
      if (!listeners[evt]) listeners[evt] = [];
      listeners[evt].push(fn);
    },
    off: function (evt, fn) {
      if (!listeners[evt]) return;
      listeners[evt] = listeners[evt].filter(function (f) { return f !== fn; });
    },
    emit: function (evt, data) {
      if (!listeners[evt]) return;
      listeners[evt].forEach(function (fn) { fn(data); });
    },
  };
})();

/* ── Local storage helpers ── */
QMW.storage_get = function (key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return fallback;
    return JSON.parse(raw);
  } catch (_) {
    return fallback;
  }
};

QMW.storage_set = function (key, val) {
  try {
    localStorage.setItem(key, JSON.stringify(val));
  } catch (_) {}
};

/* ── Format bytes ── */
QMW.fmt_bytes = function (n) {
  const x = Number(n || 0);
  if (x < 1024) return x + " B";
  if (x < 1048576) return (x / 1024).toFixed(1) + " KB";
  if (x < 1073741824) return (x / 1048576).toFixed(1) + " MB";
  return (x / 1073741824).toFixed(2) + " GB";
};

/* ── Confirm billing action ── */
QMW.confirm_billing = function (message) {
  return new Promise(function (resolve) {
    const bd = QMW.$("modalBillingBackdrop");
    const msgEl = QMW.$("modalBillingMessage");
    const inp = QMW.$("modalBillingInput");
    const ok = QMW.$("modalBillingOk");
    const cancel = QMW.$("modalBillingCancel");
    if (!bd || !msgEl || !inp || !ok || !cancel) {
      resolve(false);
      return;
    }
    msgEl.textContent = message;
    inp.value = "";
    ok.disabled = true;
    bd.hidden = false;
    setTimeout(function () { inp.focus(); }, 0);
    const cleanup = function () {
      bd.hidden = true;
      inp.oninput = null;
      ok.onclick = null;
      cancel.onclick = null;
    };
    inp.oninput = function () { ok.disabled = inp.value !== "confirm"; };
    ok.onclick = function () { cleanup(); resolve(true); };
    cancel.onclick = function () { cleanup(); resolve(false); };
  });
};

/* ── Model slug from name ── */
QMW.model_slug_from_name = function (name) {
  const raw = (name || "").toLowerCase().trim();
  if (!raw) return "model_" + Math.floor(Date.now() / 1000);
  const slug = raw
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .replace(/--+/g, "-");
  return slug || "model_" + Math.floor(Date.now() / 1000);
};

export default QMW;