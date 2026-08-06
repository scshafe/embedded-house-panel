#!/bin/sh
# Provisions the explicitly non-production root-public-key-only bring-up image.

set -eu

target_directory=${1:?TARGET_DIR argument is required}
: "${BR2_EXTERNAL_BHP_PATH:?BR2_EXTERNAL_BHP_PATH is required}"
: "${BHP_BRINGUP_AUTHORIZED_KEYS_FILE:?Bring-up public key input is required}"
project_root=$BR2_EXTERNAL_BHP_PATH/../..
# shellcheck source=/dev/null
. "$project_root/scripts/build/buildroot_contract.sh"

if ! bhp_bringup_authorized_key_is_valid \
	"$BHP_BRINGUP_AUTHORIZED_KEYS_FILE"; then
	printf '%s\n' \
		'BHP_BRINGUP.AUTHORIZED_KEY_INVALID: expected one valid Ed25519 public key' >&2
	exit 1
fi
test -d "$target_directory"
dropbear_defaults=$target_directory/etc/default/dropbear
test -f "$dropbear_defaults"
test ! -L "$dropbear_defaults"
grep -F -x 'DROPBEAR_ARGS="-s"' "$dropbear_defaults" >/dev/null || {
	printf '%s\n' \
		'BHP_BRINGUP.DROPBEAR_POLICY_INVALID: password authentication must be disabled' >&2
	exit 1
}

mkdir -p "$target_directory/root/.ssh"
chmod 0700 "$target_directory/root/.ssh"
authorized_keys=$target_directory/root/.ssh/authorized_keys
if [ -e "$authorized_keys" ] || [ -L "$authorized_keys" ]; then
	test -f "$authorized_keys"
	test ! -L "$authorized_keys"
fi
authorized_keys_new=$authorized_keys.bhp-new
test ! -e "$authorized_keys_new"
test ! -L "$authorized_keys_new"
install -m 0600 "$BHP_BRINGUP_AUTHORIZED_KEYS_FILE" "$authorized_keys_new"
mv "$authorized_keys_new" "$authorized_keys"

profile_directory=$target_directory/etc/beaglebone-house-panel
mkdir -p "$profile_directory"
profile_path=$profile_directory/image-profile
fingerprint_path=$profile_directory/bringup-authorized-key-fingerprint
test ! -L "$profile_path"
if [ -e "$fingerprint_path" ] || [ -L "$fingerprint_path" ]; then
	test -f "$fingerprint_path"
	test ! -L "$fingerprint_path"
fi
printf '%s\n' bringup-root-public-key-only >"$profile_path"
key_fingerprint=$(ssh-keygen -l -E sha256 \
	-f "$BHP_BRINGUP_AUTHORIZED_KEYS_FILE" | awk '{ print $2 }')
case "$key_fingerprint" in
	''|*[!A-Za-z0-9:+/=]*|SHA256:)
		printf '%s\n' 'BHP_BRINGUP.KEY_FINGERPRINT_INVALID' >&2
		exit 1
		;;
	SHA256:*) ;;
	*)
		printf '%s\n' 'BHP_BRINGUP.KEY_FINGERPRINT_INVALID' >&2
		exit 1
		;;
esac
fingerprint_new=$fingerprint_path.bhp-new
test ! -e "$fingerprint_new"
test ! -L "$fingerprint_new"
printf '%s\n' "$key_fingerprint" >"$fingerprint_new"
mv "$fingerprint_new" "$fingerprint_path"
