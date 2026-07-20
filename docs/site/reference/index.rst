API reference
=============

Authoritative, curated pages for the public surface. The matter -- signatures,
parameters, returns -- renders straight from the header comments (Breathe) and
the binding's docstrings (autodoc), never hand-transcribed; the pages arrange
it by task and add the guarantees and tested examples. The exhaustive Doxygen
tree always has every symbol:

.. raw:: html

   <p><a href="../api/index.html">Browse the full Doxygen reference (/api)</a>.</p>

C++ core
--------

- **Compile a pattern** -- at run time: :doc:`basic_regex <basic_regex>`
  (``real::regex``); or in the type, at compile time: ``real::static_regex``
  (same page -- an invalid pattern is a compile error, matching is
  ``constexpr``-ready).
- **Match, search, anchor** -- ``match()`` / ``fullmatch()`` / ``search()``
  on :doc:`basic_regex <basic_regex>`.
- **Iterate every match** -- ``find_iter()``, a lazy range.
- **Count without allocating** -- ``count_matches()``.
- **Replace or split** -- ``replace()`` / ``split()``.

.. raw:: html

   <ul>
   <li><strong>Inspect a match</strong> — groups, spans, truthiness:
       <a href="../api/classreal_1_1basic__match__result.html">basic_match_result</a>.</li>
   <li><strong>Match many patterns at once</strong> — which-matched:
       <a href="../api/classreal_1_1regex__set.html">regex_set</a>.</li>
   <li><strong>Tokenize, maximal-munch</strong> — the lexer-grade automaton:
       <a href="../api/classreal_1_1dfa.html">dfa</a>.</li>
   </ul>

Python binding
--------------

The ``re``-compatible package, rendered from its docstrings:
:doc:`Python API <python>`.

Drop-in layers
--------------

Migrating from ``std::regex``, RE2, Python ``re``, Rust ``regex`` or Go
``regexp``? The :doc:`Drop-in section <../drop-in/index>` documents each
layer's API, divergences and limits.

.. toctree::
   :hidden:
   :maxdepth: 1

   basic_regex
   python
