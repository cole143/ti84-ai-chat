"""Tests for system_prompt.py — the cached SYSTEM list builder."""
from pathlib import Path

import pytest

from host import system_prompt as sp


@pytest.fixture(autouse=True)
def _ensure_pack(tmp_path, monkeypatch):
    """Provide a fake study_pack.txt so tests don't depend on the real one
    being built. Override the module's path constant."""
    fake = tmp_path / "study_pack.txt"
    fake.write_text("FAKE STUDY PACK CONTENT" * 500, encoding="ascii")
    monkeypatch.setattr(sp, "STUDY_PACK_PATH", fake)


def test_build_system_returns_two_blocks():
    system = sp.build_system()
    assert isinstance(system, list)
    assert len(system) == 2


def test_first_block_is_uncached_rubric():
    system = sp.build_system()
    rubric = system[0]
    assert rubric["type"] == "text"
    assert "cache_control" not in rubric
    # Rubric must enforce ASCII + answer-first style.
    assert "ASCII" in rubric["text"]
    assert "Lead with" in rubric["text"] or "lead with" in rubric["text"]


def test_second_block_is_cached_with_1h_ttl():
    system = sp.build_system()
    pack = system[1]
    assert pack["type"] == "text"
    assert pack["cache_control"] == {"type": "ephemeral", "ttl": "1h"}


def test_second_block_text_is_study_pack_contents():
    system = sp.build_system()
    pack = system[1]
    # Must contain our fake content
    assert "FAKE STUDY PACK CONTENT" in pack["text"]


def test_build_system_raises_when_pack_missing(monkeypatch):
    monkeypatch.setattr(sp, "STUDY_PACK_PATH", Path("/nonexistent/study_pack.txt"))
    with pytest.raises(FileNotFoundError):
        sp.build_system()
