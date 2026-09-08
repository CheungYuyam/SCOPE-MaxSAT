param(
    [Parameter(Mandatory=$true)][string]$BenchmarkDirectory,
    [Parameter(Mandatory=$true)][double]$CutoffSeconds,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [int]$Seed = 1,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Runner = Join-Path $RepoRoot "experiments\run_benchmark.py"
$Verifier = Join-Path $RepoRoot "tools\verifier\wcnf-verify.exe"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$Cells = @(
    @{Name="host_nuwls"; Solver="experiments\cross_kernel\host_nuwls\official-nuwls.exe"},
    @{Name="scope_nuwls"; Solver="experiments\cross_kernel\scope_nuwls\nuwls.exe"},
    @{Name="host_ccehc"; Solver="experiments\cross_kernel\host_ccehc\ccehc-host.exe"},
    @{Name="scope_ccehc"; Solver="experiments\cross_kernel\scope_ccehc\scope-ccehc.exe"}
)

foreach ($Cell in $Cells) {
    $Solver = Join-Path $RepoRoot $Cell.Solver
    $Output = Join-Path $OutputDirectory ($Cell.Name + ".csv")
    & $Python $Runner --solver $Solver --benchmark-dir $BenchmarkDirectory --seed $Seed --cutoff $CutoffSeconds --verifier $Verifier --output $Output
    if ($LASTEXITCODE -ne 0) { throw "Cross-kernel cell failed: $($Cell.Name)" }
}
