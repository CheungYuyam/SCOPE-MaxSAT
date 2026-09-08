#!/usr/bin/env python3
"""Run one solver over a WCNF directory or manifest and verify each output."""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
import time
from pathlib import Path


def inputs(directory: Path | None, manifest: Path | None) -> list[Path]:
    if directory is not None:
        return sorted(path.resolve() for path in directory.rglob("*.wcnf") if path.is_file())
    assert manifest is not None
    base = manifest.resolve().parent
    result = []
    for raw in manifest.read_text(encoding="utf-8").splitlines():
        item = raw.strip()
        if not item or item.startswith("#"):
            continue
        path = Path(item)
        result.append((path if path.is_absolute() else base / path).resolve())
    return result


def verifier_value(text: str, name: str) -> str:
    match = re.search(rf"^c {re.escape(name)}\s+(.+)$", text, flags=re.MULTILINE)
    return match.group(1).strip() if match else ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--solver", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--benchmark-dir", type=Path)
    source.add_argument("--manifest", type=Path)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--cutoff", type=float, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--verifier", type=Path)
    parser.add_argument("--extra-arg", action="append", default=[])
    args = parser.parse_args()

    solver = args.solver.resolve()
    verifier = args.verifier.resolve() if args.verifier else None
    instances = inputs(args.benchmark_dir, args.manifest)
    if not instances:
        parser.error("no WCNF instances were found")
    missing = [str(path) for path in instances if not path.is_file()]
    if missing:
        parser.error(f"missing instance: {missing[0]}")

    output = args.output.resolve()
    log_dir = output.parent / f"{output.stem}_logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    output.parent.mkdir(parents=True, exist_ok=True)
    fields = ["Index", "Instance", "Seed", "CutoffSeconds", "ExternalSeconds", "SolverExitCode", "VerifierExitCode", "HasVerifiedSolution", "BestCost", "Log"]

    with output.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for index, instance in enumerate(instances, start=1):
            log_path = log_dir / f"{index:04d}.log"
            command = [str(solver), str(instance), str(args.seed), str(args.cutoff), *args.extra_arg]
            started = time.perf_counter()
            completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            elapsed = time.perf_counter() - started
            log_path.write_text(completed.stdout, encoding="utf-8")

            verify_code = ""
            verified = False
            cost = ""
            if verifier is not None:
                checked = subprocess.run([str(verifier), str(instance), str(log_path)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
                verify_code = checked.returncode
                verified = verifier_value(checked.stdout, "external_verified") == "1"
                cost = verifier_value(checked.stdout, "external_cost")

            writer.writerow({
                "Index": index,
                "Instance": str(instance),
                "Seed": args.seed,
                "CutoffSeconds": args.cutoff,
                "ExternalSeconds": f"{elapsed:.6f}",
                "SolverExitCode": completed.returncode,
                "VerifierExitCode": verify_code,
                "HasVerifiedSolution": verified,
                "BestCost": cost,
                "Log": str(log_path.relative_to(output.parent)),
            })
            handle.flush()
            print(f"[{index}/{len(instances)}] {instance.name}: solver={completed.returncode}, verified={verified}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
