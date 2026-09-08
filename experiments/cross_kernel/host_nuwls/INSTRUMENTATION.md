# Official NuWLS baseline provenance

- Upstream repository: https://github.com/filyouzicha/NuWLS
- Upstream commit inspected: 62e858063e867b8d8fda219e629daf908156d7dc
- Source snapshot SHA-256:
  B1D086250868D341DB47F46046F76AEFF37EC90D5969C42084AB661EBF9ACBE0

The upstream search, weighting, decimation, and variable-selection logic is
unchanged. The local experiment instrumentation only:

1. accepts seed and cutoff as direct command-line arguments;
2. uses portable steady-clock timing;
3. checks the cutoff every 1000 flips;
4. prints and internally verifies the final assignment;
5. prints provenance and single-process markers.

This program is used as the uncontrolled NuWLS cell in the crossed-kernel
experiment. It is not used as a substitute for the primary SCOPE-MaxSAT solver.
