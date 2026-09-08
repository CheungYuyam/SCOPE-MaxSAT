SCOPE-NuWLS: SCOPE-MaxSAT with the NuWLS refinement kernel

Build outputs:
  nuwls               SCOPE-MaxSAT controller
  nuwls-core          NuWLS feasibility/baseline child
  nuwls-quality-core  NuWLS seeded quality-refinement child

Linux:
  make -j4

Windows with MinGW-w64:
  mingw32-make -j4

The experiment runner launches only nuwls. Keep both child programs beside it.
All child searches are scheduled sequentially; the controller never keeps two
search processes active at the same time.
