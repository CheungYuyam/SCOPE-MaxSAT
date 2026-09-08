# SCOPE-MaxSAT

SCOPE-MaxSAT is a feasibility-preserving sequential controller for weighted
partial MaxSAT.  The primary artifact is the single-core implementation in
[`solver/single_core`](solver/single_core).  It combines hard-clause
projection, verified state retention, and seeded NuWLS refinement while
running at most one search engine at a time.

## Repository layout

- `solver/single_core`: primary resource-matched SCOPE-MaxSAT implementation.
- `experiments/ablation`: component and factorial configurations.
- `experiments/sensitivity`: frozen one-factor parameter screen.
- `experiments/cross_kernel`: NuWLS/CCEHC crossed transfer experiment.
- `analysis/source_data`: normalized instance-level records used in the paper.
- `analysis/tables`: publication-ready LaTeX tables derived from those records.
- `analysis/figures`: publication-ready PDF, PNG, and SVG figures.
- `analysis/scripts`: result verification and figure/table generation.
- `tools/verifier`: independent WCNF assignment and objective verifier.
- `extensions/two_core_prototype`: separately documented two-core extension.

The two-core extension is not interchangeable with the paper's primary
single-core implementation.  Its code, results, and audit are isolated under
`extensions/two_core_prototype`.

The released result archive includes the complete instance-level outputs used
for the paper, together with their derived summaries, tables, and lightweight
figures.  Exploratory datasets and their dedicated run lists are not included.

## Build the primary solver

Linux:

```sh
make -C solver/single_core -j4
./solver/single_core/scope-maxsat tests/smoke.wcnf 1 60
```

Windows with MinGW-w64:

```powershell
mingw32-make -C solver/single_core -j4
solver\single_core\scope-maxsat.exe tests\smoke.wcnf 1 60
```

Keep `nuwls-core` and `nuwls-quality-core` beside the wrapper executable.  See
[`docs/REPRODUCIBILITY.md`](docs/REPRODUCIBILITY.md) for complete commands.

## Verify the released results

Install the Python dependencies and run the public audit:

```sh
python -m pip install -r requirements.txt
python analysis/scripts/verify_results.py
```

The audit checks the full 60-s and 300-s comparison records, the timing
normalization log, all ablation cells, the crossed kernel experiment, and the
parameter-screen structure.  To regenerate derived tables and figures:

```sh
python analysis/scripts/generate_experiment_evidence.py
```

## Benchmarks

Competition benchmark instances are not redistributed here.  Supply legally
obtained WCNF files through a directory or manifest; see
[`benchmarks/README.md`](benchmarks/README.md).  `tests/smoke.wcnf` is a small
synthetic instance intended only for build and interface checks.

## Third-party code and release status

This repository contains code derived from NuWLS, bundled CaDiCaL sources, and
CCEHC sources in the crossed-kernel experiment.  Their notices and license
locations are documented in [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).

The official NuWLS repository was audited on 2026-09-08.  It publishes source
code but contains no repository-level license grant, so public redistribution
of the included NuWLS-derived files still requires written authorization from
the upstream authors.  The audit record and a ready-to-send permission request
are provided in [`docs/NUWLS_LICENSE_AUDIT.md`](docs/NUWLS_LICENSE_AUDIT.md) and
[`docs/NUWLS_PERMISSION_REQUEST.md`](docs/NUWLS_PERMISSION_REQUEST.md).

Until that authorization is obtained, this checkout is a release candidate and
must not be made public.  No single repository-wide license is asserted.

## Citation

Citation metadata is provided in [`CITATION.cff`](CITATION.cff).  Repository
and publication identifiers can be added after the public archive exists.
