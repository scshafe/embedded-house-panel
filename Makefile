.PHONY: verify verify-host verify-linux-x86_64 verify-scripts verify-docs verify-release-candidate

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
	./scripts/verification/verify_buildroot_external_tree.sh
	./scripts/verification/verify_presence_sensor_sources.sh

verify-release-candidate:
	./scripts/verification/verify_release_candidate_image.sh
