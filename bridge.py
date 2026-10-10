"""TI-84 Plus CE AI Chat Bridge (ZachGPT).

Reads questions from the calculator over USB CDC serial, asks Claude as a
general-purpose assistant, and writes the answer back with the ZachGPT intro
in front of it.

Usage: python bridge.py [COM_PORT]
"""
import sys
import os
import time
import serial
import anthropic
from dotenv import load_dotenv

from host.system_prompt import build_system, REPLY_PREFIX

load_dotenv()

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD = 9600
TIMEOUT = 90
MAX_RESPONSE = 500

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
    """Replace non-ASCII chars and flatten to single line for calc display."""
    text = text.replace("**", "").replace("__", "")
    text = text.replace("\r\n", " ").replace("\n", " ").replace("\r", " ")
    while "  " in text:
        text = text.replace("  ", " ")
    for char, replacement in ASCII_REPLACEMENTS.items():
        text = text.replace(char, replacement)
    return text.encode("ascii", errors="replace").decode("ascii").strip()

def format_reply(model_text: str) -> str:
    """Build the line sent to the calculator: ZachGPT intro + sanitized answer,
    capped at MAX_RESPONSE. If the model wrote the intro anyway, drop its copy."""
    answer = sanitize(model_text)
    intro = REPLY_PREFIX.strip().lower()
    if answer.lower().startswith(intro):
        answer = answer[len(intro):].lstrip()
    return (REPLY_PREFIX + answer).rstrip()[:MAX_RESPONSE]

def ask_claude(client: anthropic.Anthropic, query: str, system: list[dict]) -> str:
    """Send the question to Claude and return the ZachGPT reply line
    (intro + sanitized answer, truncated to MAX_RESPONSE).

    Uses claude-opus-4-7 with adaptive thinking and effort=high.
    """
    try:
        msg = client.messages.create(
            model="claude-opus-4-7",
            max_tokens=4000,
            thinking={"type": "adaptive"},
            output_config={"effort": "high"},
            system=system,
            messages=[{"role": "user", "content": query}],
        )
        # Adaptive thinking puts ThinkingBlock(s) before the TextBlock.
        # Filter to text blocks only.
        text = next(
            (b.text for b in msg.content if b.type == "text"),
            "",
        )
        text = format_reply(text)
        usage = msg.usage
        print(
            f"[USE] in={usage.input_tokens} "
            f"cache_read={usage.cache_read_input_tokens} "
            f"cache_write={usage.cache_creation_input_tokens} "
            f"out={usage.output_tokens}"
        )
        return text
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
    system = build_system()

    print(f"TI-84 AI Bridge v5.1 (ZachGPT, general use) on {PORT}")
    print(f"Max response: {MAX_RESPONSE} chars (intro takes {len(REPLY_PREFIX)})")
    print("Waiting for calculator connection...")
    print("Press Ctrl+C to quit.\n")

    while True:
        try:
            ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)
            print(f"Connected on {PORT}. Waiting for queries...\n")
        except serial.SerialException:
            print(f"Port {PORT} not available. Retrying in 2s...")
            time.sleep(2)
            continue

        try:
            while True:
                line = ser.readline().decode("ascii", errors="replace").strip()
                if not line:
                    continue
                print(f"[R] {len(line)} chars")
                response = ask_claude(client, line, system)
                print(f"[RES] {len(response)} chars")
                ser.write((response + "\n").encode("ascii"))
        except serial.SerialException:
            print("USB disconnected. Reconnecting in 2s...")
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(2)
        except KeyboardInterrupt:
            print("\nBridge stopped.")
            try:
                ser.close()
            except Exception:
                pass
            sys.exit(0)

if __name__ == "__main__":
    main()
