"""Build the SYSTEM list passed to client.messages.create().

Two blocks:
  [0] Exam rubric — frozen, uncached, ~300 tokens. Defines response shape.
  [1] Study pack  — large, cached with 1-hour TTL.

The first call after startup pays the cache write premium (~2x). Every call
within the next hour reads from cache at ~0.1x. Cache key is the exact bytes
of all content up to the cache_control breakpoint, so this module does NOT
inject timestamps or any per-run content.
"""
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
STUDY_PACK_PATH = PROJECT_ROOT / "study_pack.txt"


# Frozen — never edit on a per-run basis. Editing this invalidates the
# cache for every subsequent request, so leave it alone during the exam.
EXAM_RUBRIC = """You are an exam tutor for FIN 30103 Module 3 (Equities).
The student is taking the exam right now and reading your answer on a
TI-84 Plus CE calculator screen (about 38 chars wide, 14 lines tall).

RESPONSE FORMAT (strict):
- Lead with the answer. For multiple choice, give the letter AND value: "C) 17.33".
  For true/false, give "TRUE" or "FALSE" first.
- Then at most 2 short lines explaining why, naming the formula or concept.
- For numerical problems, show the formula with values plugged in, then the answer:
  "P_call = 26*(0.5/0.75) = 17.33".
- Plain ASCII only. No LaTeX. No Unicode. No markdown asterisks or bullets.
- ONE PARAGRAPH, no internal line breaks. Total under 80 words.

WHEN UNSURE: state the most likely textbook answer and flag uncertainty briefly,
e.g. "B) 19.45 (most likely; assumes...)".

The study materials below are the student's class decks, term review, and
practice exam. Use them as the authoritative source for definitions and
preferred answer phrasing on this specific exam.
"""


def build_system() -> list[dict]:
    """Return the SYSTEM list to pass to messages.create()."""
    if not STUDY_PACK_PATH.exists():
        raise FileNotFoundError(
            f"Study pack not found at {STUDY_PACK_PATH}. "
            "Run: python -m host.build_study_pack"
        )
    pack_text = STUDY_PACK_PATH.read_text(encoding="ascii")
    return [
        {"type": "text", "text": EXAM_RUBRIC},
        {
            "type": "text",
            "text": pack_text,
            "cache_control": {"type": "ephemeral", "ttl": "1h"},
        },
    ]
