"""TI-84 Plus CE AI Chat Bridge.

Reads queries from the calculator over USB CDC serial,
sends them to Claude API, and writes responses back.

Usage: python bridge.py [COM_PORT]
"""
import sys
import os
import serial
import anthropic
from dotenv import load_dotenv

load_dotenv()

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD = 9600
TIMEOUT = 90
MAX_RESPONSE = 500

SYSTEM_PROMPT = """You are a concise financial analysis assistant running on a TI-84 calculator.
Keep responses under 400 characters. Use plain ASCII only — no special characters, no unicode.
Focus on: NPV, IRR, WACC, DCF, portfolio theory, options pricing, bond valuation,
financial ratios, amortization, time value of money, CAPM, and related concepts.
When showing formulas, use plain text notation (e.g., NPV = sum of CF_t / (1+r)^t).
When showing numbers, round to 2 decimal places unless more precision is needed.
Be direct — skip preamble, give the answer first, then brief explanation."""

ASCII_REPLACEMENTS = {
    "\u2014": "-",
    "\u2013": "-",
    "\u2018": "'",
    "\u2019": "'",
    "\u201c": '"',
    "\u201d": '"',
    "\u2026": "...",
    "\u00b0": " deg",
    "\u2022": "-",
    "\u00d7": "x",
    "\u00f7": "/",
    "\u2264": "<=",
    "\u2265": ">=",
    "\u2260": "!=",
    "\u03b1": "alpha",
    "\u03b2": "beta",
    "\u03c3": "sigma",
    "\u03bc": "mu",
    "\u221a": "sqrt",
    "\u221e": "inf",
    "\u03c0": "pi",
    "\u2211": "sum",
}

def sanitize(text: str) -> str:
    """Replace non-ASCII chars Claude commonly outputs."""
    for char, replacement in ASCII_REPLACEMENTS.items():
        text = text.replace(char, replacement)
    return text.encode("ascii", errors="replace").decode("ascii")

def ask_claude(client: anthropic.Anthropic, query: str) -> str:
    """Send query to Claude and return sanitized, truncated response."""
    try:
        msg = client.messages.create(
            model="claude-sonnet-4-6",
            max_tokens=300,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": query}],
        )
        response = msg.content[0].text
        response = sanitize(response)
        return response[:MAX_RESPONSE]
    except anthropic.APIError as e:
        return f"API Error: {str(e)[:80]}"
    except Exception as e:
        return f"Error: {str(e)[:80]}"

def main():
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("ERROR: ANTHROPIC_API_KEY not set.")
        print("Create a .env file with: ANTHROPIC_API_KEY=sk-ant-...")
        sys.exit(1)

    client = anthropic.Anthropic(api_key=api_key)

    print(f"TI-84 AI Bridge on {PORT}")
    print(f"Max response: {MAX_RESPONSE} chars")
    print("Waiting for calculator connection...")
    print("Press Ctrl+C to quit.\n")

    try:
        with serial.Serial(PORT, BAUD, timeout=TIMEOUT) as ser:
            print("Serial port open. Waiting for queries...\n")
            while True:
                line = ser.readline().decode("ascii", errors="replace").strip()
                if not line:
                    continue
                print(f"[QUERY]    {line}")
                response = ask_claude(client, line)
                print(f"[RESPONSE] {response}\n")
                ser.write((response + "\n").encode("ascii"))
    except KeyboardInterrupt:
        print("\nBridge stopped.")
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        print("Make sure TI Connect CE is closed and the calc is running prgmAICHAT.")
        sys.exit(1)

if __name__ == "__main__":
    main()
