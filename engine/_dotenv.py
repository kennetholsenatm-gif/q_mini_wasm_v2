"""Load ``.env`` into the process environment when ``python-dotenv`` is installed."""

from __future__ import annotations


def load_dotenv_if_available() -> None:
    """Call ``load_dotenv()`` so ``HUGGING_FACE_HUB_TOKEN``, ``ACCELERATOR``, etc. can live in ``.env``.

    Does nothing if ``python-dotenv`` is not installed. Existing OS environment variables win.
    """
    try:
        from dotenv import load_dotenv
    except ImportError:
        return
    load_dotenv()
