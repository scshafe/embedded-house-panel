#!/bin/sh
# Builds the pinned Linux x86_64 verification image and runs make verify in it.
# Inputs: optional BHP_LINUX_VERIFICATION_IMAGE tag and BHP_DOCKER command name.
# Side effects: updates the local image cache and uses a disposable container tmpfs.
# Idempotent: yes; image layers are reusable and build products are disposable.

set -eu

SCRIPT_DIRECTORY=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH='' cd -- "$SCRIPT_DIRECTORY/../.." && pwd)
# shellcheck source=../structured_logging/structured_log.sh
. "$PROJECT_ROOT/scripts/structured_logging/structured_log.sh"
BHP_SCRIPT_SERVICE_NAME=beaglebone_house_panel.verify_linux_x86_64
export BHP_SCRIPT_SERVICE_NAME
TRACE_ID=$(bhp_new_trace_id)

if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then
	printf '%s\n' \
		"Usage: [BHP_DOCKER=docker] [BHP_LINUX_VERIFICATION_IMAGE=name:tag] $0"
	exit 0
fi
if [ "$#" -ne 0 ]; then
	bhp_fail VERIFY_LINUX_X86_64.ARGUMENT_INVALID \
		"No positional arguments are accepted" "$TRACE_ID"
	exit 2
fi

docker_command=${BHP_DOCKER:-docker}
image_name=${BHP_LINUX_VERIFICATION_IMAGE:-beaglebone-house-panel-verify-linux-x86-64:ubuntu-24.04}
verification_uid=$(id -u)
verification_gid=$(id -g)
command -v "$docker_command" >/dev/null 2>&1 || {
	bhp_fail VERIFY_LINUX_X86_64.CONTAINER_ENGINE_MISSING \
		"$docker_command" "$TRACE_ID"
	exit 1
}
container_engine_architecture=$(
	"$docker_command" info --format '{{.Architecture}}'
) || {
	bhp_fail VERIFY_LINUX_X86_64.CONTAINER_ENGINE_INSPECTION_FAILED \
		"$docker_command" "$TRACE_ID"
	exit 1
}
shellcheck_aarch64_fallback_allowed=false
case "$container_engine_architecture" in
	arm64 | aarch64) shellcheck_aarch64_fallback_allowed=true ;;
esac

bhp_log info linux_x86_64_image_build_started "$TRACE_ID" \
	container_engine "$docker_command" \
	container_engine_architecture "$container_engine_architecture" \
	image "$image_name" \
	platform linux/amd64
"$docker_command" build \
	--platform linux/amd64 \
	--build-arg BHP_VERIFICATION_UID="$verification_uid" \
	--build-arg BHP_VERIFICATION_GID="$verification_gid" \
	--file "$SCRIPT_DIRECTORY/linux-x86_64.Dockerfile" \
	--tag "$image_name" \
	"$PROJECT_ROOT"

image_architecture=$(
	"$docker_command" image inspect --format '{{.Architecture}}' "$image_name"
) || {
	bhp_fail VERIFY_LINUX_X86_64.IMAGE_INSPECTION_FAILED \
		"$image_name" "$TRACE_ID"
	exit 1
}
if [ "$image_architecture" != amd64 ]; then
	bhp_fail VERIFY_LINUX_X86_64.IMAGE_ARCHITECTURE_INVALID \
		"Expected amd64, got $image_architecture" "$TRACE_ID"
	exit 1
fi

bhp_log info linux_x86_64_verification_started "$TRACE_ID" \
	image "$image_name" platform linux/amd64 command make-verify
"$docker_command" run --rm \
	--platform linux/amd64 \
	--user "$verification_uid:$verification_gid" \
	--env BHP_SHELLCHECK_AARCH64_FALLBACK_ALLOWED="$shellcheck_aarch64_fallback_allowed" \
	--mount "type=bind,source=$PROJECT_ROOT,target=/workspace,readonly" \
	--tmpfs /workspace/.build:rw,exec,nosuid,nodev,mode=1777 \
	--workdir /workspace \
	"$image_name"
bhp_log info linux_x86_64_verification_complete "$TRACE_ID" \
	image "$image_name" platform linux/amd64
