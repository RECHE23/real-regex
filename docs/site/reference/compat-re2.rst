RE2 compatibility
=================

Synopsis
--------

``real::compat::re2::RE2`` -- Google's RE2 surface on the linear engine,
header-only and zero-dependency. The statics take typed ``Arg`` outputs, the
instance keeps RE2's no-exception contract (``ok()``), and ``RE2::Set`` is
the multi-pattern which-matched. Migration, divergences and limits live in
:doc:`the RE2 drop-in <../drop-in/re2>` -- this page is the object reference.

Interface
---------

.. doxygenclass:: real::compat::re2::RE2
   :project: real
   :members:

.. doxygenclass:: real::compat::re2::Arg
   :project: real
   :members:

Complexity
----------

The same engine as :doc:`basic_regex`: every accepted pattern matches in
**guaranteed linear** time -- O(len(text)) -- and never backtracks
(ReDoS-safe). A construct this layer cannot honor is a clean
``ok() == false``, never a silent fallback.

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_compat_re2.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- Migration, divergences, benchmarks: :doc:`the RE2 drop-in <../drop-in/re2>`.
- The native engine behind it: :doc:`basic_regex`.
- Native multi-pattern which-matched: :doc:`regex_set`.
