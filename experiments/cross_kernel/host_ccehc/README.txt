Host-CCEHC: the CCEHC refinement kernel without the SCOPE-MaxSAT controller

Build outputs:
  ccehc-host    wall-clock host wrapper
  ccehc-seeded  frozen CCEHC program used by the wrapper

Linux:
  make -j4

Windows with MinGW-w64:
  mingw32-make -j4

Keep ccehc-seeded beside ccehc-host. The experiment fixes CCEHC parameters at
p=0.279 and sp=0.085 unless the runner is explicitly given other values.
