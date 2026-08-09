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

echo "tag=$tag version.mk=$src amipilot.readme=$readme"
[ "$tag" = "$src" ]    || { echo "::error file=version.mk::Tag v$tag does not match version.mk \"$src\""; exit 1; }
[ "$tag" = "$readme" ] || { echo "::error file=amipilot.readme::Tag v$tag does not match Version: \"$readme\""; exit 1; }
