#!/usr/bin/env python3
"""Check the two-core extension results against their published summaries.

This audit is deliberately kept outside ``analysis/scripts`` because the
two-core implementation is an optional extension, not the paper's primary
resource-matched solver.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"
MAIN_SOURCE = HERE.parents[1] / "analysis" / "source_data" / "all_60s_cleaned_results_long.csv"


def as_bool(series: pd.Series) -> pd.Series:
    if pd.api.types.is_bool_dtype(series):
        return series.fillna(False)
    return series.astype(str).str.strip().str.lower().isin({"true", "1", "yes"})


def fail(message: str) -> None:
    raise AssertionError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)
    print(f"PASS  {message}")


def summarize_pair(block: pd.DataFrame) -> dict[str, int | float]:
    two_feasible = as_bool(block["DualCoreFeasible"])
    one_feasible = as_bool(block["SingleCoreFeasible"])
    both = two_feasible & one_feasible
    two_cost = pd.to_numeric(block["DualCoreBestCost"], errors="coerce")
    one_cost = pd.to_numeric(block["SingleCoreBestCost"], errors="coerce")
    delta = 100.0 * (two_cost[both] - one_cost[both]) / one_cost[both]
    return {
        "Instances": len(block),
        "DualCoreFeasible": int(two_feasible.sum()),
        "SingleCoreFeasible": int(one_feasible.sum()),
        "FeasibilityDifferenceDualMinusSingle": int(two_feasible.sum() - one_feasible.sum()),
        "BothFeasible": int(both.sum()),
        "DualCoreOnly": int((two_feasible & ~one_feasible).sum()),
        "SingleCoreOnly": int((~two_feasible & one_feasible).sum()),
        "NeitherFeasible": int((~two_feasible & ~one_feasible).sum()),
        "DualCoreQualityWins": int((both & two_cost.lt(one_cost)).sum()),
        "SingleCoreQualityWins": int((both & one_cost.lt(two_cost)).sum()),
        "QualityTies": int((both & one_cost.eq(two_cost)).sum()),
        "MedianDualMinusSingleCostPct": float(np.median(delta)) if len(delta) else float("nan"),
    }


def main() -> int:
    normalized = pd.read_csv(RESULTS / "two_core_normalized.csv")
    summary = pd.read_csv(RESULTS / "two_core_summary.csv")
    paired = pd.read_csv(RESULTS / "two_core_vs_single_core_paired.csv")
    paired_summary = pd.read_csv(RESULTS / "two_core_vs_single_core_summary.csv")
    single = pd.read_csv(MAIN_SOURCE)

    require(len(normalized) == 784, "two-core archive contains 784 instance records")
    require(not normalized.duplicated(["Dataset", "InstanceKey"]).any(), "two-core instance identifiers are unique within each track")
    require(len(paired) == 784, "paired extension comparison contains 784 records")
    require(not paired.duplicated(["Dataset", "InstanceKey"]).any(), "paired extension comparison has no duplicate records")

    single_scope = single[single["Model"].eq("SCOPE-MaxSAT")][["Dataset", "InstanceKey", "FeasibleStrict60", "BestCost"]].copy()
    require(len(single_scope) == 784, "single-core reference contains 784 SCOPE-MaxSAT records")
    keys = set(zip(normalized["Dataset"], normalized["InstanceKey"]))
    single_keys = set(zip(single_scope["Dataset"], single_scope["InstanceKey"]))
    require(keys == single_keys, "two-core and single-core records use the same four-track corpus")

    summary_by_track = summary.set_index("Dataset")
    pair_summary_by_track = paired_summary.set_index("Dataset")
    for track, block in normalized.groupby("Dataset", sort=False):
        require(len(block) == int(summary_by_track.loc[track, "Instances"]), f"{track} instance count matches the extension summary")
        feasible = int(as_bool(block["Feasible"]).sum())
        require(feasible == int(summary_by_track.loc[track, "VerifiedFeasible"]), f"{track} verified coverage matches the extension summary")

    for label, block in list(paired.groupby("Dataset", sort=False)) + [("All four tracks", paired)]:
        calculated = summarize_pair(block)
        recorded = pair_summary_by_track.loc[label]
        for field, value in calculated.items():
            target = recorded[field]
            if isinstance(value, float):
                require(np.isclose(value, float(target), equal_nan=True), f"{label} {field} matches the paired summary")
            else:
                require(value == int(target), f"{label} {field} matches the paired summary")

    print("\nTwo-core extension audit completed successfully.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"FAIL  {exc}", file=sys.stderr)
        sys.exit(1)
