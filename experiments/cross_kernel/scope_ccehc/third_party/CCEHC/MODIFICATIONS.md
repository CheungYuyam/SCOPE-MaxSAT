# Experimental adapter modifications

This is a modified CCEHC build used only for the SCOPE cross-kernel transfer
experiment. It is not presented as the official CCEHC release.

Changes relative to upstream commit `38d19ad8261acbe986a01dca723a60f8f18562c8`:

1. Added optional `-seed-file <path>` input. The file contains exactly one
   binary value per WCNF variable. Only the first CCEHC restart uses this
   controller-provided assignment; later restarts retain upstream random
   initialization.
2. Replaced Linux `times()` CPU timing with portable
   `std::chrono::steady_clock` wall-clock timing and allowed fractional cutoff
   seconds.
3. Added the first-achievement wall-clock time to each `o <cost> <time>` line.
4. Printed the final assignment as `v <binary_assignment>` and emitted
   `c final internal_verified 1` only after CCEHC's own verifier accepts it.

The CCEHC flip selection, clause weighting, configuration checking, smoothing,
and restart logic are otherwise unchanged. The outer SCOPE controller performs
an additional independent WCNF verification before accepting the candidate.
