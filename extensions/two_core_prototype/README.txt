SCOPE-MaxSAT: optional two-core prototype

This directory is an extension of the paper's primary single-core artifact.
It starts the feasibility-oriented and quality-oriented refinement processes
concurrently and therefore must not be used as a resource-matched substitute
for the sequential implementation.

Linux:
  make clean
  make -j4
  ./scope-maxsat-two-core instance.wcnf 1 60

Windows with MinGW-w64:
  mingw32-make clean
  mingw32-make -j4
  .\scope-maxsat-two-core.exe instance.wcnf 1 60

Run only scope-maxsat-two-core. Keep nuwls-core and nuwls-quality-core beside it.
No Python or pthread package is required.
