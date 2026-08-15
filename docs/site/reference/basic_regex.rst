.. The by-object skeleton every /reference/ page follows: Synopsis (curated) ->
   the matter via Breathe/autodoc (signatures and parameters straight from the
   header comments / docstrings, never retyped) -> Complexity (the linear-time
   guarantee) -> Example (literalinclude of a source compiled and run by the
   gates) -> See also. Curation arranges the matter; it never duplicates it.

basic_regex
===========

Synopsis
--------

``real::basic_regex`` is the compiled pattern -- the engine's front door, with
a Python-``re``-shaped surface. Two aliases cover its storage policies:

- ``real::regex`` compiles at **run time**: ``real::regex re{pattern};``.
- ``real::static_regex`` carries the pattern **in its type**: parsed, compiled
  and exactly sized at compile time, matching allocates nothing and works in
  ``constexpr`` -- an invalid pattern is a compile error.

Lifetime and regions
--------------------

- Group views borrow the subject: it must outlive the result. A temporary
  ``std::string`` is a compile error.
- ``find_iter`` / ``find_all`` are lvalue-only -- a C++20 range-for would
  dangle on a temporary regex.
- ``match`` / ``search`` / ``fullmatch`` on a temporary regex return
  ``owning_result_type``; bind it with ``auto``.
- ``pos`` / ``endpos`` are byte offsets, not a slice. ``\A`` and ``^``
  (without multiline) still see the absolute position, so they fail when
  ``pos > 0``.

Interface
---------

.. doxygenclass:: real::basic_regex
   :project: real
   :members: basic_regex, result_type, owning_result_type, match, fullmatch, search, find_iter,
             find_all, count_matches, replace, split

The two aliases:

.. doxygentypedef:: real::regex
   :project: real

.. doxygentypedef:: real::static_regex
   :project: real

Two result aliases go with them, both *derived* from the type ``real::regex`` actually
returns rather than restated: ``real::match_result`` for an attempt on a live regex, which
borrows its name tables, and ``real::owning_match_result`` for an attempt on a *temporary*
one, which owns them.

Complexity
----------

Every matching call is **guaranteed linear** in the searched text --
O(len(text)) -- and never backtracks (ReDoS-safe by construction).

- **Allocation.** ``real::regex`` allocates when it compiles. ``static_regex``
  does not. ``count_matches`` allocates no result objects. ``replace``
  returns an owning ``std::string``. ``split`` and group views borrow the
  subject.
- **Sharing.** A compiled regex is immutable and can be used from many
  threads. An iterator is not shared.

Example
-------

Compiled and run by the ``example-check`` gate on every push -- tested code,
not an illustration:

.. literalinclude:: ../../../examples/cpp/reference_basic_regex.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- The match object every call returns -- groups, spans, truthiness:
  :doc:`basic_match_result`.
- The lazy range ``find_iter`` returns: :doc:`basic_match_range`.
- Migrating from another engine: :doc:`Drop-in <../drop-in/index>`.
- The same surface from Python: :doc:`python`.
- Multi-pattern which-matched: :doc:`regex_set`.
