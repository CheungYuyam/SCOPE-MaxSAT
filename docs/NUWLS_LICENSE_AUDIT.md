# NuWLS license audit

Audit date: 2026-09-08

## Upstream checked

- Official repository: `https://github.com/shaowei-cai-group/NuWLS`
- Default branch: `master`
- Repository API: `https://api.github.com/repos/shaowei-cai-group/NuWLS`
- Recursive tree API:
  `https://api.github.com/repos/shaowei-cai-group/NuWLS/git/trees/master?recursive=1`

## Finding

The official repository publishes the NuWLS source code and identifies the
AAAI 2023 paper by Yi Chu, Shaowei Cai, and Chuan Luo.  However, the recursive
tree contains no `LICENSE`, `COPYING`, or equivalent licensing file, the README
does not state redistribution terms, and the repository API reports a null
license value.

GitHub's licensing documentation explains that, without a license, default
copyright rules apply and others do not automatically receive permission to
reproduce, distribute, or create derivative works.  Public availability of a
repository is therefore not sufficient authorization for this release to
redistribute the original or modified NuWLS files.

## Impact on this artifact

NuWLS-derived files occur in the primary solver, component and sensitivity
experiments, the NuWLS cells of the crossed-kernel experiment, and the separate
two-core extension.  Some files retain substantial upstream structure while
adding SCOPE-MaxSAT functionality.  They must be treated as affected even when
their hashes differ from the upstream files.

CaDiCaL and CCEHC are not part of this unresolved finding; their MIT and GPLv3
notices, respectively, are preserved separately.

## Publication decision

The complete checkout may be retained as a private release candidate.  A
public GitHub release requires either:

1. an explicit upstream license that covers redistribution and modification;
   or
2. written authorization from the NuWLS copyright holders covering the files
   and distribution described in `NUWLS_PERMISSION_REQUEST.md`.

The repository must not claim that NuWLS is MIT-, BSD-, GPL-, or otherwise
open-source licensed unless the upstream authors provide that grant.
