"""TI-84 Plus CE AI Chat Bridge.

Reads queries from the calculator over USB CDC serial,
sends them to Claude API, and writes responses back.
Includes abbreviation expansion and course-specific context.

Usage: python bridge.py [COM_PORT]
"""
import sys
import os
import re
import time
import serial
import anthropic
from dotenv import load_dotenv

load_dotenv()

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM3"
BAUD = 9600
TIMEOUT = 90
MAX_RESPONSE = 500

# ── Abbreviation Expander ──
# Sorted longest-first so "SDEV" matches before "SD"
ABBREVIATIONS = {
    # Question types
    "MC": "Multiple choice question:",
    "FITB": "Fill in the blank:",
    "TF": "True or false:",
    "CALC": "Calculate:",
    "DEF": "Define:",
    # Returns & rates
    "HPR": "holding period return",
    "APR": "annual percentage rate",
    "EAR": "effective annual rate",
    "RFR": "risk-free rate",
    "MRP": "market risk premium",
    "ERR": "expected rate of return",
    "ROR": "rate of return",
    "DWR": "dollar-weighted return",
    "GEO": "geometric average return",
    "ARITH": "arithmetic average return",
    # Risk & portfolio
    "SDEV": "standard deviation",
    "SD": "standard deviation",
    "VAR": "variance",
    "COV": "covariance",
    "CORR": "correlation coefficient",
    "SR": "Sharpe ratio",
    "VAR5": "Value at Risk at 5 percent",
    "SYST": "systematic risk",
    "UNSYST": "unsystematic risk",
    "DIV": "diversification",
    "MVP": "minimum variance portfolio",
    "EF": "efficient frontier",
    "CAL": "capital allocation line",
    "CML": "capital market line",
    "SML": "security market line",
    "SCL": "security characteristic line",
    "OPP SET": "investment opportunity set",
    "COMP PORT": "complete portfolio",
    # CAPM & models
    "CAPM": "Capital Asset Pricing Model",
    "APT": "Arbitrage Pricing Theory",
    "SIM": "single-index model",
    "FF": "Fama-French",
    "ALPHA": "alpha (abnormal return)",
    "BETA": "beta (systematic risk measure)",
    "IR": "information ratio",
    # EMH
    "EMH": "Efficient Market Hypothesis",
    "WF": "weak-form EMH",
    "SSF": "semi-strong form EMH",
    "SF": "strong-form EMH",
    "TA": "technical analysis",
    "FA": "fundamental analysis",
    "RW": "random walk",
    # Bonds
    "YTM": "yield to maturity",
    "YTC": "yield to call",
    "CY": "current yield",
    "DUR": "duration",
    "MDUR": "modified duration",
    "CONV": "convexity",
    "TIPS": "Treasury Inflation Protected Securities",
    "ZCB": "zero-coupon bond",
    "CDS": "credit default swap",
    "LPT": "liquidity preference theory",
    "IMM": "immunization",
    "CF MATCH": "cash flow matching",
    "SINK": "sinking fund",
    # Corporate finance
    "NPV": "net present value",
    "IRR": "internal rate of return",
    "WACC": "weighted average cost of capital",
    "DCF": "discounted cash flow",
    "TVM": "time value of money",
    "ROIC": "return on invested capital",
    "ROE": "return on equity",
    "ROA": "return on assets",
    "COD": "cost of debt",
    "COE": "cost of equity",
    "FCFF": "free cash flow to the firm",
    "CAPEX": "capital expenditure",
    "MM": "Modigliani-Miller",
    "PECKING": "pecking order theory",
    "TRADEOFF": "static trade-off theory",
    # Financial ratios
    "CR": "current ratio",
    "QR": "quick ratio",
    "DE": "debt-to-equity ratio",
    "ICR": "interest coverage ratio",
    "GPM": "gross profit margin",
    "OPM": "operating profit margin",
    "NPM": "net profit margin",
    "DOH": "days of inventory on hand",
    "DSO": "days sales outstanding",
    "CCC": "cash conversion cycle",
    "SGR": "sustainable growth rate",
    "DUPONT": "DuPont decomposition",
    # Governance & ESG
    "ESG": "environmental social and governance",
    "PA": "principal-agent relationship",
    "CG": "corporate governance",
    "P5F": "Porter's Five Forces",
    # Business models
    "VP": "value proposition",
    "VC": "value chain",
    "NE": "network effects",
    "FREQ": "freemium model",
    "SUB": "subscription model",
}

def expand_abbreviations(text: str) -> str:
    """Expand known abbreviations in the query before sending to Claude."""
    # Sort by length descending so longer matches take priority
    for abbr in sorted(ABBREVIATIONS.keys(), key=len, reverse=True):
        # Word-boundary match (case insensitive)
        pattern = r'\b' + re.escape(abbr) + r'\b'
        text = re.sub(pattern, ABBREVIATIONS[abbr], text, flags=re.IGNORECASE)
    return text

SYSTEM_PROMPT = """You are an expert exam assistant for two courses:
1. FIN 30603 - Investments (Bodie, Kane, Marcus - Essentials of Investments): risk/return, CAPM, APT, EMH, bonds, duration
2. CFA Level 1 - FSA & Corporate Issuers: financial ratios, DuPont, NPV/IRR, capital structure, governance, business models

CRITICAL DISPLAY RULES:
- ONE SINGLE PARAGRAPH. NO line breaks. NO bullets. NO markdown. NO asterisks.
- Under 400 characters total. Plain ASCII only.

ANSWERING RULES:
- If multiple choice options are given (A B C D), pick the BEST answer FIRST: "ANSWER: [letter]. [explanation]"
- If fill-in-the-blank, give the standard textbook term first.
- For calculations, show formula, plug in numbers, give final answer.
- Use standard CFA/textbook definitions. Most commonly tested answer wins.

KEY EXAM CONCEPTS TO KNOW:
- Arithmetic average ignores compounding; geometric accounts for it. Dollar-weighted = IRR.
- APR ignores compounding; EAR does not.
- Sharpe ratio = slope of CAL = risk premium / standard deviation.
- Complete portfolio = risky assets + risk-free asset.
- Systematic risk cannot be diversified away; unsystematic can.
- Beta measures systematic/market risk. Total risk = systematic + firm-specific.
- CAPM: E(r) = Rf + Beta(Rm - Rf). Alpha = actual return minus CAPM expected return.
- SML plots expected return vs beta. CML plots expected return vs total risk (std dev).
- Investors prefer portfolios LEFT AND ABOVE the current opportunity set (higher return, lower risk).
- EMH: weak=past prices, semi-strong=all public info, strong=all info including inside.
- Momentum = short-term continuation. Reversal = long-term mean reversion.
- Bond prices move inversely to yields. Duration measures price sensitivity to rate changes.
- Duration of zero-coupon bond = maturity. Lower coupon = higher duration.
- Modified duration = Macaulay duration / (1 + y). Price change approx = -ModDur x delta_y.
- Immunization: match duration of assets to duration of liabilities.
- Convexity: curvature benefit; investors prefer more convexity.
- NPV > 0 accept; IRR > hurdle rate accept. NPV preferred for mutually exclusive projects.
- DuPont 3-part: ROE = profit margin x asset turnover x equity multiplier.
- Sustainable growth = ROE x retention ratio.
- Cash conversion cycle = DOH + DSO - days payable outstanding.
- Liquidity preference theory: forward rates include liquidity premium, yield curve biased upward.
- Sinking fund: issuer retires bonds gradually. Callable: issuer can redeem early. Puttable: holder can sell back.
- WACC = wd x rd x (1-t) + we x re. MM without taxes: capital structure irrelevant.
- Pecking order: internal funds first, then debt, then equity.
- Agency costs: insufficient effort, empire building, entrenchment, self-dealing."""

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

    print(f"TI-84 AI Bridge v4.0 on {PORT}")
    print(f"Max response: {MAX_RESPONSE} chars")
    print(f"Abbreviations loaded: {len(ABBREVIATIONS)}")
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
                expanded = expand_abbreviations(line)
                if expanded != line:
                    print(f"[E] {len(expanded)} chars")
                response = ask_claude(client, expanded)
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
