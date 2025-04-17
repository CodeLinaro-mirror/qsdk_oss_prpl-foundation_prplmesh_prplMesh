#!/bin/bash

scriptdir="$(cd "${0%/*}" || exit 1; pwd)"
rootdir="${scriptdir%/*/*}"

PPM_VERSION=$(grep -E -o "prplmesh_VERSION \"[0-9]\.[0-9]\.[0-9]\"" "${rootdir}/cmake/multiap-helpers.cmake" | cut -d\" -f2)
[ -z "$VERSION" ] && VERSION="${PPM_VERSION}-custom-$(date -Iminutes)"


mkdir -p "${rootdir}/build" && pushd "${rootdir}/build" || exit 1
amxo-cg -G xml "${rootdir}/build/controller/nbapi/controller/odl/controller.odl"
amxo-cg -G xml "${rootdir}/build/agent/src/beerocks/slave/nbapi/agent/odl/agent.odl"
popd || exit 1
if [ ! -r "${rootdir}/build/controller.odl.xml" ]; then
    echo -e "\\033[1;31mXML generation for Controller ODL failed -- ODL syntax issue?\\033[0m"
    exit 1
elif [ ! -r "${rootdir}/build/agent.odl.xml" ]; then
    echo -e "\\033[1;31mXML generation for Agent ODL failed -- ODL syntax issue?\\033[0m"
    exit 1
fi

mkdir -p "${rootdir}/build/html/controller"
amxo-xml-to -x html\
                  -o output-dir="${rootdir}/build/html/controller"\
                  -o title="prplMesh Controller"\
                  -o sub-title="Northbound API"\
                  -o version="$VERSION"\
                  -o stylesheet="prpl_style.css"\
                  -o copyrights="prpl Foundation"\
                  "${rootdir}/build/controller.odl.xml"

mkdir -p "${rootdir}/build/html/agent"
amxo-xml-to -x html\
                  -o output-dir="${rootdir}/build/html/agent"\
                  -o title="prplMesh Agent"\
                  -o sub-title="Northbound API"\
                  -o version="$VERSION"\
                  -o stylesheet="prpl_style.css"\
                  -o copyrights="prpl Foundation"\
                  "${rootdir}/build/agent.odl.xml"
