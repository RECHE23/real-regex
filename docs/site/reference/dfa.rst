dfa
===

Synopsis
--------

The **capture-free maximal-munch** engine, opt-in via ``<real/dfa.hpp>`` (not
pulled in by ``<real/real.hpp>``). Several patterns compile into one automaton
walked one table transition per byte -- lexer-grade tokenizing. The contract:
the longest match wins; on equal length the earliest pattern (lowest index)
wins; an empty match never wins. No capture groups -- that is the trade-off
against :doc:`basic_regex`.

Interface
---------

.. doxygenclass:: real::dfa
   :project: real
   :members:

.. doxygenstruct:: real::dfa_match
   :project: real
   :members:

.. doxygenenum:: real::dfa_mode
   :project: real

.. doxygenclass:: real::dfa_error
   :project: real

Complexity
----------

Matching is **guaranteed linear** -- one table transition per input byte,
never backtracking (ReDoS-safe). The price is capture-freedom: the result
names the winning rule and its length, nothing inside it. Use
:doc:`basic_regex` when you need groups, or :doc:`regex_set` when you need
which-matched without the DFA restrictions. Numbers live in
:doc:`Performance <../performance/index>`.

Raises
------

Construction audits every pattern for DFA-ability and raises ``dfa_error``
rather than silently mis-recognizing. A pattern is rejected when it holds a
zero-width assertion other than a leading ``\A``/``^`` (``$``, ``\b``,
multiline anchors), a lookaround, a **Unicode code-point class** (``\w`` /
``\d`` / ``\s`` in text mode -- use byte classes like ``[0-9]`` instead), or
a possessive quantifier / atomic group.

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_dfa.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- The capture-full engine, the usual choice: :doc:`basic_regex`.
- Which-matched over a shared scan, without the DFA opt-in:
  :doc:`regex_set`.
- The measured trade-off: :doc:`Performance <../performance/index>`.
