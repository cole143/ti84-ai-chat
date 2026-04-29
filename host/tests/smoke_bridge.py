"""Manual smoke test: send two questions through the bridge's ask_claude
and verify the second one shows a cache hit. Does NOT require the calculator.

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

    q1 = "MC: An investor purchases stock at $26 with IM=50% and MM=25%. Margin call price is closest to: A) 15.25 B) 19.45 C) 17.33"
    q2 = "TF: The duration of a zero-coupon bond is equal to its maturity."

    print("\n--- Q1 (expect cache write) ---")
    print(f"Q: {q1}")
    a1 = ask_claude(client, q1, system)
    print(f"A: {a1}")

    print("\n--- Q2 (expect cache read) ---")
    print(f"Q: {q2}")
    a2 = ask_claude(client, q2, system)
    print(f"A: {a2}")

    print("\nIf [USE] for Q2 shows cache_read > 0, caching is working.")


if __name__ == "__main__":
    main()
