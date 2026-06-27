"""Dep-free benchmark statistics + ASCII rendering for REAL's bench harness."""

from .stats import ascii_boxplot, ascii_ecdf, bootstrap_ci, geomean_ci, median_iqr, ratio_ci

__all__ = ["median_iqr", "bootstrap_ci", "geomean_ci", "ratio_ci", "ascii_boxplot", "ascii_ecdf"]
