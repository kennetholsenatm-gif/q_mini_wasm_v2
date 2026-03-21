"""Allow running the engine with python -m engine."""

from .train import main

if __name__ == "__main__":
    result = main()
    print("Training complete:", result)
