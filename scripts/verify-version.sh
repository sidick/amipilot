#!/bin/sh
# Verifies a release tag (e.g. v1.0) matches version.mk and amipilot.readme
# before release.yml hands off to sidick/amiga-workflows' aminet-release.yml
# - the calling repo's own version-file format is project-specific, so this
# check stays here rather than in the shared workflow (see that repo's own
# aminet-release.yml header comment for why).
#
# Usage: scripts/verify-version.sh vX.Y
set -eu

tag_ref="${1:?usage: verify-version.sh <tag, e.g. v1.0>}"
tag="${tag_ref#v}"

ver=$(sed -n 's/^VERSION[[:space:]]*:=[[:space:]]*//p' version.mk)
rev=$(sed -n 's/^REVISION[[:space:]]*:=[[:space:]]*//p' version.mk)
src="$ver.$rev"
readme=$(sed -n 's/^Version:[[:space:]]*\(.*\)$/\1/p' amipilot.readme)
# host/pyproject.toml carries a full X.Y.Z (PEP 440); version.mk/amipilot.readme
# only go to X.Y, so compare against that with a trailing ".0" appended.
pypkg=$(sed -n 's/^version[[:space:]]*=[[:space:]]*"\(.*\)"$/\1/p' host/pyproject.toml)

echo "tag=$tag version.mk=$src amipilot.readme=$readme host/pyproject.toml=$pypkg"
[ "$tag" = "$src" ]      || { echo "::error file=version.mk::Tag v$tag does not match version.mk \"$src\""; exit 1; }
[ "$tag" = "$readme" ]   || { echo "::error file=amipilot.readme::Tag v$tag does not match Version: \"$readme\""; exit 1; }
[ "$src.0" = "$pypkg" ]  || { echo "::error file=host/pyproject.toml::version.mk \"$src\" does not match pyproject.toml version \"$pypkg\""; exit 1; }
