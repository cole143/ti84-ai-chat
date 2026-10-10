"""Tests for the pure parts of bridge.py (no serial port, no API)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import bridge  # noqa: E402
from host.system_prompt import REPLY_PREFIX  # noqa: E402


def test_format_reply_prepends_the_zachgpt_intro_and_sanitizes():
    out = bridge.format_reply("Paris is the **capital** of France.\nIt is on the Seine.")
    assert out == REPLY_PREFIX + "Paris is the capital of France. It is on the Seine."


def test_format_reply_never_exceeds_max_response():
    out = bridge.format_reply("x" * 2000)
    assert len(out) == bridge.MAX_RESPONSE
    assert out.startswith(REPLY_PREFIX)


def test_format_reply_drops_a_duplicate_intro_written_by_the_model():
    out = bridge.format_reply(
        "Yo ZachGPT got u fam dont even worry cuh here you go with the sauce "
        "on what your question was: 42"
    )
    assert out == REPLY_PREFIX + "42"


def test_format_reply_is_ascii_only_and_single_line():
    out = bridge.format_reply("café — ok\r\nnext")
    assert out.isascii()
    assert "\n" not in out and "\r" not in out
    assert out == REPLY_PREFIX + "caf? - ok next"


def test_format_reply_handles_empty_model_text():
    assert bridge.format_reply("") == REPLY_PREFIX.rstrip()


def test_equity_abbreviation_expander_is_gone():
    assert not hasattr(bridge, "expand_abbreviations")
    assert not hasattr(bridge, "ABBREVIATIONS")
