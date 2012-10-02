#!/bin/bash

# Examples of using:
# ./elmerfem_get-orig-source.sh
# ./elmerfem_get-orig-source.sh 7.0.svn.5845+dfsg
# ./elmerfem_get-orig-source.sh 5.5.0.svn.4499.dfsg
# ./elmerfem_get-orig-source.sh 6.1.0.svn.5396.dfsg2

PACKAGE=elmerfem
SRC_VERSION="${1}"
SVN_REPO="https://elmerfem.svn.sourceforge.net/svnroot/elmerfem/trunk"

if [ -z "${SRC_VERSION}" ]; then
    echo "Package version is not specified, last revision from SVN repo will be used."
    SVN_REVISION=$(svn log "${SVN_REPO}" | head -n2 |grep "r[0-9]\+" | sed -e "s/^r\([0-9]\+\).*$/\1/")
    if [ -z "${SVN_REVISION}" ]; then
        echo "Failed to find last SVN revision."
        exit 1
    fi
    echo "SVN_REVISION = ${SVN_REVISION}"
    
    CUR_VERSION=$(curl "${SVN_REPO}/fem/configure" 2>&1 | grep "\ VERSION=" | sed -e "s/^ VERSION=\(.\+\)$/\1/")
    if [ -z "${CUR_VERSION}" ]; then
        echo "Failed to define current version."
        exit 1
    fi
    echo "CUR_VERSION = ${CUR_VERSION}"
    
    SRC_VERSION="${CUR_VERSION}.svn.${SVN_REVISION}+dfsg"
    echo "SRC_VERSION  = ${SRC_VERSION}"
else
    echo "SRC_VERSION  = ${SRC_VERSION}"

    SVN_REVISION=$(echo ${SRC_VERSION} | sed -e "s/^.*.svn.\([0-9]\+\).dfsg.*$/\1/")
    if [ -z "${SVN_REVISION}" ]; then
        echo "Failed to get SVN revision from package version."
        exit 1
    fi
    echo "SVN_REVISION = ${SVN_REVISION}"
fi

TARBALL="${PACKAGE}_${SRC_VERSION}.orig.tar.xz"
rm -rf "${PACKAGE}-${SRC_VERSION}" "${TARBALL}"

echo "Start svn export, this will take some time..."
svn export -r ${SVN_REVISION} "${SVN_REPO}" "${PACKAGE}-${SRC_VERSION}" > svn-export.log || exit 1
echo "svn export finished successfully."

cd "${PACKAGE}-${SRC_VERSION}"
rm -rf mathlibs umfpack elmergrid/src/metis post/src/fonts elmergrid/acx_metis.m4
rm -rf */*.cache post/src/*/*.cache
rm -rf ElmerGUI/Application/plugins/tetgen.h misc/tetgen_plugin/*
cd ..

tar -cJf ${TARBALL} "${PACKAGE}-${SRC_VERSION}" || exit 1
rm -rf "${PACKAGE}-${SRC_VERSION}" svn-export.log

echo "${TARBALL} was created."
