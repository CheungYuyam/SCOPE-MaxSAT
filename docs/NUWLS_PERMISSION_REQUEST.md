# NuWLS redistribution permission request

The following message is ready to send to the NuWLS authors or official
repository maintainers.  Preserve their written response with the release
records and update `THIRD_PARTY.md` before making the repository public.

## Subject

Permission request to redistribute modified NuWLS source in the SCOPE-MaxSAT artifact

## Message

Dear Professors Chu, Cai, and Luo,

We are preparing the research artifact for our paper, “SCOPE-MaxSAT:
Feasibility-Preserving Sequential Optimization for Weighted Partial MaxSAT.”
The implementation uses and modifies the NuWLS source code associated with
your AAAI 2023 paper, “NuWLS: Improving Local Search for (Weighted) Partial
MaxSAT by New Weighting Techniques.”

We would like to publish the artifact in a public GitHub repository.  It would
include modified NuWLS-derived C++ source files used by the main single-core
solver, ablation and sensitivity configurations, a crossed-kernel experiment,
and a separately identified two-core research extension.  The repository would
credit NuWLS and its authors, cite the original paper and upstream repository,
preserve any notices you require, and clearly distinguish our controller and
modifications from the NuWLS kernel.  It would also include our experiment
scripts, derived result data, and documentation; third-party benchmark
instances would not be redistributed.

The current NuWLS GitHub repository does not appear to include an explicit
software license.  Could you please confirm one of the following?

1. the license under which the NuWLS source and derivative modifications may be
   used, modified, and publicly redistributed; or
2. your written permission for us to redistribute the original and modified
   NuWLS-derived source files in the SCOPE-MaxSAT public research artifact.

If you prefer a particular license, copyright notice, attribution statement,
or list of files, please let us know and we will apply it exactly.  We can also
send the release candidate for inspection before publication.

Thank you for making NuWLS available and for considering this request.

Sincerely,

Yuyam Cheung, Ruqian Qin, and Zaijun Zhang

Corresponding author: Zaijun Zhang (`zzj@sgmtu.edu.cn`)
