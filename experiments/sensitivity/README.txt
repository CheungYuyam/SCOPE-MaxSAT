SCOPE-MaxSAT single-core parameter-sensitivity solver

Linux:
  make -j4
  ./nuwls instance.wcnf 1 60

Windows with MinGW-w64:
  mingw32-make -j4
  .\nuwls.exe instance.wcnf 1 60

Run the scripts in the parent folder. Keep nuwls-core, nuwls-quality-core and
wcnf-verify beside nuwls. The default configuration is the frozen SCOPE-MaxSAT setup;
the sensitivity runner changes exactly one declared parameter per run.
No Python or pthread package is required.
