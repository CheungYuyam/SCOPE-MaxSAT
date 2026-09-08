# Reproducibility guide

## Primary artifact

The paper evaluates the sequential implementation in `solver/single_core`.
Build it with a C++11 compiler and GNU Make (or MinGW Make on Windows):

```sh
make -C solver/single_core -j4
make -C tools/verifier
```

Run the solver as:

```sh
./solver/single_core/scope-maxsat INSTANCE.wcnf SEED CUTOFF_SECONDS > run.log
./tools/verifier/wcnf-verify INSTANCE.wcnf run.log
```

The wrapper and both NuWLS child executables must remain in the same directory.
Parsing, hard projection, search, and finalization are charged to the wrapper's
wall-clock budget.

## Result audit

```sh
python -m pip install -r requirements.txt
python analysis/scripts/normalize_60s_timestamps.py
python analysis/scripts/verify_results.py
python analysis/scripts/generate_experiment_evidence.py
```

The normalization script is idempotent: the distributed 60-s data are already
normalized, while the retained correction log records the original endpoint
jitter.

## Cross-kernel experiment

The four cells are separately buildable:

```sh
make -C experiments/cross_kernel/host_nuwls
make -C experiments/cross_kernel/scope_nuwls
make -C experiments/cross_kernel/host_ccehc
make -C experiments/cross_kernel/scope_ccehc
```

Use `experiments/cross_kernel/run_cross_kernel.sh` or its PowerShell equivalent
to run a directory or manifest through all four cells.

## Two-core extension

The extension in `extensions/two_core_prototype` concurrently runs two search
processes.  It has a different resource envelope and is not part of the
single-core main comparison.  Build and audit it separately:

```sh
make -C extensions/two_core_prototype
python extensions/two_core_prototype/audit_results.py
```
