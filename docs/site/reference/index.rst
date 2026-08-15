API reference
=============

Signatures, parameters and returns render from the header comments (Breathe)
and the binding's docstrings (autodoc), never hand-transcribed. These pages
arrange that matter by task and add the guarantees and tested examples. The
exhaustive Doxygen tree has every symbol, including internals:

.. raw:: html

   <p><a href="../api/index.html">Browse the full Doxygen reference (/api)</a>.</p>

Which type
----------

.. list-table::
   :header-rows: 1
   :widths: 46 24 30

   * - I want to…
     - Type
     - Page
   * - Compile a pattern and search
     - ``real::regex``
     - :doc:`basic_regex`
   * - Put the pattern in the type (``constexpr``, no heap)
     - ``real::static_regex``
     - :doc:`basic_regex`
   * - Know which of N patterns matched
     - ``real::regex_set``
     - :doc:`regex_set`
   * - Tokenize: longest match wins
     - ``real::dfa``
     - :doc:`dfa`
   * - Read groups and offsets
     - ``real::match_result``
     - :doc:`basic_match_result`

Pattern syntax
--------------

- **The pattern language itself** -- every construct and what it means,
  shared by all surfaces: :doc:`Pattern syntax <syntax>`.

C++ core
--------

- **Match, search, anchor** -- ``match()`` / ``fullmatch()`` / ``search()``
  on :doc:`basic_regex <basic_regex>`.
- **Iterate every match** -- ``find_iter()``, a lazy
  :doc:`range <basic_match_range>`.
- **Count without allocating** -- ``count_matches()``.
- **Replace or split** -- ``replace()`` / ``split()``.
- **Flags, the pattern literal, the exception** -- the auxiliary types:
  :doc:`Support types <support>`.

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
   support
   compat-std
   compat-re2
   python
