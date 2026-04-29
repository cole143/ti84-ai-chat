"""Tests for build_study_pack.py -- the source-material assembler."""
from pathlib import Path

import pytest

from host import build_study_pack as bsp


SOURCES = Path("c:/Users/gardn/Downloads")


def test_extract_pdf_text_returns_nonempty_ascii():
    pdf = SOURCES / "Term Exam 3 - Review - Final.pdf"
    text = bsp.extract_pdf_text(pdf)
    assert text, "PDF extraction returned empty string"
    assert text.isascii(), "PDF extraction left non-ASCII characters"
    assert "FIN 30103" in text or "Equities" in text


def test_extract_pptx_text_returns_nonempty_ascii():
    pptx = SOURCES / "EMH - Market Efficiency.pptx"
    text = bsp.extract_pptx_text(pptx)
    assert text, "PPTX extraction returned empty string"
    assert text.isascii(), "PPTX extraction left non-ASCII characters"
    assert "EMH" in text or "efficient" in text.lower()


def test_extract_docx_text_returns_nonempty_ascii():
    docx = SOURCES / "Practice Exam - Final (Equities) - noKEY.docx"
    text = bsp.extract_docx_text(docx)
    assert text, "DOCX extraction returned empty string"
    assert text.isascii(), "DOCX extraction left non-ASCII characters"


def test_assemble_orders_sections_deterministically():
    pack = bsp.assemble()
    # Must start with formula appendix (most stable, hand-written)
    assert pack.startswith("=== FORMULA APPENDIX ===")
    # Then the review blueprint
    assert "=== TERM EXAM 3 REVIEW ===" in pack
    # Then deck transcripts in alphabetical filename order
    review_idx = pack.index("=== TERM EXAM 3 REVIEW ===")
    deck1_idx = pack.index("=== DECK: 2 - RiskReward")
    deck2_idx = pack.index("=== DECK: CFA_2024_L1_Masterclass_Equity")
    deck3_idx = pack.index("=== DECK: EMH - Market Efficiency")
    deck4_idx = pack.index("=== DECK: Security_Market_Indexes")
    assert review_idx < deck1_idx < deck2_idx < deck3_idx < deck4_idx
    # Practice exam last
    practice_idx = pack.index("=== PRACTICE EXAM ===")
    assert deck4_idx < practice_idx


def test_assemble_is_pure_ascii():
    pack = bsp.assemble()
    assert pack.isascii(), "Assembled pack contains non-ASCII characters"


def test_assemble_size_in_expected_range():
    pack = bsp.assemble()
    # Roughly 30K-200K characters; below this means extraction failed.
    assert 30_000 < len(pack) < 500_000, f"Unexpected pack size: {len(pack)}"


def test_assemble_includes_key_formulas():
    pack = bsp.assemble()
    # Spot-check that the formula appendix contains every formula a BASIC
    # program implements (so Claude has the formula reference in cache).
    for token in [
        "P_call = P0 * (1 - IM) / (1 - MM)",
        "Leverage ratio = 1 / IM",
        "V0 = D1 / (k - g)",
        "Justified P/E = (1 - b) / (k - g)",
        "Sustainable growth: g = b * ROE",
        "Preferred stock value = D / k",
    ]:
        assert token in pack, f"Missing formula token: {token}"


def test_assemble_is_deterministic():
    pack1 = bsp.assemble()
    pack2 = bsp.assemble()
    assert pack1 == pack2, "assemble() is not byte-for-byte reproducible"
