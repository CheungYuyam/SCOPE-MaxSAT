SCOPE-CCEHC: SCOPE-MaxSAT with the CCEHC refinement kernel

Build outputs:
  scope-ccehc   SCOPE-MaxSAT controller and hard-clause projection
  ccehc-seeded  frozen CCEHC refinement child

Linux:
  make -j4

Windows with MinGW-w64:
  mingw32-make -j4

Keep ccehc-seeded beside scope-ccehc. The experiment fixes CCEHC parameters at
p=0.279 and sp=0.085 unless the runner is explicitly given other values. Child
searches are collected sequentially; only one search process is active.
