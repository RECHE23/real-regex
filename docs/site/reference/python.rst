Python API
==========

.. Declares the module so Sphinx's own py-modindex has a target to link to. `automodule`
   used to create it as a side effect, and rendering the module docstring with it; this
   directive creates the anchor and renders nothing.
.. py:module:: real

.. currentmodule:: real

Synopsis
--------

``import real`` and use it like ``re`` -- the same functions, the same
``Pattern`` / ``Match`` shapes, rendered here from the binding's own
docstrings (the text ``help()`` shows). Everything runs on the linear-time,
ReDoS-safe engine by default; the opt-in ``fallback`` policy delegates a
pattern REAL would reject to the standard-library ``re`` and forfeits that
guarantee for it -- ``Pattern.engine`` reports which backend ran. The
extensions beyond ``re`` say so in their docstrings (``count_matches``, the
``fallback`` policy, ``RegexSet``).

Functions
---------

.. autofunction:: compile
.. autofunction:: match
.. autofunction:: fullmatch
.. autofunction:: search
.. autofunction:: findall
.. autofunction:: count_matches
.. autofunction:: finditer
.. autofunction:: split
.. autofunction:: sub
.. autofunction:: subn
.. autofunction:: escape
.. autofunction:: purge
.. autofunction:: get_include
.. autofunction:: get_config
.. autoexception:: error

Pattern
-------

The compiled pattern -- the C++ :doc:`basic_regex <basic_regex>` behind a
``re.Pattern`` face.

.. autoclass:: Pattern
   :members:

Match
-----

The result of a successful match -- :doc:`basic_match_result
<basic_match_result>` behind a ``re.Match`` face; ``finditer`` walks it
through :doc:`basic_match_range <basic_match_range>`.

.. autoclass:: Match
   :members:

RegexSet
--------

Multi-pattern which-matched -- the C++ :doc:`regex_set <regex_set>` from
Python.

.. autoclass:: RegexSet
   :members:

Flags and module data
---------------------

The flag integers match ``re`` (``real.I is 2``, same as ``re.I``). Pair
aliases (``I`` / ``IGNORECASE``, …) are the same object.

.. list-table::
   :header-rows: 1
   :widths: 28 16 56

   * - Name
     - ``re`` twin
     - Meaning
   * - ``NOFLAG``
     - ``re.NOFLAG``
     - No flags.
   * - ``I`` / ``IGNORECASE``
     - ``re.I``
     - Case-insensitive (Unicode fold in text mode; ASCII under ``A``).
   * - ``M`` / ``MULTILINE``
     - ``re.M``
     - ``^`` and ``$`` also match at line boundaries.
   * - ``S`` / ``DOTALL``
     - ``re.S``
     - ``.`` also matches a newline.
   * - ``X`` / ``VERBOSE``
     - ``re.X``
     - Ignore unescaped whitespace and ``#`` comments outside classes.
   * - ``A`` / ``ASCII``
     - ``re.A``
     - Keep ``\w \d \s \b`` and case folding ASCII, even in ``str`` mode.
   * - ``U`` / ``UNICODE``
     - ``re.U``
     - No-op: Unicode is the ``str``-mode default.

.. py:data:: fallback
   :type: bool
   :value: False

   Module-level policy for a pattern the linear engine cannot represent
   (backreferences, conditionals, an unbounded lookaround). The default is
   strict: such a pattern raises :class:`error`. Set ``real.fallback = True``,
   or pass ``fallback=True`` to :func:`compile` / the module functions, to
   delegate that pattern to the standard-library ``re`` -- which may accept
   it but forfeits the linear-time guarantee. A per-call argument wins.

Complexity
----------

Every pattern REAL accepts runs the engine the C++ pages document: matching is
**guaranteed linear** -- O(len(string)) -- and never backtracks (ReDoS-safe),
for ``str`` and ``bytes`` alike. The opt-in ``fallback=True`` (per call, or
``real.fallback = True``) routes a pattern REAL rejects to the backtracking
stdlib ``re``, trading the linear-time guarantee for that pattern;
``Pattern.engine`` (``"real"`` / ``"re"``) tells you which ran. The
Python-vs-``re`` numbers live in :doc:`Performance <../performance/index>`.

Example
-------

Run by the Python test suite on every push -- tested code, not an
illustration:

.. literalinclude:: ../../../bindings/python/examples/quickstart.py
   :language: python
   :start-after: # [quickstart]
   :end-before: # [/quickstart]

See also
--------

- Migrating from ``re`` -- the API, divergences and limits:
  :doc:`the re drop-in <../drop-in/re>`.
- The C++ faces of these objects: :doc:`basic_regex`,
  :doc:`basic_match_result`, :doc:`regex_set`.
- Python-vs-``re`` benchmarks: :doc:`Performance <../performance/index>`.
