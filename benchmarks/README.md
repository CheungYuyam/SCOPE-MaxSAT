# Benchmark input

The MaxSAT Evaluation benchmark instances are not redistributed in this
repository.  Download the required WCNF files from their authorized source and
retain the original filenames so that they can be paired with the released
instance-level records.

The generic runner accepts either:

- a directory, recursively scanned for `*.wcnf` files; or
- a UTF-8 manifest containing one WCNF path per line.

Example:

```sh
python experiments/run_benchmark.py \
  --solver solver/single_core/scope-maxsat \
  --benchmark-dir /path/to/wcnf \
  --seed 1 --cutoff 60 --output runs/scope.csv
```

Use `tests/smoke.wcnf` only to confirm that the executable and verifier work;
it is not part of the evaluation corpus.
