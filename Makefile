.PHONY: verify verify-host verify-linux-x86_64 verify-scripts verify-docs verify-release-candidate verify-boards verify-aarch64-cross aarch64-verification-sysroot

verify:
	./scripts/verification/verify_repository.sh

verify-host:
	./scripts/verification/verify_host_cpp.sh all

verify-linux-x86_64:
	./scripts/verification/verify_linux_x86_64.sh

verify-scripts:
	./scripts/verification/verify_shell_scripts.sh

verify-docs:
	./scripts/verification/verify_documentation.sh
	python3 ./scripts/verification/verify_board_profiles.py
	./scripts/verification/verify_buildroot_external_tree.sh
	./scripts/verification/verify_presence_sensor_sources.sh
	./scripts/verification/verify_frdm_device_tree_overlay.sh

verify-boards:
	python3 ./scripts/verification/verify_board_profiles.py
	./scripts/verification/verify_suspend_kernel_config_checker.sh
	./scripts/verification/verify_frdm_device_tree_overlay.sh

verify-aarch64-cross:
	./scripts/verification/verify_aarch64_cross_build.sh

aarch64-verification-sysroot:
	./scripts/environment/build_aarch64_verification_sysroot.sh

verify-release-candidate:
	./scripts/verification/verify_release_candidate_image.sh

.PHONY: sim-panel verify-panel-goldens update-panel-goldens verify-panel-simulator verify-panel-media verify-panel-vm

sim-panel:
	./scripts/sim/build_panel.sh native --execute
	python3 scripts/sim/run_native_panel.py --execute

verify-panel-goldens:
	./scripts/sim/build_panel.sh headless --execute
	python3 scripts/verification/verify_panel_goldens.py --renderer .build/panel-headless/simulator/bhp-panel-render

update-panel-goldens:
	./scripts/sim/build_panel.sh headless --execute
	python3 scripts/verification/verify_panel_goldens.py --renderer .build/panel-headless/simulator/bhp-panel-render --update

verify-panel-simulator:
	./scripts/sim/build_panel.sh native --execute
	python3 scripts/verification/verify_panel_simulator.py --simulator .build/panel-native/simulator/bhp-panel-sim

verify-panel-media:
	python3 -m unittest discover -s media_broker/tests -p 'test_*.py'

verify-panel-vm:
	python3 scripts/verification/verify_panel_vm.py
