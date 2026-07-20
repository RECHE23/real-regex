API reference
=============

The symbol reference for real::regex. C++ symbols render below through the
Doxygen-XML -> Breathe -> Sphinx pipeline, straight from the header comments --
never hand-transcribed. The :doc:`Python surface <python>` renders the same way,
from the binding's docstrings. The exhaustive Doxygen tree is always available:

.. raw:: html

   <p><a href="../api/index.html">Browse the full Doxygen reference (/api)</a>.</p>

.. toctree::
   :hidden:
   :maxdepth: 1

   python

.. doxygenclass:: real::basic_regex
   :project: real

.. A docs/site/api/* source page is impossible: build/site/html/api/ is the
   wholesale Doxygen copy -- guaranteed build collision. The doxygenclass above
   keeps the Breathe pipeline exercised on every build; the four \ref labels it
   leaves unrendered are conf.py's nitpick_ignore entries.
