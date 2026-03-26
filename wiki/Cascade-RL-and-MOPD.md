# Cascade RL and MOPD

**Cascade reinforcement learning** here means a short **on-policy GRPO** phase on a toy routing MDP before each epoch’s supervised MSE updates. **MOPD** (multi-domain on-policy distillation) adds an optional **feature-matching** term during that phase; the implementation supports multiple named feature keys and optional KL on auxiliary heads for future teachers.

## Quick facts

- **Default:** Cascade phase is **on** (`CASCADE_RL` unset or true). Set `CASCADE_RL=0` to disable.
- **MOPD weight:** `CASCADE_MOPD_LAMBDA` — `0` means GRPO only; larger values add MOPD feature loss.
- **Feature loss shape:** `CASCADE_MOPD_FEAT_LOSS=mse` (default) or `cosine`.
- **Today’s teacher:** A **synthetic** noisy copy of the MDP state embedding (regularization), not a separate trained model. See the design doc for the roadmap.

## Full documentation (repository)

Canonical technical write-up with equations, code map, env table excerpt, and tests:

**[docs/CASCADE_AND_MOPD.md](https://github.com/kennetholsenatm-gif/qminiwasm-core/blob/main/docs/CASCADE_AND_MOPD.md)**

Related: **[AI Training Pipeline](AI-Training-Pipeline.md)** (epoch flow and data), **[Development](Development.md)** (running `python -m qminiwasm.engine`).

---

**Last Updated:** 2026-03-22
