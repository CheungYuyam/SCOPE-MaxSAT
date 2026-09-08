Host-NuWLS: instrumented official NuWLS baseline

Build output:
  official-nuwls

Linux:
  make -j4

Windows with MinGW-w64:
  mingw32-make -j4

The instrumentation accepts instance, seed, and cutoff arguments and prints a
final assignment for independent verification. Search, weighting, decimation,
and variable-selection logic remain unchanged. See INSTRUMENTATION.md for the
upstream repository, commit, and archive checksum.
