"""Manual smoke test: send two questions through the bridge's ask_claude and
eyeball the ZachGPT replies (intro + answer, <= 500 ASCII chars). Does NOT
require the calculator.

Run: python -m host.tests.smoke_bridge
"""
import os
import sys

import anthropic
from dotenv import load_dotenv

# Make project root importable
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from bridge import ask_claude  # noqa: E402
from host.system_prompt import build_system  # noqa: E402


def main() -> None:
    load_dotenv()
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    assert api_key, "ANTHROPIC_API_KEY not set"

    client = anthropic.Anthropic(api_key=api_key)
    system = build_system()

    q1 = "WHATS THE CAPITAL OF AUSTRALIA"
    q2 = "HOW DO I BOIL AN EGG"

    print("\n--- Q1 ---")
    print(f"Q: {q1}")
    a1 = ask_claude(client, q1, system)
    print(f"A: {a1}")

    print("\n--- Q2 ---")
    print(f"Q: {q2}")
    a2 = ask_claude(client, q2, system)
    print(f"A: {a2}")

    print(f"\nBoth replies must start with the intro and be <= 500 chars: {len(a1)}, {len(a2)}")


if __name__ == "__main__":
    main()
