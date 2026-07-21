std::regex compatibility
========================

Synopsis
--------

``real::compat`` -- the ``<regex>`` API you already type, on the linear
engine: ``basic_regex`` (aliases ``regex`` / ``wregex``), the ``regex_match``
/ ``regex_search`` / ``regex_replace`` families, ``match_results`` (aliases
``smatch`` / ``cmatch``) and the two iterators. Which patterns run on REAL
and which fall back is the :doc:`compatibility contract
<../drop-in/std-regex-reference>`; migration lives in :doc:`the std::regex
drop-in <../drop-in/std-regex-tour>` -- this page is the object reference.

Interface
---------

.. doxygenclass:: real::compat::basic_regex
   :project: real
   :members:

.. doxygenclass:: real::compat::match_results
   :project: real
   :members:

.. doxygenclass:: real::compat::sub_match
   :project: real
   :members:

.. doxygenclass:: real::compat::regex_iterator
   :project: real
   :members:

.. doxygenclass:: real::compat::regex_token_iterator
   :project: real
   :members:

Matching and replacing
----------------------

The ``<regex>`` free functions ship with their full standard overload sets
(C strings, ``std::string``, iterator pairs, with or without a
``match_results``):

- ``regex_match`` -- the whole range must match.
- ``regex_search`` -- leftmost match anywhere in the range.
- ``regex_replace`` -- template substitution over every match.

Their complete signatures render in the Doxygen namespace reference:

.. raw:: html

   <p><a href="../api/namespacereal_1_1compat.html">real::compat — every
   overload (/api)</a>.</p>

Complexity
----------

A pattern the shim routes to REAL matches in **guaranteed linear** time --
O(len(text)) -- and never backtracks (ReDoS-safe). Which patterns route, and
what the opt-in fallback changes, is exactly the
:doc:`compatibility contract <../drop-in/std-regex-reference>`.

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_compat_std.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- Migration from ``std::regex``: :doc:`the drop-in tour <../drop-in/std-regex-tour>`.
- The routing/fallback contract: :doc:`std-regex-reference <../drop-in/std-regex-reference>`.
- The native engine behind it: :doc:`basic_regex`.
