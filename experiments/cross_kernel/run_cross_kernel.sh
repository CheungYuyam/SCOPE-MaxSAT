#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 BENCHMARK_DIRECTORY CUTOFF_SECONDS OUTPUT_DIRECTORY [SEED]" >&2
  exit 2
fi

benchmark_dir=$1
cutoff=$2
output_dir=$3
seed=${4:-1}
repo_root=$(cd "$(dirname "$0")/../.." && pwd)
runner="$repo_root/experiments/run_benchmark.py"
verifier="$repo_root/tools/verifier/wcnf-verify"

mkdir -p "$output_dir"
python "$runner" --solver "$repo_root/experiments/cross_kernel/host_nuwls/official-nuwls" --benchmark-dir "$benchmark_dir" --seed "$seed" --cutoff "$cutoff" --verifier "$verifier" --output "$output_dir/host_nuwls.csv"
python "$runner" --solver "$repo_root/experiments/cross_kernel/scope_nuwls/nuwls" --benchmark-dir "$benchmark_dir" --seed "$seed" --cutoff "$cutoff" --verifier "$verifier" --output "$output_dir/scope_nuwls.csv"
python "$runner" --solver "$repo_root/experiments/cross_kernel/host_ccehc/ccehc-host" --benchmark-dir "$benchmark_dir" --seed "$seed" --cutoff "$cutoff" --verifier "$verifier" --output "$output_dir/host_ccehc.csv"
python "$runner" --solver "$repo_root/experiments/cross_kernel/scope_ccehc/scope-ccehc" --benchmark-dir "$benchmark_dir" --seed "$seed" --cutoff "$cutoff" --verifier "$verifier" --output "$output_dir/scope_ccehc.csv"
