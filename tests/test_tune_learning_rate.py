from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def _load_module():
    repo = Path(__file__).resolve().parents[1]
    p = repo / "scripts" / "tune_learning_rate.py"
    spec = importlib.util.spec_from_file_location("tune_learning_rate", p)
    assert spec is not None and spec.loader is not None
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


def test_replace_training_scalar_updates_existing_key():
    m = _load_module()
    src = "[training]\nlearning_rate = 0.1\nepochs = 5\n"
    out = m._replace_training_scalar(src, "learning_rate", "0.0003")
    assert "learning_rate = 0.0003" in out


def test_replace_training_scalar_adds_missing_key():
    m = _load_module()
    src = "[training]\nepochs = 5\n"
    out = m._replace_training_scalar(src, "learning_rate", "0.0003")
    assert "learning_rate = 0.0003" in out


def test_extract_training_result_and_metric():
    m = _load_module()
    out = "x\ny\nTraining complete: {'final_loss': 0.123, 'metrics': {'eval_mean_mse': 0.02}}\n"
    parsed = m._extract_training_result(out)
    assert isinstance(parsed, dict)
    metric_name, metric_value = m._metric_from_result(parsed)
    assert metric_name == "eval_mean_mse"
    assert metric_value == 0.02


def test_metric_from_logs_prefers_eval_then_train():
    m = _load_module()
    txt = "INFO x: epoch=1 eval_mean_mse=0.456\nINFO x: epoch=1/2 mean_mse=0.789\n"
    name, val = m._metric_from_logs(txt)
    assert name == "eval_mean_mse_partial"
    assert val == 0.456

    txt2 = "INFO x: epoch=1/2 mean_mse=0.123\n"
    name2, val2 = m._metric_from_logs(txt2)
    assert name2 == "mean_mse_partial"
    assert val2 == 0.123
