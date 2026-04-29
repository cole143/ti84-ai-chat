"""Reference implementations of every formula a TI-BASIC program implements.

These are the source of truth tested in test_formulas.py. The TI-BASIC
programs in basic/ are hand-translations of these — keep them in sync.
"""


def margin_call_price(p0: float, im: float, mm: float) -> float:
    """Margin call price for a long position.

    p0: initial price per share
    im: initial margin (decimal, e.g., 0.50 for 50%)
    mm: maintenance margin (decimal)
    """
    if not (0 < mm < im < 1):
        raise ValueError("Require 0 < mm < im < 1")
    return p0 * (1.0 - im) / (1.0 - mm)


def margin_return(
    p0: float,
    p_end: float,
    im: float,
    dividends: float = 0.0,
    interest_cost: float = 0.0,
    transaction_cost: float = 0.0,
) -> float:
    """Holding-period return on a margined long position."""
    if not (0 < im < 1):
        raise ValueError("Require 0 < im < 1")
    capital_gain = p_end - p0
    net = capital_gain + dividends - interest_cost - transaction_cost
    return net / (im * p0)


def leverage_ratio(im: float) -> float:
    """Leverage ratio = 1 / IM."""
    if not (0 < im <= 1):
        raise ValueError("Require 0 < im <= 1")
    return 1.0 / im


def ddm(d1: float, k: float, g: float) -> float:
    """Single-stage Gordon DDM. V0 = D1 / (k - g). Requires k > g."""
    if k <= g:
        raise ValueError("Require k > g for Gordon DDM")
    return d1 / (k - g)


def preferred_value(d: float, k: float) -> float:
    """Preferred stock value = D / k. Special case of DDM with g = 0."""
    return ddm(d, k, 0.0)


def mddm(d1: float, d2: float, g: float, k: float) -> float:
    """Two-stage DDM with explicit D1, D2 and constant g thereafter.

    TV2 = D2*(1+g)/(k-g); V0 = D1/(1+k) + D2/(1+k)^2 + TV2/(1+k)^2
    """
    if k <= g:
        raise ValueError("Require k > g for terminal value")
    tv2 = d2 * (1.0 + g) / (k - g)
    return d1 / (1.0 + k) + d2 / (1.0 + k) ** 2 + tv2 / (1.0 + k) ** 2


def justified_pe(b: float, k: float, g: float) -> float:
    """Justified leading P/E = (1 - b) / (k - g)."""
    if k <= g:
        raise ValueError("Require k > g")
    if not (0 <= b <= 1):
        raise ValueError("Require 0 <= b <= 1")
    return (1.0 - b) / (k - g)


def sustainable_growth(b: float, roe: float) -> float:
    """g = b * ROE."""
    return b * roe
