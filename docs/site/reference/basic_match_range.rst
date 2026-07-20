basic_match_range
=================

Synopsis
--------

The lazy range ``find_iter()`` returns. Iterate it with a range-for; each
element is a :doc:`match result <basic_match_result>`. Nothing is scanned
until the iterator advances, and each step applies the non-overlapping and
empty-match advance rules.

Interface
---------

.. doxygenclass:: real::basic_match_range
   :project: real
   :members:

.. doxygenclass:: real::basic_match_iterator
   :project: real
   :members:

Complexity
----------

Lazy: each increment advances to the next match. A full traversal is one
**guaranteed linear** scan of the text -- O(len(text)) -- and never
backtracks (ReDoS-safe by construction).

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_basic_regex.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- The call that produces it: :doc:`basic_regex` (``find_iter``).
- Each element: :doc:`basic_match_result`.
- The same iteration from Python (``finditer``): :doc:`python`.
