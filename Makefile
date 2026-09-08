.PHONY: primary verifier ablation sensitivity cross-kernel two-core audit evidence clean

primary:
	$(MAKE) -C solver/single_core

verifier:
	$(MAKE) -C tools/verifier

ablation:
	$(MAKE) -C experiments/ablation

sensitivity:
	$(MAKE) -C experiments/sensitivity

cross-kernel:
	$(MAKE) -C experiments/cross_kernel/host_nuwls
	$(MAKE) -C experiments/cross_kernel/scope_nuwls
	$(MAKE) -C experiments/cross_kernel/host_ccehc
	$(MAKE) -C experiments/cross_kernel/scope_ccehc

two-core:
	$(MAKE) -C extensions/two_core_prototype

audit:
	python analysis/scripts/verify_results.py
	python extensions/two_core_prototype/audit_results.py

evidence:
	python analysis/scripts/generate_experiment_evidence.py

clean:
	$(MAKE) -C solver/single_core clean
	$(MAKE) -C tools/verifier clean
