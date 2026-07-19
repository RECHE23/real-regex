API reference
=============

The symbol reference for real::regex. C++ symbols render below through the
Doxygen-XML -> Breathe -> Sphinx pipeline, straight from the header comments --
never hand-transcribed. A curated, per-task reference is planned (doc-site P5);
the exhaustive Doxygen tree is always available:

.. raw:: html

   <p><a href="../api/index.html">Browse the full Doxygen reference (/api)</a>.</p>

.. doxygenclass:: real::basic_regex
   :project: real

.. doc-site P1 reorg: this page absorbed the former "proof of the Breathe
   pipeline" page -- same file, same URL, rewritten as the API-reference entry
   (a new docs/site/api/* source page is impossible: build/site/html/api/ is
   the wholesale Doxygen copy, a guaranteed build collision). The doxygenclass
   above keeps the Breathe pipeline exercised on every build. ``basic_regex``'s
   Doxygen comment ``\ref``-links four symbols this page does not render
   (``real::regex``, ``real::static_regex``, the two Storage policies); see
   ``conf.py``'s ``nitpick_ignore`` for the four resulting Breathe labels.
