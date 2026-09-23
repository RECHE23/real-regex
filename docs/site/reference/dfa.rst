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

.. doxygenclass:: real::dfa_munch_memo
   :project: real
   :members:

.. doxygenclass:: real::dfa_error
   :project: real

.. doxygenfunction:: real::dfa_faithful(const regex&, std::size_t)
   :project: real

.. doxygenfunction:: real::dfa_faithful(std::span<const regex>, std::size_t)
   :project: real

.. doxygenstruct:: real::dfa_fidelity
   :project: real
   :members:

.. doxygenenum:: real::dfa_fidelity_outcome
   :project: real

.. doxygenvariable:: real::dfa_default_state_budget
   :project: real

Fidelity
--------

A DFA takes the **longest** match; ``regex::match()`` takes the match its
priority order prefers. For many patterns the two coincide, for others they
do not, and the difference is not visible in the syntax: ``a|ab`` on ``"ab"``
matches one byte where the DFA takes two, and so does the greedy,
longer-branch-first ``(?:ab|a)(?:bc)?`` on ``"abc"`` -- while the lazy
``x*?y`` agrees on every input. A caller that needs the DFA to reproduce each
rule's ``match()`` (a lexer falling back to per-rule matching, say) asks
``dfa_faithful``. It decides the question exactly for each pattern and
returns one of three answers: ``faithful``, ``divergent`` with an input that
separates the two, or ``undecided`` when its state budget runs out -- which
must be read as *not* faithful. Over a set, all-faithful is sufficient for
the DFA's munch to equal the per-rule one, not necessary: a divergent rule
that a higher-priority rule always outlasts is still refused.

Complexity
----------

Matching is **guaranteed linear** -- one table transition per input byte,
never backtracking (ReDoS-safe). That bounds one munch by the bytes it reads,
not a whole tokenization: a munch walks until the automaton dies, so ``a*b``
beside ``a`` over ``"aaa…"`` rereads the rest of the subject from every
position, n(n+1)/2 transitions in all. ``match(subject, offset, memo)`` with a
``dfa_munch_memo`` for the subject answers the same and remembers every state a
walk proved leads to no accept, so tokenizing costs O(states × length) -- 3n
transitions on that input (Reps, *Maximal-munch tokenization in linear time*,
1998). The price is capture-freedom: the result
names the winning rule and its length, nothing inside it. Use
:doc:`basic_regex` when you need groups, or :doc:`regex_set` when you need
which-matched without the DFA restrictions. Numbers live in
:doc:`Performance <../performance/index>`.

Raises
------

Construction audits every pattern for DFA-ability and raises ``dfa_error``
rather than silently mis-recognizing. A pattern is rejected when it holds a
zero-width assertion other than a leading ``\A``/``^`` (``$``, ``\b``,
multiline anchors), a lookaround, a possessive quantifier / atomic group, a
code-point class whose UTF-8 expansion is too large (text-mode ``\w``, or a
class repeated many times -- narrower ones such as ``\d``, ``\p{Greek}`` or
``[àé]`` build), or when the automaton outgrows its state cap.
``dfa_faithful`` raises the same error for the same patterns.

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
