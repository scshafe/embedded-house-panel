#!/bin/sh
# @canonical sha256_file -- all scripts calculate SHA-256 through this function.

bhp_sha256_file()
{
  sha256_file_path=$1
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$sha256_file_path" | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$sha256_file_path" | awk '{print $1}'
    return
  fi
  printf '%s\n' "SHA256.NO_IMPLEMENTATION: sha256sum or shasum is required" >&2
  return 1
}

