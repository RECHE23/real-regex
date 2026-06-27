"""Bootstrap statistics and ASCII box-plots for the REAL benchmark harness.

Pure standard library (statistics / random / math) — the benchmark must run with
nothing installed. This lives in real-regex (it is NOT extracted to SciForge): it is
benchmark plumbing, not a shipped substrate.

The headline summary is a CI-aware paired geometric mean: ratios are re_time/real_time
measured on adjacent runs (paired), so the bootstrap resamples the pairs and the verdict
calls a win only when the confidence interval clears 1.0.
"""

import bisect
import math
import random
import statistics


def median_iqr(samples):
    """Return (median, q1, q3, iqr, minimum) of `samples`.

    Quartiles use the "exclusive" rule: the median of each half, with the overall median
    excluded from both halves for an odd count — stable and dependency-free.
    """
    ordered = sorted(samples)
    median = statistics.median(ordered)
    q1, q3 = _quartiles(ordered)
    return median, q1, q3, q3 - q1, ordered[0]


def _quartiles(ordered):
    count = len(ordered)
    if count == 1:
        return ordered[0], ordered[0]
    half = count // 2
    lower = ordered[:half]
    upper = ordered[half + (count % 2):]  # drop the median itself when the count is odd
    return statistics.median(lower), statistics.median(upper)


def bootstrap_ci(samples, stat, B=1000, level=0.95, rng=None):
    """Percentile bootstrap confidence interval of `stat(samples)`.

    Resamples `samples` with replacement B times, applies `stat` to each resample, and
    returns the (lower, upper) percentile bounds for the requested confidence level.
    """
    rng = rng if rng is not None else random.Random(0)
    count = len(samples)
    if count == 0:
        raise ValueError("bootstrap_ci needs at least one sample")
    boots = []
    for _ in range(B):
        resample = [samples[rng.randrange(count)] for _ in range(count)]
        boots.append(stat(resample))
    boots.sort()
    alpha = (1.0 - level) / 2.0
    low = boots[_clamp(int(alpha * B), B)]
    high = boots[_clamp(int(round((1.0 - alpha) * B)) - 1, B)]
    return low, high


def _clamp(index, size):
    return max(0, min(index, size - 1))


def ratio_ci(numerator, denominator, B=1000, level=0.95, rng=None):
    """Point estimate and bootstrap CI for median(numerator)/median(denominator).

    Two INDEPENDENT samples (e.g. two engines timed in separate loops, not paired): each is
    resampled with replacement and the ratio of medians is taken. Returns (ratio, low, high).
    """
    if not numerator or not denominator:
        raise ValueError("ratio_ci needs non-empty samples")
    rng = rng if rng is not None else random.Random(0)
    n_num, n_den = len(numerator), len(denominator)
    point = statistics.median(numerator) / statistics.median(denominator)
    boots = []
    for _ in range(B):
        num = statistics.median([numerator[rng.randrange(n_num)] for _ in range(n_num)])
        den = statistics.median([denominator[rng.randrange(n_den)] for _ in range(n_den)])
        boots.append(num / den)
    boots.sort()
    alpha = (1.0 - level) / 2.0
    low = boots[_clamp(int(alpha * B), B)]
    high = boots[_clamp(int(round((1.0 - alpha) * B)) - 1, B)]
    return point, low, high


def geomean_ci(paired_ratios, B=1000, level=0.95, rng=None):
    """Geometric mean of PAIRED ratios with a bootstrap CI on the log-ratios.

    Each ratio is re_time/real_time from the same (paired) run. The bootstrap resamples
    the log-ratios with replacement (i.e. resamples pairs), takes the mean in log space,
    and exponentiates — so the returned (geomean, low, high) are all in ratio units.
    """
    if not paired_ratios:
        raise ValueError("geomean_ci needs at least one ratio")
    logs = [math.log(r) for r in paired_ratios]

    def geomean_of(log_values):
        return math.exp(statistics.fmean(log_values))

    point = geomean_of(logs)
    low, high = bootstrap_ci(logs, geomean_of, B=B, level=level, rng=rng)
    return point, low, high


def ascii_boxplot(series, labels, width=44):
    """Render stacked horizontal box-plots on a shared axis (Unicode box-drawing).

    Each series gets a row: whiskers (min..max) as ├──┤, the IQR box (q1..q3) as ┃━━┃,
    and the median as █. All series share one linear scale, so re and REAL line up for a
    direct visual comparison. `labels` names the rows.
    """
    values = [value for one in series for value in one]
    if not values:
        return ""
    low, high = min(values), max(values)
    span = (high - low) or 1.0
    width = max(width, 8)

    def position(value):
        return _clamp(int((value - low) / span * (width - 1)), width)

    rows = []
    label_width = max(len(label) for label in labels)
    for samples, label in zip(series, labels):
        median, q1, q3, _iqr, minimum = median_iqr(samples)
        maximum = max(samples)
        cells = [" "] * width
        lo_w, hi_w = position(minimum), position(maximum)
        for i in range(lo_w, hi_w + 1):
            cells[i] = "─"  # ─ whisker
        cells[lo_w], cells[hi_w] = "├", "┤"  # ├ ┤
        box_lo, box_hi = position(q1), position(q3)
        for i in range(box_lo, box_hi + 1):
            cells[i] = "━"  # ━ box
        cells[box_lo], cells[box_hi] = "┃", "┃"  # ┃ ┃
        cells[position(median)] = "█"  # █ median
        rows.append(f"{label:<{label_width}} │{''.join(cells)}│")
    axis = f"{'':<{label_width}} │{_axis_ticks(low, high, width)}│"
    return "\n".join(rows + [axis])


def _axis_ticks(low, high, width):
    left = f"{low:.2g}"
    right = f"{high:.2g}"
    middle = max(width - len(left) - len(right), 1)
    return left + (" " * middle) + right


def ascii_ecdf(samples, width=48, height=10, log=False):
    """Render the empirical CDF as a filled ASCII grid (cumulative fraction, left→right).

    Each column is a value bin across [min, max]; the column is filled from the bottom up to
    the fraction of samples at or below that value, so the curve rises monotonically — better
    than a histogram for heavily skewed data (e.g. speedup ratios). `log=True` puts the value
    axis on a log scale, the right choice when the values span orders of magnitude.
    """
    ordered = sorted(samples)
    n = len(ordered)
    if n == 0:
        return ""
    if log:
        keys = [math.log(max(v, 1e-12)) for v in ordered]
    else:
        keys = list(ordered)
    low, high = keys[0], keys[-1]
    span = (high - low) or 1.0
    width, height = max(width, 8), max(height, 3)

    grid = [[" "] * width for _ in range(height)]
    for col in range(width):
        x = low + span * (col / (width - 1))
        frac = bisect.bisect_right(keys, x) / n
        filled = int(round(frac * height))
        for r in range(filled):
            grid[height - 1 - r][col] = "█"  # █ filled cumulative area

    lines = []
    for r, cells in enumerate(grid):
        tick = "1.0" if r == 0 else ("0.0" if r == height - 1 else "   ")
        lines.append(f"{tick} │{''.join(cells)}│")
    lo_label = f"{ordered[0]:.2g}"
    hi_label = f"{ordered[-1]:.2g}{' (log)' if log else ''}"
    pad = max(width - len(lo_label) - len(hi_label), 1)
    lines.append(f"    └{'─' * width}┘")
    lines.append(f"     {lo_label}{' ' * pad}{hi_label}")
    return "\n".join(lines)
