API reference
=============

Authoritative, curated pages for the public surface. The matter -- signatures,
parameters, returns -- renders straight from the header comments (Breathe) and
the binding's docstrings (autodoc), never hand-transcribed; the pages arrange
it by task and add the guarantees and tested examples. The exhaustive Doxygen
tree always has every symbol:

.. raw:: html

   <p><a href="../api/index.html">Browse the full Doxygen reference (/api)</a>.</p>

Pattern syntax
--------------

- **The pattern language itself** -- every construct and what it means,
  shared by all surfaces: :doc:`Pattern syntax <syntax>`.

C++ core
--------

- **Compile a pattern** -- at run time: :doc:`basic_regex <basic_regex>`
  (``real::regex``); or in the type, at compile time: ``real::static_regex``
  (same page -- an invalid pattern is a compile error, matching is
  ``constexpr``-ready).
- **Match, search, anchor** -- ``match()`` / ``fullmatch()`` / ``search()``
  on :doc:`basic_regex <basic_regex>`.
- **Iterate every match** -- ``find_iter()``, a lazy
  :doc:`range <basic_match_range>`.
- **Count without allocating** -- ``count_matches()``.
- **Replace or split** -- ``replace()`` / ``split()``.
- **Inspect a match** -- groups, spans, truthiness:
  :doc:`basic_match_result <basic_match_result>`.
- **Match many patterns at once** -- which-matched:
  :doc:`regex_set <regex_set>`.
- **Tokenize, maximal-munch** -- the lexer-grade automaton:
  :doc:`dfa <dfa>`.

Compatibility shims (C++)
-------------------------

- **The ``std::regex`` face** -- ``basic_regex``, the ``regex_match`` family,
  ``match_results``, the iterators: :doc:`compat-std <compat-std>`.
- **The RE2 face** -- ``RE2`` and ``RE2::Set``:
  :doc:`compat-re2 <compat-re2>`.

Python binding
--------------

The ``re``-compatible package, rendered from its docstrings:
:doc:`Python API <python>`.

Rust & Go bindings
------------------

- **Rust** -- the ``regex``-crate-shaped API on the linear engine:
  `docs.rs/real-regex <https://docs.rs/real-regex>`_ (rendered rustdoc).
- **Go** -- the ``regexp``-shaped API:
  `pkg.go.dev/…/bindings/go <https://pkg.go.dev/github.com/RECHE23/real-regex/bindings/go>`_
  (rendered godoc).

Drop-in layers
--------------

Migrating from ``std::regex``, RE2, Python ``re``, Rust ``regex`` or Go
``regexp``? The :doc:`Drop-in section <../drop-in/index>` documents each
layer's API, divergences and limits.

.. toctree::
   :hidden:
   :maxdepth: 1

   syntax
   basic_regex
   basic_match_result
   basic_match_range
   regex_set
   dfa
   compat-std
   compat-re2
   python
