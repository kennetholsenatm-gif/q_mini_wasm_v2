# Q-Mini-WASM UX/UI Design Document and Style Guide

This document defines the design system, accessibility strategy, and component guidelines for the Q-Mini-WASM Web User Interface (WUI). The WUI is built with React and TypeScript and serves highly technical users configuring quantum circuits, edge hardware, AI model deployment, and real-time thermodynamic and security monitoring in zero-trust, potentially air-gapped environments.

---

## 1. Core Design Principles & Philosophy

### 1.1 Overarching Design Heuristics (Tactical, DoD-Focused)

- **Mission-first clarity:** Every screen and control must answer: *What is the system state, and what can I do next?* Prioritize actionable information over decorative content.
- **Consistency with DoD UX standards:** Align with MIL-STD-1472 (Human Engineering) and DoD Human Factors Engineering Technical Advisory Group (HFE TAG) guidance where applicable. Use consistent terminology from the domain (e.g., "escalation," "state migration," "ternary optimizer").
- **Progressive disclosure:** Show only the information necessary for the current task. Use expandable sections, tabs, or step wizards for advanced options (e.g., quantum backend selection, SYCL settings) so that expert users can drill down without overwhelming novices.
- **Single source of truth:** One visible indicator per system state (e.g., one "Air-gapped" badge, one "Quantum backend: stubs" label). Avoid duplicate or conflicting status messages.

### 1.2 High Data Density and Cognitive Load

- **Chunking:** Group related data into clearly bounded regions (cards, panels) with a maximum of 5–7 items per group. Use headings (e.g., `h2`/`h3`) to create a scannable hierarchy.
- **Data tables:** For tabular data (e.g., circuit parameters, hardware list), use fixed headers, zebra striping (subtle alternating row background), and a maximum of ~10–12 columns without horizontal scroll on 1024px width. Provide sortable column headers and optional column visibility toggles.
- **Numerical precision:** Display only the precision needed for decision-making (e.g., 2–4 decimal places for thermodynamic metrics). Use monospaced digits for aligned columns and avoid decorative fonts for numbers.
- **Whitespace as structure:** Use spacing to separate logical groups rather than heavy borders. Dense layouts are acceptable only when the user has explicitly chosen a "compact" or "expert" view; default to a comfortable reading density.

### 1.3 Graceful Degradation (Resource-Constrained / Air-Gapped)

- **Offline-first indicators:** When the app detects no network (or air-gapped mode), show a persistent, non-blocking indicator (e.g., a small banner or status dot) stating "Offline" or "Air-gapped." Do not rely on network-dependent features (e.g., external fonts or CDN assets) for core UI; bundle critical assets.
- **Reduced motion and animation:** Provide a user preference or system setting to respect `prefers-reduced-motion: reduce`. In low-power or constrained environments, avoid continuous animations; use short, purposeful transitions (e.g., 150–200 ms) only where they aid feedback.
- **Fallback content:** If real-time data cannot be loaded (e.g., quantum execution times), show the last known value with a "Last updated" timestamp and a "Stale" or "Offline" label rather than a spinner indefinitely.
- **Minimal dependency on external services:** Core configuration flows (circuit design, hardware selection, deployment settings) must be usable without a live connection to a quantum backend or cloud service. Disable or hide only the features that truly require connectivity.

---

## 2. Native Accessibility (A11y) Strategy

Target: **WCAG 2.2 Level AA** as minimum; **Level AAA** for critical paths (forms, alerts, navigation).

### 2.1 Color Contrast, Typography, and Visual Hierarchy

- **Contrast ratios (WCAG 2.2):**
  - Normal text: minimum **4.5:1** against background (AA); **7:1** for AAA.
  - Large text (≥18 pt or 14 pt bold): minimum **3:1** (AA); **4.5:1** (AAA).
  - UI components and graphical objects: minimum **3:1** against adjacent colors.
- **Actionable rule:** Run contrast checks (e.g., WebAIM Contrast Checker) on every foreground/background pair in the design system. Document the exact hex values and ratios in the Visual Style Guide (Section 3).
- **Typography legibility:** Use a minimum body font size of **16px** (1rem) for primary content. Line height: **1.5** for body text, **1.25** for headings. Avoid font sizes below 12px for any interactive or informational text.
- **Visual hierarchy:** Establish a clear heading ladder (H1 → H6). One H1 per view; do not skip levels. Use size, weight, and color to reinforce hierarchy while still meeting contrast requirements.

### 2.2 Keyboard-First Navigation and Focus States

- **All interactive elements** must be reachable and activatable via keyboard (Tab, Enter, Space, Arrow keys where appropriate).
- **Focus order:** Follow DOM order or use `tabIndex` only when necessary to fix a logical order. Avoid positive `tabIndex` unless required for complex widgets.
- **Visible focus indicator:** Provide a **2px solid** focus ring with a color that achieves at least **3:1** contrast against the component background. Do not remove focus outline with `outline: none` without providing a visible alternative (e.g., `outline-offset: 2px` and a high-contrast color). Ensure focus is visible on custom components (buttons, tabs, dropdowns).
- **Skip link:** Include a "Skip to main content" link as the first focusable element on each page for keyboard users.

### 2.3 Semantic HTML and ARIA (Screen Reader Compatibility)

- **Semantic structure:** Use `<main>`, `<nav>`, `<aside>`, `<section>`, and `<article>` appropriately. Use `<button>` for actions, `<a>` for navigation; avoid divs with click handlers for primary actions.
- **Forms:** Associate every form control with a visible `<label>` (use `htmlFor`/`id` or wrap the control in the label). For complex inputs (e.g., parameter sliders with numeric input), provide an accessible name and, if needed, `aria-describedby` for units or constraints. Use `fieldset`/`legend` for grouped controls (e.g., "Quantum backend" with radio options).
- **ARIA roles and states:** Use ARIA only when HTML semantics are insufficient. Examples: `role="tablist"`, `role="tab"`, `aria-selected`; `aria-live="polite"` for status updates; `aria-invalid` and `aria-errormessage` for validation errors. Avoid redundant ARIA (e.g., `role="button"` on a `<button>`).
- **Dynamic content:** When content updates without a full page load (e.g., execution status), expose the update to screen readers via `aria-live` regions. Use `aria-live="polite"` for non-urgent updates and `aria-live="assertive"` only for critical alerts.

### 2.4 Cognitive Accessibility

- **Error messaging:** Use clear, specific language (e.g., "POSTGRES_HOST is set; set POSTGRES_USER and POSTGRES_PASSWORD" rather than "Invalid configuration"). Place error text near the relevant field and associate it with the control (`aria-describedby`/`aria-errormessage`).
- **Predictable layouts:** Keep navigation, primary actions, and status areas in consistent positions across views. Avoid moving or auto-updating content in a way that disorients the user (e.g., no auto-refresh that shifts the viewport).
- **Confirmation for destructive actions:** Require explicit confirmation (e.g., checkbox + button, or modal) for actions that cannot be easily undone (e.g., "Deploy to production," "Clear circuit").

---

## 3. Visual Style Guide

### 3.1 Color Palette

**Dual theme: Dark (Tactical) and Light.**

- **Dark (Tactical) – default for operational environments**
  - Background primary: `#0d1117` (or equivalent near-black). Ensure text on this meets 4.5:1 minimum.
  - Background secondary (cards, panels): `#161b22`.
  - Primary (brand/primary actions): e.g., `#58a6ff`. Check contrast against dark background (target ≥4.5:1 for large text, 3:1 for UI).
  - Secondary (secondary actions, links): e.g., `#8b949e` or a muted blue. Maintain ≥4.5:1 for text.
  - Semantic: Success `#3fb950`, Warning `#d29922`, Error `#f85149`, Info `#58a6ff`. Use in addition to iconography/shape for status (see Section 4).
  - Neutral scale: e.g., `#21262d`, `#30363d`, `#484f58`, `#8b949e`, `#b1bac4`, `#c9d1d9`. Use for borders, disabled states, and secondary text.
  - Document exact hex values and contrast ratios (e.g., "Success on #161b22: 4.6:1") in the design tokens file.

- **Light theme**
  - Background primary: `#ffffff`; secondary: `#f6f8fa`.
  - Primary: e.g., `#0969da`; ensure 4.5:1 on white.
  - Semantic and neutral scales: define light-background equivalents and verify all pairs meet WCAG AA (and AAA where required).

**Rule:** Every foreground/background combination used in the UI must be documented with its contrast ratio. No exceptions for "decorative" text.

### 3.2 Typography

- **Stack:** Choose a highly legible, monospaced-friendly stack suitable for data and code (e.g., "IBM Plex Mono" for code/numbers, "IBM Plex Sans" or "Inter" for UI and body). Fallback: `system-ui, -apple-system, sans-serif` and `ui-monospace, monospace` for code.
- **Modular scale:** Define a consistent scale (e.g., 1.25 or 1.2 ratio). Example:
  - H1: 2rem (32px), weight 700, line-height 1.25.
  - H2: 1.5rem (24px), weight 600.
  - H3: 1.25rem (20px), weight 600.
  - H4–H6: 1.125rem, 1rem, 0.875rem with decreasing weight.
  - Body: 1rem (16px), weight 400, **line-height 1.5**.
  - Caption/small: 0.875rem (14px), line-height 1.4.
- **Code and math:** Use monospace for circuit identifiers, parameter names, and numeric values in tables. Minimum size 14px for code snippets. Ensure sufficient contrast and optional syntax highlighting that remains accessible (no color-only distinction).

### 3.3 Iconography & Imagery

- **Style:** Technical, consistent stroke weight (e.g., 1.5px), 24px default size for inline icons. Prefer outlined over filled for clarity at small sizes.
- **Rule:** Icons must **support, not replace**, text labels for primary actions and navigation. Use `aria-label` or visible text; for icon-only buttons, provide an accessible name and a tooltip that appears on focus/hover.
- **Status and alerts:** Combine color with shape/icon (e.g., checkmark for success, triangle for warning, X for error) so that meaning is clear for color-blind users.

### 3.4 Spacing & Grid

- **Base unit:** **8pt grid.** All spacing (padding, margins, gaps) should be multiples of 8 (e.g., 8, 16, 24, 32, 48).
- **Consistent alignment:** Align form labels and controls to a shared grid. Use consistent padding inside cards (e.g., 16px or 24px).
- **Data grouping:** Use 16–24px spacing between logical groups; 8px between related items within a group.

---

## 4. Component Library Guidelines

### 4.1 Complex Forms

- **Mathematical and quantum parameters:** Provide numeric inputs with explicit units (e.g., "Layers (1–10)") and min/max where applicable. Use `<input type="number">` with `min`, `max`, `step`, and `aria-valuemin`/`aria-valuemax` for range inputs. For advanced users, allow direct numeric input alongside sliders.
- **Hardware and deployment settings:** Group by domain (e.g., "Quantum backend," "SYCL," "Data stack connection"). Use radio groups or select dropdowns with clear labels. For optional sections, use collapsible panels so the form does not overwhelm.
- **Validation:** Validate on blur and on submit. Show inline errors next to the field; do not rely only on color (use icon and text). Ensure `aria-invalid` and `aria-errormessage` are set when a field is invalid.

### 4.2 Data Visualization

- **Real-time metrics (thermodynamic limits, memory, execution times):** Prefer simple, legible formats: numeric readouts with units, progress bars or gauges with labeled axes. Ensure that critical thresholds (e.g., sub-100MB memory) are visually and textually clear (e.g., "Memory: 87 MB" with a green indicator when under 100 MB).
- **Charts and graphs:** Provide text alternatives (e.g., a summary table or `aria-label` describing the trend). Use patterns or distinct shapes in addition to color for data series. Axis labels and legends must meet contrast requirements.
- **Accuracy:** Avoid misleading scaling (e.g., truncated Y-axis without clear indication). Show exact values on hover/focus where possible.

### 4.3 Alerts & Status Indicators

- **Security alerts (e.g., zero-trust boundary violations):** Use a distinct, consistent pattern: e.g., icon + short title + optional detail. Ensure the alert is announced to screen readers (`role="alert"` or `aria-live="assertive"` for critical). Do not rely on color alone: use icon shape and text.
- **System status (online/offline, backend status):** Use a badge or pill with icon + text. For color-blind accessibility, pair color with icon (e.g., cloud-off icon for offline, checkmark for healthy).
- **Success/warning/error:** Semantic colors (Section 3.1) plus icon and short message. Place alerts where they do not block primary content (e.g., banner at top or inline near the trigger).

---

## 5. Interaction & State Design

### 5.1 System States

- **Air-gapped/Offline:** Persistent, low-key indicator (e.g., top bar or status dot) with text "Offline" or "Air-gapped." Use `aria-live="polite"` when transitioning to/from this state. Do not block the UI; allow all offline-capable actions.
- **Loading/Processing (e.g., quantum execution):** Show a determinate progress indicator when possible (e.g., "Step 2 of 3"); otherwise use an indeterminate spinner with `role="status"` and `aria-label="Loading"` or "Processing." Avoid long-running spinners without a cancel or timeout message.
- **Error state:** Clear message, suggested action (e.g., "Retry" or "Check connection"), and a way to dismiss or navigate away. Log errors for support but do not expose stack traces or internal paths to the user unless in a "Developer" mode.

### 5.2 Micro-Interactions and Performance

- **Feedback:** Buttons and links must provide immediate feedback on click (e.g., `:active` state or a brief 150 ms visual change). Form submissions: disable the submit button and show "Saving…" or "Deploying…" to prevent double submission.
- **Performance:** Avoid animations that run continuously (e.g., looping pulses) unless required. Prefer CSS transitions (transform, opacity) for better performance. Respect `prefers-reduced-motion: reduce` by disabling or shortening non-essential motion.

---

## Summary

This UX/UI Design Document and Style Guide establishes:

- **Core principles:** Mission-first clarity, controlled data density, graceful degradation in air-gapped and resource-constrained environments.
- **Accessibility:** WCAG 2.2 AA/AAA targets, contrast and typography rules, keyboard and screen reader support, cognitive accessibility.
- **Visual system:** Dual theme (Dark Tactical / Light), typography scale, iconography and spacing (8pt grid).
- **Components:** Forms, data visualization, and alerts designed for precision and accessibility.
- **States and interaction:** Offline, loading, and error handling plus performant, minimal micro-interactions.

Implement these rules in the WUI codebase (React components, design tokens, and tests) and re-evaluate with real users and automated a11y checks (e.g., axe-core, Lighthouse) on each release.
