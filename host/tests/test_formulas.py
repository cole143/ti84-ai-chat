"""Tests for formula_calcs.py.

Numbers come from the FIN 30103 practice exam (final-equities-noKEY.docx).
The practice exam has no answer key, so values below are hand-computed and
recorded here as the agreed source of truth that BASIC programs must match.
"""
import math

import pytest

from host import formula_calcs as fc


# ---- Margin ----

def test_margin_call_price_q2():
    # Practice Q2: P0=$26, IM=50%, MM=25%
    # P_call = 26 * (1 - 0.50) / (1 - 0.25) = 26 * 0.5 / 0.75 = 17.333...
    assert math.isclose(fc.margin_call_price(26.0, 0.50, 0.25), 17.333, abs_tol=0.01)


def test_margin_call_price_q6():
    # Practice Q6: P0=$32, IM=40%, MM=25%
    # 32 * 0.60 / 0.75 = 25.60
    assert math.isclose(fc.margin_call_price(32.0, 0.40, 0.25), 25.60, abs_tol=0.01)


def test_margin_return_q6():
    # Practice Q6: P0=$32, P_end=$34, IM=40%, no div/costs
    # HPR = (34 - 32) / 32 = 0.0625; R = 0.0625 / 0.40 = 0.15625
    assert math.isclose(fc.margin_return(32.0, 34.0, 0.40), 0.15625, abs_tol=0.0001)


def test_leverage_ratio_25pct():
    assert math.isclose(fc.leverage_ratio(0.25), 4.0)


def test_leverage_ratio_50pct():
    assert math.isclose(fc.leverage_ratio(0.50), 2.0)


# ---- Valuation ----

def test_ddm_q34():
    # Practice Q34: preferred-style: D=$7 annual, k=7.75%
    # V = 7 / 0.0775 = 90.32
    assert math.isclose(fc.ddm(7.0, 0.0775, 0.0), 90.322, abs_tol=0.01)


def test_preferred_q34():
    assert math.isclose(fc.preferred_value(7.0, 0.0775), 90.322, abs_tol=0.01)


def test_mddm_q35():
    # Practice Q35: D1=$1.25, D2=$1.56, g=5% thereafter, k=11%
    # TV2 = 1.56 * 1.05 / (0.11 - 0.05) = 27.30
    # V0 = 1.25 / 1.11 + 1.56 / 1.11^2 + 27.30 / 1.11^2
    #    = 1.1261 + 1.2660 + 22.158 = 24.55
    assert math.isclose(fc.mddm(1.25, 1.56, 0.05, 0.11), 24.55, abs_tol=0.05)


def test_jpe_typical_values():
    # Justified P/E with b=0.40 (60% payout), k=10%, g=4%
    # = (1 - 0.40) / (0.10 - 0.04) = 0.60 / 0.06 = 10.0
    assert math.isclose(fc.justified_pe(0.40, 0.10, 0.04), 10.0)


def test_sustainable_growth():
    # ROE=15%, retention=60% -> g = 0.60 * 0.15 = 0.09
    assert math.isclose(fc.sustainable_growth(0.60, 0.15), 0.09)


# ---- Guardrails ----

def test_ddm_raises_when_g_geq_k():
    with pytest.raises(ValueError):
        fc.ddm(1.0, 0.05, 0.05)
