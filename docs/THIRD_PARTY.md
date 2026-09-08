# Third-party notices

The artifact incorporates or adapts the following projects.

## NuWLS

NuWLS supplies the host local-search kernel used by the primary implementation
and several experiment cells.  The official upstream repository is
`https://github.com/shaowei-cai-group/NuWLS`.  An audit performed on 2026-09-08
found no `LICENSE`, `COPYING`, or equivalent grant in its recursive file tree,
and the GitHub repository API reported no detected license.  Public source
redistribution must therefore wait for written authorization or an upstream
license clarification.  See `NUWLS_LICENSE_AUDIT.md` for the evidence and
`NUWLS_PERMISSION_REQUEST.md` for a ready-to-send request.

## CaDiCaL 1.9.5

CaDiCaL is bundled for complete hard-clause projection.  Its MIT license is
retained in each bundled source tree, for example:

`solver/single_core/third_party/cadical/LICENSE`

A convenience copy is provided at `LICENSES/MIT-CaDiCaL.txt`.

## CCEHC

CCEHC is used only in the crossed-kernel experiment.  The upstream GPLv3 notice
is retained at:

`experiments/cross_kernel/host_ccehc/third_party/CCEHC/LICENSE.txt`

and in the corresponding controlled-kernel source tree.  The CCEHC directories
must be distributed under their applicable GPL terms and with modification
notices preserved.

A convenience copy is provided at `LICENSES/GPL-3.0-only.txt`.

No repository-wide license is declared because the included components have
different terms and the NuWLS permission remains unresolved.
