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

Interface
---------

.. doxygenclass:: real::basic_regex
   :project: real
   :members: basic_regex, match, fullmatch, search, find_iter, count_matches, replace, split

The two aliases:

.. doxygentypedef:: real::regex
   :project: real

.. doxygentypedef:: real::static_regex
   :project: real

Complexity
----------

Every matching call above is **guaranteed linear** in the searched text --
O(len(text)) -- and never backtracks: ReDoS-safe by construction, for every
pattern the engine accepts.

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

.. raw:: html

   <ul>
   <li>Multi-pattern which-matched:
       <a href="../api/classreal_1_1regex__set.html">regex_set</a>.</li>
   </ul>
