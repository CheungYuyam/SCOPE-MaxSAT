SCOPE-MaxSAT: sequential single-core implementation

Linux:
  make -j4
  ./scope-maxsat instance.wcnf 1 60

Windows with MinGW-w64:
  mingw32-make -j4
  .\scope-maxsat.exe instance.wcnf 1 60

Run only scope-maxsat. Keep nuwls-core and nuwls-quality-core beside it.
The wrapper runs hard-SAT, optional baseline probing, and quality refinement
strictly sequentially. No Python or pthread package is required.
