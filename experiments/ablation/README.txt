SCOPE-MaxSAT sequential single-core solver

Linux:
  make -j4
  ./nuwls instance.wcnf 1 60

Windows with MinGW-w64:
  mingw32-make -j4
  .\nuwls.exe instance.wcnf 1 60

Run only nuwls. Keep nuwls-core and nuwls-quality-core beside it.
The wrapper runs hard-SAT, optional baseline probing, and quality refinement
strictly sequentially. No Python or pthread package is required.
