#!/bin/sh -e

function run {
    desc="$1"
    cmd="$2"

    if [ x"$DOC" = xdoc ]; then
	echo " - $desc"
        echo "   - $cmd"
    else
        echo "===="
        echo "Phase: $desc"
        echo "  Run: $cmd"
        okay
        $cmd
        okay
    fi
}

function find_version {
    grep AC_INIT $@
    VERS=`grep AC_INIT $@ | awk -F[ '{print $3}' | sed 's/].*//'`
}

function bump_version {
    find_version ../configure.ac
    OLD_VERSION=$VERS

    vi ../configure.ac

    find_version ../configure.ac
    NEW_VERSION=$VERS

    echo "previous version: " $OLD_VERSION
    echo "new version:      " $NEW_VERSION
}

function okay {
   /bin/echo -n "proceed? "
   read OKAY
   if [ x"$OKAY" = xn -o  x"$OKAY" = xno ]; then
      exit 1;
   fi
}

if [ "$1" = "-d" ]; then
    DOC=doc
fi

echo "starting new release"

run "commit any changes" "gt commit"
run "move to master branch" "gt br master"
run "merge development changes into master" "gt merge-from develop"
run "bump version number in configure.ac" "bump_version"

run "autoconf" "autoreconf --install"
run "configure" "../configure $CONFIGURE_OPTS"
run "build and test" \
  "make clean && make && make install && make test && make dist"
run "Commit any changes" "gt commit"

run "upload any documentation changes to gh-pages/" "make upload"
run "add the release tag" "gt tag $NEW_VERSION"
run "publish packaging data to gh-pages/" "make packages"

run "move back to develop branch" "gt br develop"
run "pick up changes for configure.ac" "gt merge-from master"
run "show diffs" "gt diff ${OLD_VERSION}...${NEW_VERSION}"
run "goto https://github.com/Juniper/libslax/releases" "open https://github.com/Juniper/libslax/releases"
