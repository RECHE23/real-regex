Python API
==========

.. automodule:: real

.. currentmodule:: real

Synopsis
--------

``import real`` and use it like ``re`` -- the same functions, the same
``Pattern`` / ``Match`` shapes, rendered here straight from the binding's own
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

.. autodata:: fallback

.. autodata:: NOFLAG
.. autodata:: A
.. autodata:: ASCII
.. autodata:: I
.. autodata:: IGNORECASE
.. autodata:: M
.. autodata:: MULTILINE
.. autodata:: S
.. autodata:: DOTALL
.. autodata:: U
.. autodata:: UNICODE
.. autodata:: X
.. autodata:: VERBOSE

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
