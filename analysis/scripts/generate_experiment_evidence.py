#!/usr/bin/env python3
"""Generate the experiment tables and figures used by the manuscript.

The script deliberately keeps two timing concepts separate.  The eight-solver
workbooks all record the first time at which the finally returned objective was
found.  Only four workbook families also export a first-feasible timestamp.
Consequently, the main-comparison curves use the common final-output-availability
timestamp.  Ablation and cross-kernel RMST calculations use true first-feasible
timestamps recovered from their CSV fields or run logs.
"""

from __future__ import annotations

import math
import re
from pathlib import Path

import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "source_data"
TABLES = ROOT / "tables"
FIGURES = ROOT / "figures"
RNG = np.random.default_rng(20260829)

mpl.rcParams.update(
    {
        "font.family": "sans-serif",
        "font.sans-serif": ["Arial", "Helvetica", "DejaVu Sans", "sans-serif"],
        "font.size": 7,
        "axes.spines.right": False,
        "axes.spines.top": False,
        "axes.linewidth": 0.8,
        "legend.frameon": False,
        "svg.fonttype": "none",
        "pdf.fonttype": 42,
    }
)

MODEL_ORDER = [
    "SCOPE-MaxSAT",
    "NuWLS",
    "SATLike3.0",
    "BandMaxSAT",
    "NuWLS-c-2023",
    "NuWLS-c-FPS",
    "SPB-MaxSAT-c-FPS",
    "SPB-MaxSAT-c-Band",
]
SHORT = {
    "SCOPE-MaxSAT": "SCOPE-MaxSAT",
    "NuWLS": "NuWLS",
    "SATLike3.0": "SATLike3.0",
    "BandMaxSAT": "BandMaxSAT",
    "NuWLS-c-2023": "NuWLS-c-2023",
    "NuWLS-c-FPS": "NuWLS-c-FPS",
    "SPB-MaxSAT-c-FPS": "SPB-c-FPS",
    "SPB-MaxSAT-c-Band": "SPB-c-Band",
}
COLORS = {
    name: color
    for name, color in zip(
        MODEL_ORDER,
        ["#101820", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00", "#e41a1c", "#a65628", "#f781bf"],
    )
}
STYLES = {
    name: style
    for name, style in zip(MODEL_ORDER, ["-", "--", "-.", ":", "-", "--", "-.", ":"])
}

ABLATION_NAMES = {
    "A0": "Host NuWLS",
    "A1": "Projection without retention",
    "A2": "Retention without state transfer",
    "A3": "No structural schedule",
    "A4": "Objective-only weights",
    "A5": "No hard guard",
    "A6": "Uniform soft weights",
    "A7": "Full SCOPE-MaxSAT",
}

CROSS_KERNEL_ORDER = ["NuWLS", "CCEHC"]

# The A0--A7 batch is the canonical component record. The factorial table
# reuses its identical endpoint configurations so that Y00=A0 and Y11=A7
# across manuscript tables. The separately executed Y00 and Y11 CSV files
# remain in the source archive for provenance but are not reported as a second
# value for the same configuration.
FACTORIAL_ENDPOINT_SOURCE = {"Y00": "A0", "Y11": "A7"}

TRACKS = {
    60: [
        ("23-wpms-60s", "23-WPMS (160)"),
        ("23-pms-60s", "23-PMS (179)"),
        ("24-wpms-60s", "24-WPMS (229)"),
        ("24-pms-60s", "24-PMS (216)"),
    ],
    300: [
        ("23-wpms-300s", "23-WPMS (160)"),
        ("23-pms-300s", "23-PMS (179)"),
        ("24-wpms-300s", "24-WPMS (229)"),
        ("24-pms-300s", "24-PMS (216)"),
    ],
}

DISPLAY_MODEL = {
    "SCOPE-MaxSAT": "SCOPE-MaxSAT",
    "NuWLS": "NuWLS",
    "SATLike3.0": "SATLike3.0",
    "BandMaxSAT": "BandMaxSAT",
    "NuWLS-c-2023": "NuWLS-c-2023",
    "NuWLS-c-FPS": "NuWLS-c-FPS",
    "SPB-MaxSAT-c-FPS": "SPB-MaxSAT-c-FPS",
    "SPB-MaxSAT-c-Band": "SPB-MaxSAT-c-Band",
}

MAIN_TABLE_SPEC = (
    r"@{\extracolsep{\fill}}p{0.20\linewidth}"
    r"*{4}{>{\raggedleft\arraybackslash}p{0.16\linewidth}}@{}"
)


def save_publication_figure(fig: mpl.figure.Figure, stem: str) -> None:
    """Export one Python-rendered figure with editable vector text and 600-dpi raster output."""
    fig.savefig(FIGURES / f"{stem}.svg", bbox_inches="tight")
    fig.savefig(FIGURES / f"{stem}.pdf", bbox_inches="tight")
    fig.savefig(FIGURES / f"{stem}.png", dpi=300, bbox_inches="tight")
    fig.savefig(FIGURES / f"{stem}.tiff", dpi=600, bbox_inches="tight")


def latex_escape(text: str) -> str:
    return (
        text.replace("\\", r"\textbackslash{}")
        .replace("&", r"\&")
        .replace("%", r"\%")
        .replace("_", r"\_")
        .replace("#", r"\#")
    )


def as_bool(series: pd.Series) -> pd.Series:
    return series.astype(str).str.lower().isin(["true", "1", "yes"])


def exact_mcnemar_p(scope_only: int, base_only: int) -> float:
    n = scope_only + base_only
    if n == 0:
        return 1.0
    k = min(scope_only, base_only)
    tail = sum(math.comb(n, j) for j in range(k + 1)) / (2**n)
    return min(1.0, 2.0 * tail)


def exact_sign_p(wins: int, losses: int) -> float:
    """Two-sided exact sign test after excluding tied cost pairs."""
    return exact_mcnemar_p(wins, losses)


def holm(values: list[float]) -> list[float]:
    order = np.argsort(values)
    out = np.ones(len(values))
    running = 0.0
    for rank, idx in enumerate(order):
        running = max(running, (len(values) - rank) * values[idx])
        out[idx] = min(1.0, running)
    return out.tolist()


def bootstrap_mean_ci(values: np.ndarray, repetitions: int = 10000) -> tuple[float, float]:
    values = np.asarray(values, dtype=float)
    if len(values) == 0:
        return math.nan, math.nan
    samples = RNG.choice(values, size=(repetitions, len(values)), replace=True).mean(axis=1)
    return tuple(np.quantile(samples, [0.025, 0.975]))


def bootstrap_count_box(values: np.ndarray, repetitions: int = 10000) -> dict[str, float]:
    """Return box-and-whisker summaries for an absolute binary count."""
    values = np.asarray(values, dtype=bool)
    if len(values) == 0:
        return {key: math.nan for key in ["whislo", "q1", "med", "q3", "whishi"]}
    samples = RNG.choice(values, size=(repetitions, len(values)), replace=True).sum(axis=1)
    whislo, q1, med, q3, whishi = np.quantile(samples, [0.025, 0.25, 0.5, 0.75, 0.975])
    return {
        "whislo": float(whislo),
        "q1": float(q1),
        "med": float(med),
        "q3": float(q3),
        "whishi": float(whishi),
        "fliers": [],
    }


def fmt_p(value: float) -> str:
    if value < 0.001:
        return "$<0.001$"
    return f"{value:.3f}"


def rmst_from_first_time(first: pd.Series, feasible: pd.Series, cutoff: float) -> float:
    observed = pd.to_numeric(first, errors="coerce").to_numpy(dtype=float)
    feasible_array = np.asarray(feasible, dtype=bool)
    values = np.where(feasible_array & np.isfinite(observed), np.minimum(observed, cutoff), cutoff)
    return float(np.mean(values))


def main_table_summary(cutoff: int) -> list[dict[str, object]]:
    data = pd.read_csv(SOURCE / f"all_{cutoff}s_cleaned_results_long.csv")
    feasible_col = "FeasibleReported" if cutoff == 60 else "FeasibleStrict300"
    winner_col = "WinReported" if cutoff == 60 else "WinStrict300"
    rows: list[dict[str, object]] = []
    for dataset, label in TRACKS[cutoff] + [(None, "Full four-track set (784)")]:
        subset = data if dataset is None else data[data.Dataset == dataset]
        row: dict[str, object] = {"Label": label}
        for model in MODEL_ORDER:
            model_data = subset[subset.Model == model]
            feasible = as_bool(model_data[feasible_col])
            winners = as_bool(model_data[winner_col])
            winning_times = pd.to_numeric(
                model_data.loc[winners, "BestTimeSeconds"], errors="coerce"
            ).to_numpy(dtype=float)
            winning_times = winning_times[np.isfinite(winning_times)]
            if cutoff == 60:
                winning_times = np.minimum(winning_times, 60.0)
            row[model] = {
                "Feasible": int(feasible.sum()),
                "Wins": int(winners.sum()),
                "MeanWinningTime": float(np.mean(winning_times)) if len(winning_times) else math.nan,
            }
        rows.append(row)
    return rows


def main_table_header(models: list[str]) -> str:
    return "Benchmark ($\\#\\mathrm{inst.}$) & " + " & ".join(
        DISPLAY_MODEL[model] for model in models
    ) + r" \\"


def write_main_feasibility_table(rows: list[dict[str, object]], cutoff: int) -> None:
    if cutoff == 60:
        caption = (
            "Verified/reported feasible assignments in the 60-s comparison. "
            "The four complete tracks contain 784 benchmark--instance pairs. Bold denotes "
            "the largest count within a row across all eight solvers. Panel (a) contains "
            "SCOPE-MaxSAT and the published-method baselines. Panel (b) contains the "
            "competition-winning implementations."
        )
        label = "tab:main-60s-feasibility"
    else:
        caption = (
            "Hard-feasible assignments under the strict 300-s cutoff. The four complete "
            "tracks contain 784 "
            "benchmark--instance pairs. Bold denotes the largest count within a row across "
            "all eight solvers. Panel (a) contains SCOPE-MaxSAT and published methods. "
            "Panel (b) contains the competition-winning implementations."
        )
        label = "tab:main-300s-feasibility"
    panels = [
        ("(a) SCOPE-MaxSAT and published methods", MODEL_ORDER[:4]),
        ("(b) Competition-winning implementations", MODEL_ORDER[4:]),
    ]
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        f"  \\caption{{{caption}}}",
        f"  \\label{{{label}}}",
        r"  \scriptsize",
    ]
    for panel_index, (panel_title, models) in enumerate(panels):
        if panel_index:
            lines.extend(["", r"  \vspace{0.8em}", ""])
        lines.extend(
            [
                rf"  \begin{{tabular*}}{{\linewidth}}{{{MAIN_TABLE_SPEC}}}",
                r"    \toprule",
                rf"    \multicolumn{{5}}{{@{{}}l}}{{\textbf{{{panel_title}}}}} \\",
                r"    \midrule",
                "    " + main_table_header(models),
                r"    \midrule",
            ]
        )
        for row_index, row in enumerate(rows):
            values = [int(row[model]["Feasible"]) for model in MODEL_ORDER]
            maximum = max(values)
            cells = []
            for model in models:
                value = int(row[model]["Feasible"])
                cells.append(rf"\textbf{{{value}}}" if value == maximum else str(value))
            if row_index == len(rows) - 1:
                lines.append(r"    \midrule")
            lines.append(f"    {row['Label']} & " + " & ".join(cells) + r" \\")
        lines.extend([r"    \bottomrule", r"  \end{tabular*}"])
    lines.extend([r"\end{table}", ""])
    (TABLES / f"main_{cutoff}s_feasibility.tex").write_text("\n".join(lines), encoding="utf-8")


def write_main_quality_table(rows: list[dict[str, object]], cutoff: int) -> None:
    if cutoff == 60:
        caption = (
            "Tie-inclusive objective wins and time to the winning objective in the common "
            "eight-solver comparison. Each entry is "
            "$N_{\\mathrm{win}}/\\overline t_{\\mathrm{win}}$, with "
            "$\\overline t_{\\mathrm{win}}$ measured in seconds. A tied minimum "
            "counts as a win for every tied solver. Bold denotes the largest win count in "
            "each row. The conditional times are not ranked because the winning sets "
            "differ between solvers. Both panels use the same common eight-solver winner pool."
        )
        label = "tab:main-60s-quality"
    else:
        caption = (
            "Tie-inclusive objective wins under the strict 300-s cutoff and time to the "
            "winning objective. Each entry is "
            "$N_{\\mathrm{win}}/\\overline t_{\\mathrm{win}}$, with "
            "$\\overline t_{\\mathrm{win}}$ measured in seconds. A tied minimum "
            "counts as a win for every tied solver. Bold denotes the largest "
            "win count in each row. Conditional times are not ranked because winning sets differ."
        )
        label = "tab:main-300s-quality"
    panels = [
        ("(a) SCOPE-MaxSAT and published methods", MODEL_ORDER[:4]),
        ("(b) Competition-winning implementations", MODEL_ORDER[4:]),
    ]
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        f"  \\caption{{{caption}}}",
        f"  \\label{{{label}}}",
        r"  \scriptsize",
    ]
    for panel_index, (panel_title, models) in enumerate(panels):
        if panel_index:
            lines.extend(["", r"  \vspace{0.8em}", ""])
        lines.extend(
            [
                rf"  \begin{{tabular*}}{{\linewidth}}{{{MAIN_TABLE_SPEC}}}",
                r"    \toprule",
                rf"    \multicolumn{{5}}{{@{{}}l}}{{\textbf{{{panel_title}}}}} \\",
                r"    \midrule",
                "    " + main_table_header(models),
                r"    \midrule",
            ]
        )
        for row_index, row in enumerate(rows):
            maximum = max(int(row[model]["Wins"]) for model in MODEL_ORDER)
            cells = []
            for model in models:
                wins = int(row[model]["Wins"])
                mean_time = float(row[model]["MeanWinningTime"])
                count = rf"\textbf{{{wins}}}" if wins == maximum else str(wins)
                cells.append(f"{count}/{mean_time:.2f}")
            if row_index == len(rows) - 1:
                lines.append(r"    \midrule")
            lines.append(f"    {row['Label']} & " + " & ".join(cells) + r" \\")
        lines.extend([r"    \bottomrule", r"  \end{tabular*}"])
    lines.extend([r"\end{table}", ""])
    (TABLES / f"main_{cutoff}s_quality.tex").write_text("\n".join(lines), encoding="utf-8")


def write_main_comparison_tables(cutoff: int) -> None:
    rows = main_table_summary(cutoff)
    write_main_feasibility_table(rows, cutoff)
    write_main_quality_table(rows, cutoff)


def main_pairwise(cutoff: int) -> pd.DataFrame:
    suffix = "60" if cutoff == 60 else "300"
    feasible_col = "FeasibleReported" if cutoff == 60 else "FeasibleStrict300"
    quality_col = "QualityReported" if cutoff == 60 else "QualityStrict300"
    data = pd.read_csv(SOURCE / f"all_{suffix}s_cleaned_results_long.csv")
    scope = data[data.Model == "SCOPE-MaxSAT"].set_index(["Dataset", "InstanceKey"])
    rows = []
    raw_feas_p = []
    raw_quality_p = []
    for baseline in MODEL_ORDER[1:]:
        base = data[data.Model == baseline].set_index(["Dataset", "InstanceKey"])
        joined = scope.join(base, lsuffix="_scope", rsuffix="_base", how="inner")
        sf = joined[f"{feasible_col}_scope"].astype(bool).to_numpy()
        bf = joined[f"{feasible_col}_base"].astype(bool).to_numpy()
        scope_only = int(np.sum(sf & ~bf))
        base_only = int(np.sum(~sf & bf))
        paired_delta = sf.astype(int) - bf.astype(int)
        ci_low, ci_high = bootstrap_mean_ci(paired_delta)
        common = (
            joined[f"{quality_col}_scope"].astype(bool)
            & joined[f"{quality_col}_base"].astype(bool)
        )
        scope_cost = pd.to_numeric(joined.loc[common, "BestCost_scope"], errors="coerce")
        base_cost = pd.to_numeric(joined.loc[common, "BestCost_base"], errors="coerce")
        valid = scope_cost.notna() & base_cost.notna()
        scope_cost = scope_cost[valid].to_numpy(dtype=float)
        base_cost = base_cost[valid].to_numpy(dtype=float)
        relative = 100.0 * (scope_cost - base_cost) / np.maximum(1.0, np.abs(base_cost))
        wins = int(np.sum(scope_cost < base_cost))
        losses = int(np.sum(scope_cost > base_cost))
        ties = int(np.sum(scope_cost == base_cost))
        qp = exact_sign_p(wins, losses)
        mp = exact_mcnemar_p(scope_only, base_only)
        raw_feas_p.append(mp)
        raw_quality_p.append(qp)
        rows.append(
            {
                "Baseline": baseline,
                "ScopeOnly": scope_only,
                "BaselineOnly": base_only,
                "Delta": scope_only - base_only,
                "DeltaCILow": len(joined) * ci_low,
                "DeltaCIHigh": len(joined) * ci_high,
                "McNemarP": mp,
                "Common": len(scope_cost),
                "Wins": wins,
                "Losses": losses,
                "Ties": ties,
                "MedianRelativeCostPct": float(np.median(relative)) if len(relative) else math.nan,
                "SignTestP": qp,
            }
        )
    feas_holm = holm(raw_feas_p)
    quality_holm = holm(raw_quality_p)
    for row, fp, qp in zip(rows, feas_holm, quality_holm):
        row["FeasibilityHolmP"] = fp
        row["QualityHolmP"] = qp
    return pd.DataFrame(rows)


def write_pairwise_table(frame: pd.DataFrame, cutoff: int) -> None:
    quality_scope = "common feasible pairs" if cutoff == 60 else "common strict-cutoff feasible pairs"
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        rf"  \caption{{Paired SCOPE-MaxSAT contrasts over all 784 instances at {cutoff}~s. The feasibility interval is a paired instance-bootstrap 95\% interval for the count difference. $W/L/T$ compares original WCNF cost on {quality_scope}. Negative median relative cost means a lower SCOPE-MaxSAT cost. McNemar and two-sided exact sign-test $p$ values are Holm adjusted across the seven baselines.}}",
        rf"  \label{{tab:pairwise-{cutoff}s}}",
        r"  \scriptsize",
        r"  \begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}lrrrrr@{}}",
        r"    \toprule",
        r"    \multicolumn{6}{@{}l}{\textbf{(a) Paired feasibility}} \\",
        r"    \midrule",
        r"    Baseline & SCOPE only & Base only & $\Delta N_{\mathrm{feas}}$ & 95\% CI & McNemar \\",
        r"    \midrule",
    ]
    for row in frame.itertuples(index=False):
        lines.append(
            "    "
            + latex_escape(SHORT[row.Baseline])
            + f" & {row.ScopeOnly} & {row.BaselineOnly} & {row.Delta:+d}"
            + f" & [{row.DeltaCILow:.0f}, {row.DeltaCIHigh:.0f}]"
            + f" & {fmt_p(row.FeasibilityHolmP)} \\\\"
        )
    lines.extend(
        [
            r"    \bottomrule",
            r"  \end{tabular*}",
            r"  \vspace{0.65em}",
            r"  \begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}lrrrr@{}}",
            r"    \toprule",
            r"    \multicolumn{5}{@{}l}{\textbf{(b) Conditional quality}} \\",
            r"    \midrule",
            r"    Baseline & Common & $W/L/T$ & Median $\Delta$cost (\%) & Sign test \\",
            r"    \midrule",
        ]
    )
    for row in frame.itertuples(index=False):
        lines.append(
            "    "
            + latex_escape(SHORT[row.Baseline])
            + f" & {row.Common} & {row.Wins}/{row.Losses}/{row.Ties}"
            + f" & {row.MedianRelativeCostPct:.2f} & {fmt_p(row.QualityHolmP)} \\\\"
        )
    lines.extend([r"    \bottomrule", r"  \end{tabular*}", r"\end{table}", ""])
    (TABLES / f"pairwise_{cutoff}s.tex").write_text("\n".join(lines), encoding="utf-8")


def plot_main_availability() -> None:
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.05), sharey=True)
    for ax, cutoff in zip(axes, [60, 300]):
        data = pd.read_csv(SOURCE / f"all_{cutoff}s_cleaned_results_long.csv")
        quality_col = "QualityReported" if cutoff == 60 else "QualityStrict300"
        for model in MODEL_ORDER:
            group = data[data.Model == model]
            times = pd.to_numeric(
                group.loc[group[quality_col].astype(bool), "BestTimeSeconds"], errors="coerce"
            ).to_numpy(dtype=float)
            times = np.sort(times[np.isfinite(times)])
            if cutoff == 60:
                times = np.minimum(times, 60.0)
            x = np.r_[0.0, times, cutoff]
            y = np.r_[0, np.arange(1, len(times) + 1), len(times)]
            ax.step(x, y, where="post", color=COLORS[model], linestyle=STYLES[model], linewidth=1.35, label=SHORT[model])
        ax.set_xlim(0, cutoff)
        ax.set_ylim(0, 800)
        ax.set_xlabel("Recorded final-output availability time (s)")
        ax.set_title(f"{cutoff}-s cutoff")
        ax.grid(axis="y", color="#d9d9d9", linewidth=0.6)
        ax.text(0.03, 0.95, "ab"[0 if cutoff == 60 else 1], transform=ax.transAxes, fontweight="bold", va="top")
    axes[0].set_ylabel("Cumulative verified outputs")
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, ncol=4, loc="lower center", bbox_to_anchor=(0.5, -0.06), frameon=False, fontsize=7.3)
    fig.tight_layout(rect=[0, 0.12, 1, 1])
    save_publication_figure(fig, "main_solution_availability")
    plt.close(fig)


def plot_pairwise_summary(frames: dict[int, pd.DataFrame]) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.75))
    y_feas = np.arange(len(MODEL_ORDER))
    y_quality = np.arange(len(MODEL_ORDER))
    for cutoff, offset, marker, color in [(60, -0.14, "o", "#276FBF"), (300, 0.14, "s", "#D95F02")]:
        frame = frames[cutoff]
        data = pd.read_csv(SOURCE / f"all_{cutoff}s_cleaned_results_long.csv")
        feasible_col = "FeasibleReported" if cutoff == 60 else "FeasibleStrict300"
        matrix = data.pivot(index=["Dataset", "InstanceKey"], columns="Model", values=feasible_col)
        matrix = matrix.reindex(columns=MODEL_ORDER)
        if matrix.shape != (784, len(MODEL_ORDER)) or matrix.isna().any().any():
            raise ValueError(f"Incomplete feasibility matrix at {cutoff} s: {matrix.shape}")
        feasible_matrix = np.column_stack([as_bool(matrix[model]).to_numpy() for model in MODEL_ORDER])
        feasibility = feasible_matrix.sum(axis=0)
        box_stats = [bootstrap_count_box(feasible_matrix[:, index]) for index in range(len(MODEL_ORDER))]
        axes[0].bxp(
            box_stats,
            positions=y_feas + offset,
            widths=0.21,
            orientation="horizontal",
            showfliers=False,
            patch_artist=True,
            manage_ticks=False,
            boxprops={"facecolor": color, "edgecolor": color, "alpha": 0.20, "linewidth": 0.9},
            whiskerprops={"color": color, "linewidth": 0.9},
            capprops={"color": color, "linewidth": 0.9},
            medianprops={"color": color, "linewidth": 1.0},
        )
        axes[0].scatter(feasibility, y_feas + offset, marker=marker, color=color, s=18, zorder=3, label=f"{cutoff} s")
        full_row = main_table_summary(cutoff)[-1]
        objective_wins = np.array([int(full_row[model]["Wins"]) for model in MODEL_ORDER])
        axes[1].scatter(
            objective_wins,
            y_quality + offset,
            marker=marker,
            color=color,
            s=22,
            label=f"{cutoff} s",
        )
    axes[0].set_yticks(y_feas, [SHORT[name] for name in MODEL_ORDER])
    axes[1].set_yticks(y_quality, [SHORT[name] for name in MODEL_ORDER])
    for ax in axes:
        ax.invert_yaxis()
        ax.grid(axis="x", color="#dddddd", linewidth=0.6)
    axes[0].set_xlim(480, 790)
    axes[1].set_xlim(80, 510)
    axes[0].set_xlabel("Verified-feasibility count (out of 784)")
    axes[1].set_xlabel("Tie-inclusive objective wins (out of 784)")
    axes[0].set_title("Absolute verified-feasibility count")
    axes[1].set_title("Absolute objective-win count")
    axes[0].legend(frameon=False, fontsize=8)
    axes[0].text(0.02, 0.03, "a", transform=axes[0].transAxes, fontweight="bold")
    axes[1].text(0.02, 0.03, "b", transform=axes[1].transAxes, fontweight="bold")
    fig.tight_layout()
    save_publication_figure(fig, "main_pairwise_effects")
    plt.close(fig)


def load_ablation() -> dict[str, pd.DataFrame]:
    out = {}
    for path in sorted((SOURCE / "ablation").glob("*.csv")):
        match = re.fullmatch(r"(A\d|Y\d\d)", path.stem)
        if match:
            data = pd.read_csv(path)
            data["Feasible"] = as_bool(data.HasVerifiedSolution)
            out[match.group(1)] = data
    return out


def canonical_factorial_cells(cells: dict[str, pd.DataFrame]) -> dict[str, pd.DataFrame]:
    out = dict(cells)
    for target, source in FACTORIAL_ENDPOINT_SOURCE.items():
        out[target] = cells[source].copy()
    return out


def variant_summary(cells: dict[str, pd.DataFrame], ids: list[str], reference: str) -> pd.DataFrame:
    pooled = []
    for ident in ids:
        d = cells[ident].copy()
        d["ID"] = ident
        pooled.append(d)
    pool = pd.concat(pooled, ignore_index=True)
    pool["QualityStrict"] = pool.Feasible & (pd.to_numeric(pool.PaperTableTimeSeconds, errors="coerce") <= 60.0)
    best_by_instance = pool.loc[pool.QualityStrict].groupby("Instance").BestCost.min()
    reference_frame = cells[reference].set_index("Instance")
    rows = []
    for ident in ids:
        data = cells[ident]
        feasible = data.Feasible.to_numpy()
        first = pd.to_numeric(data.FirstFeasibleTimeSeconds, errors="coerce")
        cost = pd.to_numeric(data.BestCost, errors="coerce")
        time = pd.to_numeric(data.PaperTableTimeSeconds, errors="coerce")
        global_winner = data.apply(
            lambda r: bool(r.Feasible)
            and float(r.PaperTableTimeSeconds) <= 60.0
            and r.Instance in best_by_instance.index
            and float(r.BestCost) == float(best_by_instance[r.Instance]),
            axis=1,
        )
        win_time = float(time[global_winner].mean()) if global_winner.any() else math.nan
        if ident == reference:
            wins = losses = ties = None
            median_relative = math.nan
        else:
            joined = data.set_index("Instance").join(reference_frame, lsuffix="_var", rsuffix="_ref")
            common = (
                joined.Feasible_var
                & joined.Feasible_ref
                & (pd.to_numeric(joined.PaperTableTimeSeconds_var, errors="coerce") <= 60.0)
                & (pd.to_numeric(joined.PaperTableTimeSeconds_ref, errors="coerce") <= 60.0)
            )
            vc = pd.to_numeric(joined.loc[common, "BestCost_var"], errors="coerce")
            rc = pd.to_numeric(joined.loc[common, "BestCost_ref"], errors="coerce")
            valid = vc.notna() & rc.notna()
            vc = vc[valid].to_numpy(dtype=float)
            rc = rc[valid].to_numpy(dtype=float)
            wins = int(np.sum(vc < rc))
            losses = int(np.sum(vc > rc))
            ties = int(np.sum(vc == rc))
            relative = 100 * (vc - rc) / np.maximum(1, np.abs(rc))
            median_relative = float(np.median(relative)) if len(relative) else math.nan
        solver_error = int(np.sum(pd.to_numeric(data.SolverExitCode, errors="coerce").fillna(0) != 0))
        verifier_no_output = int(np.sum(~data.Feasible & (pd.to_numeric(data.VerifierExitCode, errors="coerce").fillna(0) != 0)))
        rows.append(
            {
                "ID": ident,
                "Feasible": int(np.sum(feasible)),
                "RMST": rmst_from_first_time(first, pd.Series(feasible), 60.0),
                "MedianFirst": float(first[feasible].median()) if np.any(feasible) else math.nan,
                "GlobalWins": int(global_winner.sum()),
                "MeanWinningTime": win_time,
                "Wins": wins,
                "Losses": losses,
                "Ties": ties,
                "MedianRelativeCostPct": median_relative,
                "SolverErrors": solver_error,
                "NoVerifiedOutput": verifier_no_output,
            }
        )
    return pd.DataFrame(rows)


def write_ablation_tables(leave: pd.DataFrame, factorial: pd.DataFrame) -> None:
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        r"  \caption{Completed 60-s component ablations on 160 23-WPMS instances. Feasibility RMST treats a run without a verified assignment as right-censored at 60~s. Global wins and their mean time use the common eight-variant winner pool. $W/L/T$ and median relative cost compare each variant with A7 on common feasible instances.}",
        r"  \label{tab:ablation-60s}",
        r"  \scriptsize",
        r"  \begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}llrrrr@{}}",
        r"    \toprule",
        r"    \multicolumn{6}{@{}l}{\textbf{(a) Feasibility and first-feasible time}} \\",
        r"    \midrule",
        r"    ID & Variant & Feasible & RMST (s) & Median first (s) & No verified output \\",
        r"    \midrule",
    ]
    for row in leave.itertuples(index=False):
        lines.append(
            f"    {row.ID} & {latex_escape(ABLATION_NAMES[row.ID])} & {row.Feasible} & {row.RMST:.2f} & {row.MedianFirst:.2f} & {row.NoVerifiedOutput} \\\\"
        )
    lines.extend(
        [
            r"    \bottomrule",
            r"  \end{tabular*}",
            r"  \vspace{0.65em}",
            r"  \begin{tabular*}{\textwidth}{@{\extracolsep{\fill}}lrrrr@{}}",
            r"    \toprule",
            r"    \multicolumn{5}{@{}l}{\textbf{(b) Objective quality}} \\",
            r"    \midrule",
            r"    ID & Global wins & Mean winning time (s) & $W/L/T$ vs A7 & Median $\Delta$cost (\%) \\",
            r"    \midrule",
        ]
    )
    for row in leave.itertuples(index=False):
        wlt = "--" if row.ID == "A7" else f"{int(row.Wins)}/{int(row.Losses)}/{int(row.Ties)}"
        delta = "--" if row.ID == "A7" else f"{row.MedianRelativeCostPct:.2f}"
        lines.append(
            f"    {row.ID} & {row.GlobalWins} & {row.MeanWinningTime:.2f} & {wlt} & {delta} \\\\"
        )
    lines.extend([r"    \bottomrule", r"  \end{tabular*}", r"\end{table}", ""])
    (TABLES / "ablation_60s.tex").write_text("\n".join(lines), encoding="utf-8")

    count = factorial.set_index("ID").Feasible
    rmst = factorial.set_index("ID").RMST
    count_interaction = int(count.Y11 - count.Y10 - count.Y01 + count.Y00)
    rmst_interaction = float(rmst.Y11 - rmst.Y10 - rmst.Y01 + rmst.Y00)
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        r"  \caption{Completed $2\times2$ factorial ablation. $P$ activates projection, state transfer and retained arbitration. $W$ activates the SCOPE weighting path. The endpoint cells $Y_{00}$ and $Y_{11}$ reuse the canonical A0 and A7 records from \cref{tab:ablation-60s}. The two off-diagonal cells complete the factorial contrast. Cost contrasts use $Y_{11}$ as the reference.}",
        r"  \label{tab:ablation-factorial-60s}",
        r"  \scriptsize",
        r"  \begin{tabular*}{\linewidth}{@{\extracolsep{\fill}}lccrrrr@{}}",
        r"    \toprule",
        r"    Cell & $P$ & $W$ & Feasible & RMST (s) & $W/L/T$ vs $Y_{11}$ & Median $\Delta$cost (\%) \\",
        r"    \midrule",
    ]
    for row in factorial.itertuples(index=False):
        p, w = int(row.ID[1]), int(row.ID[2])
        wlt = "--" if row.ID == "Y11" else f"{int(row.Wins)}/{int(row.Losses)}/{int(row.Ties)}"
        delta = "--" if row.ID == "Y11" else f"{row.MedianRelativeCostPct:.2f}"
        lines.append(f"    $Y_{{{p}{w}}}$ & {p} & {w} & {row.Feasible} & {row.RMST:.2f} & {wlt} & {delta} \\\\")
    lines.extend(
        [
            r"    \bottomrule",
            r"  \end{tabular*}",
            r"  \vspace{0.35em}",
            rf"  \raggedright\footnotesize The observed feasibility-count interaction is ${int(count.Y11)}-{int(count.Y10)}-{int(count.Y01)}+{int(count.Y00)}={count_interaction}$. The RMST interaction is ${rmst_interaction:.2f}\,\mathrm{{s}}$.",
            r"\end{table}",
            "",
        ]
    )
    (TABLES / "ablation_factorial_60s.tex").write_text("\n".join(lines), encoding="utf-8")


def plot_ablation(cells: dict[str, pd.DataFrame], leave: pd.DataFrame) -> None:
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.15))
    palette = plt.get_cmap("tab10")
    for idx, ident in enumerate([f"A{i}" for i in range(8)]):
        data = cells[ident]
        times = pd.to_numeric(
            data.loc[data.Feasible, "FirstFeasibleTimeSeconds"], errors="coerce"
        ).to_numpy(dtype=float)
        times = np.sort(times[np.isfinite(times)])
        x = np.r_[0, times, 60]
        y = np.r_[0, np.arange(1, len(times) + 1), len(times)]
        axes[0].step(x, y, where="post", label=ident, color=palette(idx), linewidth=1.2)
    axes[0].set_xlim(0, 60)
    axes[0].set_ylim(0, 160)
    axes[0].set_xlabel("Time to first verified feasible assignment (s)")
    axes[0].set_ylabel("Cumulative feasible instances")
    axes[0].grid(axis="y", color="#dddddd", linewidth=0.6)
    axes[0].legend(ncol=2, frameon=False, fontsize=7)
    axes[0].set_title("Exact first-feasible trajectories")

    x = np.arange(len(leave))
    axes[1].bar(x - 0.18, leave.Feasible, width=0.36, color="#276FBF", label="Feasible count")
    rmst_scaled = 160 * (1 - leave.RMST / 60)
    axes[1].bar(x + 0.18, rmst_scaled, width=0.36, color="#D95F02", label="RMST score")
    axes[1].set_xticks(x, leave.ID)
    axes[1].set_ylim(0, 160)
    axes[1].set_ylabel(r"Count or $160(1-\mathrm{RMST}/60)$")
    axes[1].set_title("Coverage and time-to-feasibility")
    axes[1].grid(axis="y", color="#dddddd", linewidth=0.6)
    axes[1].legend(frameon=False, fontsize=7)
    axes[0].text(0.02, 0.95, "a", transform=axes[0].transAxes, fontweight="bold", va="top")
    axes[1].text(0.02, 0.95, "b", transform=axes[1].transAxes, fontweight="bold", va="top")
    fig.tight_layout()
    save_publication_figure(fig, "ablation_diagnostics")
    plt.close(fig)


def cross_kernel_data() -> pd.DataFrame:
    """Assemble the crossed controller-by-kernel experiment from frozen records."""
    raw = SOURCE / "cross_kernel"
    specifications = [
        (
            "Host-NuWLS",
            "NuWLS",
            False,
            raw / "host_nuwls.csv",
        ),
        (
            "SCOPE-MaxSAT",
            "NuWLS",
            True,
            raw / "scope_nuwls.csv",
        ),
        (
            "Host-CCEHC",
            "CCEHC",
            False,
            raw / "host_ccehc.csv",
        ),
        (
            "SCOPE-CCEHC",
            "CCEHC",
            True,
            raw / "scope_ccehc.csv",
        ),
    ]
    required = [
        "Instance",
        "Seed",
        "CutoffSeconds",
        "HasVerifiedSolution",
        "FirstFeasibleTimeSeconds",
        "PaperTableTimeSeconds",
        "BestCost",
    ]
    frames = []
    reference_instances: set[str] | None = None
    for model, kernel, controlled, path in specifications:
        frame = pd.read_csv(path)
        missing = set(required).difference(frame.columns)
        if missing:
            raise RuntimeError(f"Missing cross-kernel columns in {path}: {sorted(missing)}")
        if len(frame) != 160 or frame.Instance.nunique() != 160:
            raise RuntimeError(f"Expected 160 unique instances in {path}")
        if frame.Instance.duplicated().any():
            raise RuntimeError(f"Duplicate instances in {path}")
        if set(pd.to_numeric(frame.Seed, errors="raise")) != {1}:
            raise RuntimeError(f"Unexpected seed in {path}")
        if set(pd.to_numeric(frame.CutoffSeconds, errors="raise")) != {60}:
            raise RuntimeError(f"Unexpected cutoff in {path}")
        instances = set(frame.Instance)
        if reference_instances is None:
            reference_instances = instances
        elif instances != reference_instances:
            raise RuntimeError(f"Cross-kernel instance set differs in {path}")
        frame = frame[list(required)].copy()
        frame["ModelName"] = model
        frame["KernelName"] = kernel
        frame["ControllerEnabled"] = controlled
        frames.append(frame)

    data = pd.concat(frames, ignore_index=True)
    if data.duplicated(["ModelName", "Instance"]).any():
        raise RuntimeError("Duplicate model-instance rows in cross-kernel experiment")
    data["Feasible"] = as_bool(data.HasVerifiedSolution)
    data.to_csv(SOURCE / "python_cross_kernel_runs.csv", index=False)
    return data


def cross_kernel_summary(data: pd.DataFrame) -> pd.DataFrame:
    rows = []
    for kernel in CROSS_KERNEL_ORDER:
        kernel_data = data[data.KernelName.eq(kernel)]
        host = kernel_data[~kernel_data.ControllerEnabled].set_index("Instance")
        scope = kernel_data[kernel_data.ControllerEnabled].set_index("Instance")
        joined = host.join(scope, lsuffix="_host", rsuffix="_scope", validate="one_to_one")
        host_feasible = joined.Feasible_host
        scope_feasible = joined.Feasible_scope
        both = host_feasible & scope_feasible
        scope_only = ~host_feasible & scope_feasible
        host_only = host_feasible & ~scope_feasible
        neither = ~host_feasible & ~scope_feasible
        rows.append(
            {
                "Kernel": kernel,
                "HostFeasible": int(host_feasible.sum()),
                "ScopeFeasible": int(scope_feasible.sum()),
                "DeltaFeasible": int(scope_feasible.sum() - host_feasible.sum()),
                "BothFeasible": int(both.sum()),
                "ScopeOnly": int(scope_only.sum()),
                "HostOnly": int(host_only.sum()),
                "Neither": int(neither.sum()),
            }
        )
    return pd.DataFrame(rows)


def write_cross_kernel_table(summary: pd.DataFrame) -> None:
    indexed = summary.set_index("Kernel")
    cells = [
        ("Host-NuWLS", "NuWLS", "Off", int(indexed.loc["NuWLS", "HostFeasible"])),
        ("SCOPE-MaxSAT", "NuWLS", "On", int(indexed.loc["NuWLS", "ScopeFeasible"])),
        ("Host-CCEHC", "CCEHC", "Off", int(indexed.loc["CCEHC", "HostFeasible"])),
        ("SCOPE-CCEHC", "CCEHC", "On", int(indexed.loc["CCEHC", "ScopeFeasible"])),
    ]
    lines = [
        r"\begin{table}[H]",
        r"  \centering",
        r"  \caption{Crossed controller-by-kernel experiment on the same 160 instances under the 60-s cutoff. Host-NuWLS is the independently rerun instrumented upstream configuration corresponding to A0; the SCOPE-MaxSAT cell uses the frozen A7 record. Panel a writes out all four cells of the $2\times2$ design. Panel b gives the paired host-to-SCOPE coverage contrast within each kernel. SCOPE-only and host-only are discordant paired counts; their difference equals $\Delta N_{\mathrm{feas}}$.}",
        r"  \label{tab:cross-kernel}",
        r"  \scriptsize",
        r"  \begin{tabular*}{\linewidth}{@{\extracolsep{\fill}}llcrr@{}}",
        r"    \toprule",
        r"    \multicolumn{5}{@{}l}{\textbf{(a) Four experimental cells}} \\",
        r"    \midrule",
        r"    Configuration & Kernel & SCOPE controller & Verified output & No verified output \\",
        r"    \midrule",
    ]
    for configuration, kernel, controller, feasible in cells:
        lines.append(
            f"    {latex_escape(configuration)} & {latex_escape(kernel)} & {controller} & {feasible} & {160 - feasible} \\\\"
        )
    lines.extend(
        [
            r"    \bottomrule",
            r"  \end{tabular*}",
            r"  \vspace{0.65em}",
            r"  \begin{tabular*}{\linewidth}{@{\extracolsep{\fill}}lrrrr@{}}",
            r"    \toprule",
            r"    \multicolumn{5}{@{}l}{\textbf{(b) Within-kernel paired coverage effect}} \\",
            r"    \midrule",
            r"    Kernel & $\Delta N_{\mathrm{feas}}$ & SCOPE-only & Host-only & Both \\",
            r"    \midrule",
        ]
    )
    for row in summary.itertuples(index=False):
        lines.append(
            f"    {latex_escape(row.Kernel)} & {row.DeltaFeasible:+d} & {row.ScopeOnly} & {row.HostOnly} & {row.BothFeasible} \\\\"
        )
    lines.extend([r"    \bottomrule", r"  \end{tabular*}", r"\end{table}", ""])
    (TABLES / "cross_kernel.tex").write_text("\n".join(lines), encoding="utf-8")


def plot_cross_kernel(data: pd.DataFrame, summary: pd.DataFrame) -> None:
    del data  # The plotted counts are the audited paired summary.
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 2.75), gridspec_kw={"width_ratios": [1.05, 1.0]})
    host_color = "#B7BEC8"
    scope_color = "#2F6FAE"
    loss_color = "#D07A5F"

    x = np.arange(len(summary))
    width = 0.34
    host_bars = axes[0].bar(
        x - width / 2,
        summary.HostFeasible,
        width,
        color=host_color,
        label="Host only",
    )
    scope_bars = axes[0].bar(
        x + width / 2,
        summary.ScopeFeasible,
        width,
        color=scope_color,
        label="SCOPE enabled",
    )
    axes[0].set_xticks(x, summary.Kernel)
    axes[0].set_ylim(0, 160)
    axes[0].set_ylabel("Instances with verified feasible output")
    axes[0].set_title("Absolute coverage")
    axes[0].grid(axis="y", color="#E5E7EB", linewidth=0.6, zorder=0)
    axes[0].legend(loc="upper left", frameon=False, ncol=2, fontsize=7)
    for bars in (host_bars, scope_bars):
        axes[0].bar_label(bars, padding=2, fontsize=8)

    y = np.arange(len(summary))
    axes[1].barh(y, -summary.HostOnly, color=loss_color, height=0.46)
    axes[1].barh(y, summary.ScopeOnly, color=scope_color, height=0.46)
    axes[1].axvline(0, color="#4B5563", linewidth=0.8)
    axes[1].set_yticks(y, summary.Kernel)
    axes[1].invert_yaxis()
    axes[1].set_xlim(-22, 94)
    axes[1].set_xlabel(r"Host-only instances  $\leftarrow$  0  $\rightarrow$  SCOPE-only instances")
    axes[1].set_title("Instance-level coverage transitions")
    axes[1].grid(axis="x", color="#E5E7EB", linewidth=0.6, zorder=0)
    for i, row in enumerate(summary.itertuples(index=False)):
        if row.HostOnly:
            axes[1].text(-row.HostOnly + 1.2, i, str(row.HostOnly), ha="left", va="center", fontsize=8, color="white")
        else:
            axes[1].text(-1.2, i, "0", ha="right", va="center", fontsize=8)
        axes[1].text(row.ScopeOnly - 1.2, i, str(row.ScopeOnly), ha="right", va="center", fontsize=8, color="white")
        axes[1].text(91, i - 0.31, rf"$\Delta={row.DeltaFeasible:+d}$", ha="right", va="center", fontsize=8)

    axes[0].text(-0.10, 1.04, "a", transform=axes[0].transAxes, fontweight="bold", va="top")
    axes[1].text(-0.10, 1.04, "b", transform=axes[1].transAxes, fontweight="bold", va="top")
    fig.tight_layout()
    save_publication_figure(fig, "cross_kernel_transfer")
    plt.close(fig)


def plot_parameter_sensitivity() -> None:
    data = pd.read_csv(SOURCE / "sensitivity" / "parameter_summary.csv")
    variants = data[data.ConfigID != "BASE"].copy()
    variants["Label"] = variants.ConfigID
    variants = variants.sort_values(["ParameterGroup", "ParameterValue"], kind="stable")
    y = np.arange(len(variants))
    fig, axes = plt.subplots(1, 2, figsize=(7.2, 6.0), sharey=True)
    axes[0].barh(y, variants.DeltaFeasibleVsBase, color=np.where(variants.DeltaFeasibleVsBase < 0, "#D95F02", "#276FBF"))
    net = variants.Strict60QualityWins - variants.Strict60QualityLosses
    significant = variants.Strict60CostSignTestHolmP < 0.05
    axes[1].scatter(net, y, facecolors=np.where(significant, "#276FBF", "white"), edgecolors="#276FBF", s=25)
    axes[0].axvline(0, color="#555555", linewidth=0.8)
    axes[1].axvline(0, color="#555555", linewidth=0.8)
    axes[0].set_yticks(y, variants.Label)
    axes[0].invert_yaxis()
    axes[0].set_xlabel("Change in verified-feasibility count")
    axes[1].set_xlabel("Strict-60-s net quality wins")
    axes[0].set_title("Coverage relative to BASE")
    axes[1].set_title("Conditional quality relative to BASE")
    for ax in axes:
        ax.grid(axis="x", color="#dddddd", linewidth=0.6)
    axes[0].text(0.02, 0.02, "a", transform=axes[0].transAxes, fontweight="bold")
    axes[1].text(0.02, 0.02, "b", transform=axes[1].transAxes, fontweight="bold")
    fig.tight_layout()
    save_publication_figure(fig, "parameter_sensitivity")
    plt.close(fig)


def write_parameter_sensitivity_table() -> None:
    data = pd.read_csv(SOURCE / "sensitivity" / "parameter_summary.csv").set_index("ConfigID")
    selected = ["BASE", "P05", "TP20", "TP0", "GAMMA0", "K1000", "RL0005", "TH1", "TQ20", "TQ40"]
    variables = {
        "BASE": "all",
        "P05": "$p$",
        "TP20": r"$\tau_{\mathrm P}$",
        "TP0": r"$\tau_{\mathrm P}$",
        "GAMMA0": r"$\gamma$",
        "K1000": r"$\kappa$",
        "RL0005": r"$r_{\mathrm L}$",
        "TH1": r"$\tau_{\mathrm H}$",
        "TQ20": r"$\tau_{\mathrm Q}$",
        "TQ40": r"$\tau_{\mathrm Q}$",
    }
    verdicts = {
        "BASE": "reference",
        "P05": "quality candidate",
        "TP20": "quality candidate",
        "TP0": "quality regression",
        "GAMMA0": "feasibility loss",
        "K1000": "coverage preserved",
        "RL0005": "coverage preserved",
        "TH1": "coverage preserved",
        "TQ20": "coverage preserved",
        "TQ40": "coverage preserved",
    }
    lines = [
        r"\begin{table}[H]",
        r"\centering\scriptsize",
        r"\caption{Selected parameter-sensitivity results on the 160-instance 23-WPMS benchmark under the 60-s cutoff. The table reports BASE and nine representative one-factor variants, while the complete 29-variant screen is shown in \cref{fig:parameter-sensitivity}. The primary criterion is verified feasibility. Objective comparisons are conditional on both configurations having a verified solution and a reported best-event time no later than 60~s. $W/L/T$ denotes quality wins, losses, and ties against BASE under the strict 60-s cutoff. Adjusted $p$ values use a two-sided exact sign test with Holm correction across the 29 non-baseline configurations.}",
        r"\label{tab:parameter-sensitivity-main}",
        r"\resizebox{\linewidth}{!}{%",
        r"\begin{tabular}{llrrrrrrrr}",
        r"\toprule",
        r"ID & Var. & Value & $N_{\rm feas}$ & $\Delta N_{\rm feas}$ & $W$ & $L$ & $T$ & adj. $p$ & Verdict\\",
        r"\midrule",
    ]
    for ident in selected:
        row = data.loc[ident]
        value = "frozen" if ident == "BASE" else str(row.ParameterValue)
        if value.endswith(".0"):
            value = value[:-2]
        adjusted = float(row.Strict60CostSignTestHolmP)
        adjusted_text = r"$<0.0001$" if adjusted < 0.00005 else f"{adjusted:.4f}"
        delta = int(row.DeltaFeasibleVsBase)
        lines.append(
            f"{ident} & {variables[ident]} & {value} & {int(row.Feasible)} & {delta:+d} & "
            f"{int(row.Strict60QualityWins)} & {int(row.Strict60QualityLosses)} & "
            f"{int(row.Strict60QualityTies)} & {adjusted_text} & {verdicts[ident]}\\\\"
        )
    lines.extend([r"\bottomrule", r"\end{tabular}", r"}", r"\end{table}", ""])
    (TABLES / "parameter_sensitivity_main_table.tex").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    TABLES.mkdir(exist_ok=True)
    FIGURES.mkdir(exist_ok=True)
    for cutoff in [60, 300]:
        write_main_comparison_tables(cutoff)
    pairwise = {cutoff: main_pairwise(cutoff) for cutoff in [60, 300]}
    for cutoff, frame in pairwise.items():
        frame.to_csv(SOURCE / f"python_pairwise_{cutoff}s.csv", index=False)
        write_pairwise_table(frame, cutoff)
    plot_main_availability()
    plot_pairwise_summary(pairwise)

    cells = load_ablation()
    leave = variant_summary(cells, [f"A{i}" for i in range(8)], "A7")
    factorial_cells = canonical_factorial_cells(cells)
    factorial = variant_summary(factorial_cells, ["Y00", "Y01", "Y10", "Y11"], "Y11")
    leave.to_csv(SOURCE / "python_ablation_summary.csv", index=False)
    factorial.to_csv(SOURCE / "python_factorial_summary.csv", index=False)
    write_ablation_tables(leave, factorial)
    plot_ablation(cells, leave)

    cross = cross_kernel_data()
    cross_summary = cross_kernel_summary(cross)
    cross_summary.to_csv(SOURCE / "python_cross_kernel_summary.csv", index=False)
    write_cross_kernel_table(cross_summary)
    plot_cross_kernel(cross, cross_summary)
    plot_parameter_sensitivity()
    write_parameter_sensitivity_table()


if __name__ == "__main__":
    main()
