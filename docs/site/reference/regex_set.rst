regex_set
=========

Synopsis
--------

A set of patterns compiled together, answering **which of them** match a
subject: ``is_match`` (any of them -- stops at the first hit), ``matches``
(one bool per pattern) and ``which`` (the matching indices), always in
construction order. Individual members stay accessible as compiled
:doc:`basic_regex <basic_regex>` objects.

Interface
---------

.. doxygenclass:: real::regex_set
   :project: real
   :members: regex_set, size, empty, compile_flags, is_match, matches, which,
             operator[]

Complexity
----------

Every scan is **guaranteed linear** in the text and never backtracks -- each
member keeps the engine's ReDoS-safe contract. A small set walks members
individually (``is_match`` stops at the first hit). A large enough
DFA-eligible subset shares one fused pass; the bitset stays in construction
order either way. Numbers against ``RE2::Set`` live in
:doc:`Performance <../performance/index>`.

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_regex_set.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- The single-pattern engine each member is: :doc:`basic_regex`.
- The same set from Python (``RegexSet``): :doc:`python`.
- Multi-pattern benchmarks: :doc:`Performance <../performance/index>`.
- Migrating from ``RE2::Set``: :doc:`the RE2 drop-in <../drop-in/re2>`.
