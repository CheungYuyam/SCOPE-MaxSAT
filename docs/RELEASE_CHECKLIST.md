# Public release checklist

- [x] Audit the official NuWLS repository and record the absence of an explicit
  upstream license as of 2026-09-08.
- [ ] Obtain written NuWLS redistribution permission or confirm an upstream
  license that covers the included and modified source files.
- [x] Record the private release-candidate repository URL in `CITATION.cff`.
- [x] Run `python analysis/scripts/verify_results.py`.
- [x] Run `python extensions/two_core_prototype/audit_results.py` separately.
- [x] Rebuild the primary solver and independent verifier from a clean tree.
- [x] Run the synthetic smoke instance and verify its output.
- [x] Compare all released CSV numeric fingerprints with the pre-cleaning
  fingerprints.
- [x] Confirm that no executables, object files, logs, private paths, tokens, or
  benchmark instances are staged.
- [x] Confirm that internal version labels and deprecated experiment package
  names are absent from tracked files.
- [x] Create the archival ZIP and record its SHA-256 checksum.

The unchecked NuWLS authorization item is a hard publication gate.  The full
release candidate may be archived privately, but it must not be pushed to a
public repository until that item is resolved.
