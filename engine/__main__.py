"""Allow running the engine with python -m engine."""

import argparse
import logging
import os
import sys
from pathlib import Path

from ._dotenv import load_dotenv_if_available
from .config import EngineConfig
from .train import main as train_main


def _quiet_third_party_loggers() -> None:
    """Keep Hugging Face / HTTP client traffic off INFO (less noise; stderr stays smaller on PowerShell)."""
    for name in (
        "httpx",
        "httpcore",
        "httpcore.connection",
        "httpcore.http11",
        "urllib3",
    ):
        logging.getLogger(name).setLevel(logging.WARNING)


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="QMiniWASM training engine")
    p.add_argument(
        "--config",
        type=Path,
        default=None,
        metavar="PATH",
        help="Training TOML (see configs/training/). When omitted, settings come from environment variables.",
    )
    return p.parse_args(argv)


if __name__ == "__main__":
    load_dotenv_if_available()
    level = os.environ.get("LOG_LEVEL", "INFO").strip().upper()
    logging.basicConfig(
        level=getattr(logging, level, logging.INFO),
        format="%(levelname)s %(name)s: %(message)s",
        stream=sys.stdout,
        force=True,
    )
    _quiet_third_party_loggers()
    log = logging.getLogger(__name__)

    args = _parse_args()
    if args.config is not None:
        cfg_path = args.config.expanduser().resolve()
        if not cfg_path.is_file():
            log.error("Config file not found: %s", cfg_path)
            sys.exit(1)
        config = EngineConfig.from_training_toml(cfg_path)
        acc = os.environ.get("ACCELERATOR", "").strip()
        if acc:
            config.accelerator = acc or None
        qbe = os.environ.get("QUANTUM_BACKEND", "").strip()
        if qbe:
            config.quantum_backend = qbe
    else:
        log.warning(
            "Training without --config PATH is deprecated; use a TOML file under configs/training/ "
            "and pass --config (see docs/TRAINING_DATA.md). Falling back to environment variables."
        )
        config = EngineConfig()

    result = train_main(config=config)
    print("Training complete:", result)
