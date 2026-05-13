#!/bin/bash +e

target="${CI_TARGET_BOARD:-unkown}"
declare -rA supports=(
  [freedom]="supported"
  [ospv2]="supported"
  [mozart]="supported"
)
if [ "${supports["${target}"]}x" == "x" ]; then
  echo "  target ${target} not supported or not defined"
  exit 1
fi

destination="${target}"
if [ "${target}x" == "ospv2x" ]; then
  destination="urx_ospv2"
fi

if [ "${CI_JOB_STATUS}" != "success" ]; then
  exit 0
fi

prplos=""
# shellcheck source=/dev/null
source shared.env

if [ "${prplos}x" == "x" ]; then
  exit 0
fi


rm -rf "${CI_PROJECT_DIR}/build"
mkdir -p "${CI_PROJECT_DIR}/build/${destination}"
find "${prplos}/bin/" -name 'prplmesh_*.ipk' -exec cp {} "${CI_PROJECT_DIR}/build/${destination}/prplmesh.ipk" \;
cp "${prplos}/.config" "${CI_PROJECT_DIR}/build/${destination}/openwrt.config"
cp "${prplos}/files/etc/prplwrt-version" "${CI_PROJECT_DIR}/build/${destination}/prplwrt-version"
cp "${prplos}/prplmesh.buildinfo" "${CI_PROJECT_DIR}/build/${destination}/prplmesh.buildinfo"

declare -rA subtargets=(
  [freedom]="ipq95xx/generic"
  [ospv2]="intel_x86/lgm"
  [mozart]="mediatek/filogic"
)
find "${prplos}/bin/targets/${subtargets["${target}"]}/" -maxdepth 1 -type f -exec cp {} "${CI_PROJECT_DIR}/build/${destination}/" \;

if [ "${target}x" == "ospv2x" ]; then
  find "${prplos}/bin/targets/${subtargets["${target}"]}/single-images" -maxdepth 1 -type f -exec cp {} "${CI_PROJECT_DIR}/build/${destination}/" \;
fi

find "${CI_PROJECT_DIR}/build/${destination}/" -type f -name 'prplos-*' -exec bash -c 'mv $0 ${0/\prplos/openwrt}' {} \;
mv "${CI_PROJECT_DIR}/logs" "${CI_PROJECT_DIR}/build/"


