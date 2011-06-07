#!/bin/sh

# Try to figure out version from git
GIT_DESC=$(git describe | sed s'/^vzctl-//')
GIT_V=$(echo $GIT_DESC | sed 's/-.*$//')
				# 3.0.28-1-gf784152
GIT_R=$(echo $GIT_DESC | sed 's/^[^-]*-\([1-9][0-9]*\)-g/git.\1./')
test "$GIT_V" = "$GIT_R" && GIT_R="1"
GIT_VR="${GIT_V}-${GIT_R}"

CONF_V=$(grep AC_INIT configure.ac | \
		sed 's/^[^,]*,[ ]\([^,]*\),.*$/\1/')

read_spec() {
	SPEC_V=$(awk '($1 == "Version:") {print $2}' vzctl.spec)
	SPEC_R=$(awk '($1 == "Release:") {print $2}' vzctl.spec | \
		sed 's/%.*$//')
	SPEC_VR="${SPEC_V}-${SPEC_R}"
}
read_spec

# Set version/release in spec from git
if test "$GIT_VR" != "$SPEC_VR"; then
#	echo "Changing vzctl.spec:"
	# Version: 3.0.28
	# Release: 1%{?dist}
	sed -i -e "s/^\(Version:[[:space:]]*\).*\$/\1$GIT_V/" \
	       -e "s/^\(Release:[[:space:]]*\)[^%]*\(%.*\)\$/\1$GIT_R\2/" \
		vzctl.spec
fi
#grep -E -H '^Version:|^Release:' vzctl.spec

# Set version in configure.ac from spec
read_spec
SPEC_VR=$(echo $SPEC_VR | sed 's/-1$//')
if test "$CONF_V" != "$SPEC_VR"; then
#	echo "Changing configure.ac:"
	# AC_INIT(vzctl, 3.0.28, devel@openvz.org)
	sed -i "s/^\(AC_INIT(vzctl,[ ]\)[^,]*\(,.*\$\)/\1$SPEC_VR\2/" \
		configure.ac
	autoconf
fi
#grep -H '^AC_INIT' configure.ac

exit 0