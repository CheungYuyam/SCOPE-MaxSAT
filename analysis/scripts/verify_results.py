#!/usr/bin/env python3
"""Validate the public SCOPE-MaxSAT result archive from instance-level CSVs."""

from __future__ import annotations

import sys
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source_data"
MODELS = ["SCOPE-MaxSAT", "NuWLS", "SATLike3.0", "BandMaxSAT", "NuWLS-c-2023", "NuWLS-c-FPS", "SPB-MaxSAT-c-FPS", "SPB-MaxSAT-c-Band"]
TRACK_SIZES = {"23-wpms": 160, "23-pms": 179, "24-wpms": 229, "24-pms": 216}
EXPECTED_SCOPE_COVERAGE = {60: 751, 300: 772}
EXPECTED_CROSS_KERNEL = {"host_nuwls": 132, "scope_nuwls": 135, "host_ccehc": 53, "scope_ccehc": 134}


class Audit:
    def __init__(self) -> None:
        self.failures: list[str] = []
        self.passes = 0

    def check(self, condition: bool, message: str) -> None:
        if condition:
            self.passes += 1
            print(f"PASS  {message}")
        else:
            self.failures.append(message)
            print(f"FAIL  {message}")

    def equal(self, actual: object, expected: object, message: str) -> None:
        if actual == expected:
            self.check(True, message)
            return
        actual_text = repr(actual)
        expected_text = repr(expected)
        if len(actual_text) > 200:
            actual_text = actual_text[:197] + "..."
        if len(expected_text) > 200:
            expected_text = expected_text[:197] + "..."
        self.check(False, f"{message} (actual={actual_text}, expected={expected_text})")


def as_bool(series: pd.Series) -> pd.Series:
    if pd.api.types.is_bool_dtype(series):
        return series.fillna(False)
    return series.astype(str).str.strip().str.lower().isin({"true", "1", "yes"})


def read(relative: str) -> pd.DataFrame:
    path = SOURCE / relative
    if not path.is_file():
        raise FileNotFoundError(path)
    return pd.read_csv(path)


def verify_main_comparison(audit: Audit, cutoff: int) -> None:
    data = read(f"all_{cutoff}s_cleaned_results_long.csv")
    strict_feasible = f"FeasibleStrict{cutoff}"
    strict_quality = f"QualityStrict{cutoff}"
    audit.equal(len(data), 6272, f"{cutoff}-s archive has 6,272 solver-instance records")
    audit.equal(set(data["Model"].unique()), set(MODELS), f"{cutoff}-s archive has the eight declared solvers")
    audit.equal(int(data.duplicated(["Dataset", "Model", "InstanceKey"]).sum()), 0, f"{cutoff}-s archive has no duplicate solver-instance records")
    expected_tracks = {f"{name}-{cutoff}s": size for name, size in TRACK_SIZES.items()}
    audit.equal(set(data["Dataset"].unique()), set(expected_tracks), f"{cutoff}-s archive has the four declared tracks")
    for track, size in expected_tracks.items():
        block = data[data["Dataset"].eq(track)]
        audit.equal(block["InstanceKey"].nunique(), size, f"{track} contains {size} unique instances")
        counts = block.groupby("Model")["InstanceKey"].nunique().to_dict()
        audit.check(all(counts.get(model) == size for model in MODELS), f"{track} pairs every solver on the same {size} instances")
    per_model = data.groupby("Model").size().to_dict()
    audit.check(all(per_model.get(model) == 784 for model in MODELS), f"each {cutoff}-s solver has 784 records")
    scope = data[data["Model"].eq("SCOPE-MaxSAT")]
    audit.equal(int(as_bool(scope[strict_feasible]).sum()), EXPECTED_SCOPE_COVERAGE[cutoff], f"SCOPE-MaxSAT verified coverage at {cutoff} s")
    times = pd.to_numeric(data["BestTimeSeconds"], errors="coerce")
    eligible = as_bool(data[strict_quality])
    audit.equal(int((eligible & times.gt(cutoff)).sum()), 0, f"strict {cutoff}-s quality records respect the deadline")
    audit.check(not eligible[~as_bool(data[strict_feasible])].any(), f"strict {cutoff}-s quality implies verified feasibility")
    if cutoff == 60:
        audit.equal(int((as_bool(data["QualityReported"]) & times.gt(60)).sum()), 0, "normalized 60-s reported quality timestamps do not exceed 60 s")
        corrections = read("all_60s_timing_corrections.csv")
        audit.equal(len(corrections), 13, "the 60-s timing audit preserves 13 jitter corrections")
        originals = pd.to_numeric(corrections["OriginalBestTimeSeconds"], errors="coerce")
        normalized = pd.to_numeric(corrections["NormalizedBestTimeSeconds"], errors="coerce")
        audit.check(originals.gt(60).all() and normalized.eq(60).all(), "all timing corrections map an endpoint overrun to 60 s")


def verify_run_family(audit: Audit, directory: str, names: list[str], label: str) -> dict[str, pd.DataFrame]:
    frames: dict[str, pd.DataFrame] = {}
    reference: set[str] | None = None
    for name in names:
        frame = read(f"{directory}/{name}.csv")
        frames[name] = frame
        key = "Instance" if "Instance" in frame.columns else "InstanceKey"
        instances = set(frame[key].astype(str))
        audit.equal(len(frame), 160, f"{label} {name} has 160 records")
        audit.equal(len(instances), 160, f"{label} {name} has 160 unique instances")
        if reference is None:
            reference = instances
        else:
            audit.equal(instances, reference, f"{label} {name} uses the common instance set")
    return frames


def verify_ablation(audit: Audit) -> None:
    names = [f"A{i}" for i in range(8)] + ["Y00", "Y01", "Y10", "Y11"]
    frames = verify_run_family(audit, "ablation", names, "ablation cell")
    audit.check(frames["Y00"]["Instance"].equals(frames["A0"]["Instance"]), "factorial endpoint Y00 is aligned with canonical A0")
    audit.check(frames["Y11"]["Instance"].equals(frames["A7"]["Instance"]), "factorial endpoint Y11 is aligned with canonical A7")


def verify_cross_kernel(audit: Audit) -> None:
    names = ["host_nuwls", "scope_nuwls", "host_ccehc", "scope_ccehc"]
    frames = verify_run_family(audit, "cross_kernel", names, "cross-kernel cell")
    observed: dict[str, int] = {}
    for name, frame in frames.items():
        observed[name] = int(as_bool(frame["HasVerifiedSolution"]).sum())
        audit.equal(observed[name], EXPECTED_CROSS_KERNEL[name], f"cross-kernel verified coverage for {name}")
    summary = read("python_cross_kernel_summary.csv").set_index("Kernel")
    audit.equal(int(summary.loc["NuWLS", "HostFeasible"]), observed["host_nuwls"], "NuWLS host summary matches instance records")
    audit.equal(int(summary.loc["NuWLS", "ScopeFeasible"]), observed["scope_nuwls"], "SCOPE+NuWLS summary matches instance records")
    audit.equal(int(summary.loc["CCEHC", "HostFeasible"]), observed["host_ccehc"], "CCEHC host summary matches instance records")
    audit.equal(int(summary.loc["CCEHC", "ScopeFeasible"]), observed["scope_ccehc"], "SCOPE+CCEHC summary matches instance records")


def verify_sensitivity(audit: Audit) -> None:
    data = read("sensitivity/parameter_summary.csv")
    audit.equal(len(data), 30, "parameter screen contains BASE plus 29 one-factor variants")
    audit.equal(data["ConfigID"].nunique(), 30, "parameter-screen configuration identifiers are unique")
    audit.equal(int(data["ConfigID"].eq("BASE").sum()), 1, "parameter screen contains one frozen BASE configuration")
    base = data[data["ConfigID"].eq("BASE")].iloc[0]
    audit.equal(str(base["ParameterValue"]), "frozen", "BASE uses a public frozen-configuration label")


def main() -> int:
    audit = Audit()
    try:
        verify_main_comparison(audit, 60)
        verify_main_comparison(audit, 300)
        verify_ablation(audit)
        verify_cross_kernel(audit)
        verify_sensitivity(audit)
    except Exception as exc:
        audit.failures.append(f"audit aborted: {exc}")
        print(f"FAIL  audit aborted: {exc}")
    print(f"\n{audit.passes} checks passed; {len(audit.failures)} failed.")
    if audit.failures:
        for failure in audit.failures:
            print(f"  - {failure}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
