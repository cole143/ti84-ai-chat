"""Tests for system_prompt.py — the general-purpose ZachGPT system prompt."""
from host import system_prompt as sp


def test_build_system_is_a_single_uncached_text_block():
    system = sp.build_system()
    assert isinstance(system, list)
    assert len(system) == 1
    block = system[0]
    assert block["type"] == "text"
    assert "cache_control" not in block          # too small to be worth caching


def test_build_system_needs_no_study_pack_or_files():
    # Must not depend on any file on disk (the old exam study pack is gone).
    assert not hasattr(sp, "STUDY_PACK_PATH")
    sp.build_system()


def test_prompt_defines_zachgpt_as_a_general_assistant_with_no_course_context():
    text = sp.build_system()[0]["text"]
    assert "ZachGPT" in text
    assert "anything" in text.lower()
    for stale in ("tutor", "FIN 30103", "study", "equit", "exam "):
        assert stale not in text


def test_prompt_enforces_calculator_display_constraints():
    text = sp.build_system()[0]["text"]
    assert "ASCII" in text
    assert "ONE PARAGRAPH" in text
    assert f"under {sp.MAX_ANSWER_WORDS} words" in text.lower()


def test_prompt_shows_the_intro_and_tells_the_model_not_to_repeat_it():
    text = sp.build_system()[0]["text"]
    assert sp.REPLY_PREFIX.strip() in text
    assert "Do NOT" in text


def test_reply_prefix_is_the_exact_zachgpt_line():
    assert sp.REPLY_PREFIX == (
        "yo zachgpt got u fam dont even worry cuh here you go with the sauce "
        "on what your question was: "
    )


def test_answer_budget_leaves_room_for_the_prefix():
    # 500-char calculator limit minus the prefix must still fit MAX_ANSWER_WORDS
    # at ~6 chars per word.
    assert 500 - len(sp.REPLY_PREFIX) >= sp.MAX_ANSWER_WORDS * 6
