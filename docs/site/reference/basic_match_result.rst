basic_match_result
==================

Synopsis
--------

What every ``match`` / ``fullmatch`` / ``search`` call returns: a testable
result carrying the spans and texts of the whole match and its capture
groups, addressable by index or by group name. ``real::match_result`` is the
usual alias. A non-match is simply falsy -- test the result, then read it.
``m.str(1)`` is accepted as a synonym of ``m[1]``, for callers whose fingers
come from ``std::smatch``.

Interface
---------

.. doxygenclass:: real::basic_match_result
   :project: real
   :members: basic_match_result, matched, size, start, end, group_index, operator[], str, operator bool, spans

Complexity
----------

Every accessor is O(1) -- the spans are computed by the match itself, the
result only reads them. ``group_index`` resolves a name against the
pattern's named-group table. Group views (``m[0]``, ``m["year"]``) borrow
the subject, which must outlive the result.

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_basic_regex.cpp
   :language: cpp
   :start-after: // [match-result]
   :end-before: // [/match-result]

See also
--------

- The calls that produce it: :doc:`basic_regex`.
- Iterating every match: :doc:`basic_match_range`.
- The same object from Python (``re.Match``-shaped): :doc:`python`.
