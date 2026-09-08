# Data dictionary

`analysis/source_data` contains the normalized records used to derive the
paper's tables and figures.  These are the completed instance-level result
records rather than sampled examples; publication-ready tables and lightweight
figures are retained in `analysis/tables` and `analysis/figures`.

- `all_60s_cleaned_results_long.csv` and
  `all_300s_cleaned_results_long.csv`: one row per solver, track, and instance.
- `all_60s_timing_corrections.csv`: endpoint-jitter audit.  Thirteen recorded
  output events were normalized to the implemented 60-s endpoint.
- `ablation/*.csv`: canonical component and factorial cells on the common
  23-WPMS instance set.
- `cross_kernel/*.csv`: the four cells of the NuWLS/CCEHC crossed experiment.
- `sensitivity/parameter_summary.csv`: BASE plus 29 frozen one-factor variants.
- `python_*.csv`: derived summaries generated from the instance records.

Textual labels and source-path metadata were sanitized for public release.
Experimental measurements and numeric result fields were retained unchanged;
the release process verifies this with per-file numeric fingerprints.

The files under `extensions/two_core_prototype/results` belong only to the
two-core extension and are intentionally excluded from the primary analysis
directory.

Exploratory datasets, dedicated development manifests, and third-party WCNF
benchmark instances are not redistributed in this repository.
