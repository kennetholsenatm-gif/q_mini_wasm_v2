"""Allow running the engine with python -m engine."""

import logging
import os
import sys

from .train import main


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


if __name__ == "__main__":
    level = os.environ.get("LOG_LEVEL", "INFO").strip().upper()
    # Use stdout so PowerShell does not treat every INFO line as NativeCommandError (stderr).
    logging.basicConfig(
        level=getattr(logging, level, logging.INFO),
        format="%(levelname)s %(name)s: %(message)s",
        stream=sys.stdout,
        force=True,
    )
    _quiet_third_party_loggers()
    result = main()
    print("Training complete:", result)
