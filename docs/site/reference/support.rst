Support types
=============

Synopsis
--------

The native engine's public auxiliary types: ``flags`` (the compile options and
their inline-letter equivalents), ``match_semantics`` (which match a search
returns), ``fixed_string`` (the compile-time pattern literal behind
``static_regex``), and ``regex_error`` with its machine-readable
``error_kind`` (the exception every rejection raises -- never a silent
divergence). Data types: no runtime cost of their own.

The everyday flags are ``icase``, ``multiline``, ``dotall``, ``ascii`` and
``verbose`` -- the same letters as ``(?imsxa)`` in the pattern. ``bytes``,
``ecma``, ``dollar_endonly``, ``allow_raw_byte`` and ``ungreedy`` exist for
drop-in parity with another surface; you rarely set them on
``real::regex`` directly.

Interface
---------

.. doxygenenum:: real::flags
   :project: real

.. doxygenenum:: real::match_semantics
   :project: real

.. doxygenstruct:: real::fixed_string
   :project: real
   :members:

.. doxygenclass:: real::regex_error
   :project: real
   :members:

.. doxygenenum:: real::error_kind
   :project: real

Example
-------

Compiled and run by the ``example-check`` gate on every push:

.. literalinclude:: ../../../examples/cpp/reference_support.cpp
   :language: cpp
   :start-after: // [reference]
   :end-before: // [/reference]

See also
--------

- The engine that consumes them: :doc:`basic_regex`.
- The ``imsxa`` inline flags in patterns: :doc:`Pattern syntax <syntax>`.
- What is rejected, and why: :doc:`Pattern syntax <syntax>` and
  :doc:`Features <../features>`.
