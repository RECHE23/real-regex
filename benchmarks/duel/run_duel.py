#!/usr/bin/env python3
"""REAL-vs-rust duel: the same patterns over the same corpora through both engines, ns/byte and match-count
cross-checked, non-cherry-picked. Emits a Markdown table (paste into BENCHMARKS.md). The date row is a
deliberate no-match scan (no yyyy-mm-dd in its corpus): a prefilter gap, not captures."""
import pathlib
import subprocess
import sys
import tempfile

HERE = str(pathlib.Path(__file__).resolve().parent)
REAL = f"{HERE}/real_bench"
RUST = f"{HERE}/rust_bench/target/release/rust_bench"
N = 20000  # corpus repetitions

WORDS = "the quick brown fox jumps over the lazy dog " * N
DIGITS = "42 1000 7 88 305 12 9999 6 ".replace(" ", "  ") * N
CSV = "alpha,bravo,charlie,delta,echo,foxtrot," * N
EMAILS = "contact john.doe@example.com or jane@corp.io today " * N
NODATE = "no dates here just plain english words running along " * N  # date pattern finds NOTHING
MIXED = "id_42 name_foo val_7 key_bar ok_9 " * N
# Sparse corpora: the pattern DOES match, but rarely — mostly non-matching filler with an occasional hit. This
# is where the inner-literal prefilter earns its keep (memmem the rare `-` / `@` skips the filler at memchr
# speed instead of scanning it a class-byte at a time). Versioned here so the row is reproducible, not anecdotal.
DATE_SPARSE = ("plain english words with no date on this line just running along " * 15
               + "then on 2026-07-04 a date finally appears ") * (N // 15)
EMAIL_SPARSE = ("just plain words on this line with no address to be found here at all " * 15
                + "reach jane@corp.io if you must ") * (N // 15)
KV = "config alpha beta gamma key=val host=web port=99 mode=on delta epsilon " * N  # key= flagship

# --- international corpora (Unicode comparative arc) ------------------------------------------------
# Named via \N{...} escapes (not typed as raw glyphs) so each codepoint is verified by the Python parser
# itself at import time -- a wrong name is a SyntaxError, not a silent mojibake risk. Same phrases (and
# same 200 KB-class size, N=20000 repetitions matching this file's existing convention) as the Unicode
# corpora in bench_engines.cpp, so a cross-check between the two harnesses is meaningful.
CJK = ("\N{CJK UNIFIED IDEOGRAPH-4F60}\N{CJK UNIFIED IDEOGRAPH-597D}\N{CJK UNIFIED IDEOGRAPH-4E16}"
       "\N{CJK UNIFIED IDEOGRAPH-754C} \N{HIRAGANA LETTER KO}\N{HIRAGANA LETTER N}\N{HIRAGANA LETTER NI}"
       "\N{HIRAGANA LETTER TI}\N{HIRAGANA LETTER HA} ") * N  # "hello world" (Han) + "konnichiwa" (hiragana)
ARABIC = ("\N{ARABIC LETTER MEEM}\N{ARABIC LETTER REH}\N{ARABIC LETTER HAH}\N{ARABIC LETTER BEH}"
          "\N{ARABIC LETTER ALEF} \N{ARABIC-INDIC DIGIT ZERO}\N{ARABIC-INDIC DIGIT ONE}"
          "\N{ARABIC-INDIC DIGIT TWO}\N{ARABIC-INDIC DIGIT THREE} ") * N  # RTL letters + Arabic-Indic digits
EMOJI = ("\N{GRINNING FACE} \N{PARTY POPPER} \N{THUMBS UP SIGN} "
         "\N{MAN}\N{ZERO WIDTH JOINER}\N{WOMAN}\N{ZERO WIDTH JOINER}\N{GIRL}\N{ZERO WIDTH JOINER}"
         "\N{BOY} ") * N  # astral singles + a ZWJ family sequence
MIXED_SCRIPT = ("Hello \N{CJK UNIFIED IDEOGRAPH-4F60}\N{CJK UNIFIED IDEOGRAPH-597D}"
                "\N{CJK UNIFIED IDEOGRAPH-4E16}\N{CJK UNIFIED IDEOGRAPH-754C} "
                "\N{CYRILLIC CAPITAL LETTER PE}\N{CYRILLIC SMALL LETTER ER}\N{CYRILLIC SMALL LETTER I}"
                "\N{CYRILLIC SMALL LETTER VE}\N{CYRILLIC SMALL LETTER IE}\N{CYRILLIC SMALL LETTER TE} "
                "\N{GRINNING FACE} ") * N  # Latin + Han + Cyrillic + emoji interleaved
LATIN_ACCENTED = ("caf\N{LATIN SMALL LETTER E WITH ACUTE} r\N{LATIN SMALL LETTER E WITH ACUTE}sum"
                  "\N{LATIN SMALL LETTER E WITH ACUTE} na\N{LATIN SMALL LETTER I WITH DIAERESIS}ve fa"
                  "\N{LATIN SMALL LETTER C WITH CEDILLA}ade d\N{LATIN SMALL LETTER E WITH ACUTE}j"
                  "\N{LATIN SMALL LETTER A WITH GRAVE} v\N{LATIN SMALL LETTER E WITH ACUTE}cu tr"
                  "\N{LATIN SMALL LETTER E WITH GRAVE}s \N{LATIN SMALL LETTER E WITH ACUTE}l"
                  "\N{LATIN SMALL LETTER E WITH GRAVE}ve ") * N  # café/résumé/naïve/façade-style FR prose

# Same \N{...}-escape discipline for the Unicode pattern literals below (not just the corpora).
CAFE_CI = "(?i)caf\N{LATIN SMALL LETTER E WITH ACUTE}"
ACCENT_CLASS = "[\N{LATIN SMALL LETTER A WITH GRAVE}-\N{LATIN SMALL LETTER Y WITH DIAERESIS}]+"
CJK_LITERAL = "\N{CJK UNIFIED IDEOGRAPH-4F60}\N{CJK UNIFIED IDEOGRAPH-597D}"

CASES = [
    # (label, pattern, text) — chosen to span REAL's fast-path strengths and rust's DFA/prefilter strengths
    ("literal `dog`",           r"dog",                          WORDS),
    ("alternation `fox|dog|cat`", r"fox|dog|cat",                WORDS),
    ("class `[a-z]+`",          r"[a-z]+",                       WORDS),
    ("digits `[0-9]+`",         r"[0-9]+",                       DIGITS),
    ("fields `[^,]+`",          r"[^,]+",                        CSV),
    (r"word-bound `\b\w+\b`",   r"\b\w+\b",                      WORDS),
    (r"email `(\w+)@(\w+)`",    r"(\w+)@(\w+)",                  EMAILS),
    (r"ident `(\w+)_(\w+)`",    r"(\w+)_(\w+)",                  MIXED),
    (r"date no-match `\d{4}-\d{2}-\d{2}`", r"\d{4}-\d{2}-\d{2}", NODATE),
    # Inner-literal prefilter rows (IL.2) — the sparse-match cases + the key= flagship.
    (r"date sparse `\d{4}-\d{2}-\d{2}`", r"\d{4}-\d{2}-\d{2}", DATE_SPARSE),
    (r"email sparse `(\w+)@(\w+)`",      r"(\w+)@(\w+)",       EMAIL_SPARSE),
    (r"key= `key=(\w+)`",                r"key=(\w+)",         KV),
    # Unicode comparative rows (rust regex crate = recent UCD, cleanest comparison for \p{}; see
    # docs/BENCHMARKS.md's Unicode section for each engine's UCD version and the count-divergence rule).
    (r"unicode `\w+` (mixed-script)",    r"\w+",               MIXED_SCRIPT),
    (r"unicode `\p{L}+` (CJK)",          r"\p{L}+",            CJK),
    (r"unicode `\p{N}+` (arabic digits)", r"\p{N}+",           ARABIC),
    (r"unicode `\p{sc=Han}` (CJK)",      r"\p{sc=Han}",        CJK),
    (r"unicode `\p{scx=Cyrl}` (mixed-script)", r"\p{scx=Cyrl}", MIXED_SCRIPT),
    ("unicode (?i) accented literal",    CAFE_CI,              LATIN_ACCENTED),
    ("unicode accented class `[a-y]+`",  ACCENT_CLASS,         LATIN_ACCENTED),
    ("unicode CJK literal",              CJK_LITERAL,          CJK),
    ("unicode `.` (emoji, one codepoint)", r".",               EMOJI),
]


def run(binary, pattern, text, mode=None):
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(text)
        path = f.name
    cmd = [binary, pattern, path] + ([mode] if mode else [])
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.split()
    return float(out[0]) if out[0] != "unsupported" else None, int(out[1])


def main():
    print(f"{'case':40s} {'REAL ns/B':>10s} {'rust ns/B':>10s} {'winner':>16s}  match✓")
    rows = []
    for label, pat, text in CASES:
        rr, rc = run(REAL, pat, text)
        # rust in "captures" mode: it extracts every group, the apples-to-apples with REAL's find_iter,
        # which always builds the full Match. The default "find" (spans only) would flatter rust unfairly.
        ur, uc = run(RUST, pat, text, mode="captures")
        agree = (rc == uc)
        if rr is None or ur is None:
            verdict = "REAL-only" if ur is None else "rust-only"
        elif rr < ur:
            verdict = f"REAL {ur/rr:.1f}x"
        else:
            verdict = f"rust {rr/ur:.1f}x"
        print(f"{label:40s} {rr:10.3f} {ur:10.3f} {verdict:>16s}  {'yes' if agree else 'NO('+str(rc)+'/'+str(uc)+')'}")
        rows.append((label, rr, ur, verdict, agree, rc))
    return rows


if __name__ == "__main__":
    main()
