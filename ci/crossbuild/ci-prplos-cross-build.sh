#!/bin/bash -e

scriptpath="$(realpath "${BASH_SOURCE[0]}")"
rootdir="${scriptpath%/*/*/*}"

# shellcheck source=/dev/null
source "${rootdir}/ci/common/ci-log-section.sh"

section_start "Help" "Variables List"
echo "*********************************************************************"
echo "CI_PRPLOS_COMMIT_OVERWRITE: default empty"
echo "    over write prplos commit id for the job, non-reproducible"
echo ""
echo "CI_TARGET_BOARD: default empty"
echo "    freedom, mozart, ospv2"
echo ""
echo "CI_CONSERVATIVE_BUILD: default yes"
echo "    for fresh prplos build, do not re-use host tool and toolchain"
echo ""
echo "CI_RUNNER_VOLUME_CLEAN_: default empty"
echo "    remove all contents in runner cache volume, must tag to runner"
echo ""
echo "CI_RUNNER_SLOT_CLEAN: default empty"
echo "    remove existing prplos building slot for the same commit, if there has"
echo ""
echo "*********************************************************************"
section_end "Help"

# ci parameters
section_start "Parameters" "Parameters Checking"
prplmesh="${CI_PROJECT_DIR}"
prplosurl="https://gitlab.com/prpl-foundation/prplos/prplos.git"
prploscommit="${CI_PRPLOS_COMMIT_OVERWRITE:-678a2eac4b1ad17cd6558bc74fd2c53bdb5ed9e1}"
shortid="${prploscommit:0:8}"
target="${CI_TARGET_BOARD:-unkown}"
conservative="no"
if [ "${CI_CONSERVATIVE_BUILD}x" == "yesx" ]; then
  conservative="yes"
fi
declare -rA supports=(
  [freedom]="supported"
  [ospv2]="supported"
  [mozart]="supported"
)
echo "  prplmesh dir:        ${prplmesh}"
echo "  prplos url:          ${prplosurl}"
echo "  prplos commit:       ${prploscommit}"
echo "  prplos short id:     ${shortid}"
echo "  building target:     ${target}"
echo "  conservative build:  ${conservative}"
if [ "${supports["${target}"]}x" == "x" ]; then
  echo "  target ${target} not supported or not defined"
  exit 1
fi
section_end "Parameters"

# runner cache parameters
section_start "Volume" "Runner Volume Checking"
cachedir="/cache"
dldir="${cachedir}/dl"
ccachedir="${cachedir}/ccache-${target}"
toolsdir="${cachedir}/tools"
toolchaindir="${cachedir}/toolchain"
slots="${cachedir}/slots"
prplos="${cachedir}/slots/prplos-${shortid}-${target}"
rm shared.env || true
echo "prplos=${prplos}" > shared.env
echo "  base dir:      ${cachedir}"
echo "  dlcache dir:   ${dldir}"
echo "  ccache dir:    ${ccachedir}"
echo "  tools dir:     ${toolsdir}"
echo "  toolchain dir: ${toolchaindir}"
echo "  prplos slots:  ${slots}"
echo "  current slot:  ${prplos}"
echo "  checking write permission"
sudo chown -R builder:builder "${cachedir}" || true
perm="${cachedir}/perm.txt"
touch "${perm}" && rm "${perm}"
echo "  write permission okay"
if [ "${CI_RUNNER_VOLUME_CLEAN_}x" != "x" ]; then
  echo "CI_RUNNER_VOLUME_CLEAN_ set, clean all runner volumes"
  if [ "x$(realpath "${cachedir}")" != "x/" ]; then
    rm -rf "${cachedir:-redbear}"/*
  else
    echo "cache path is invalid"
    echo "give up to clean volumes"
  fi
  exit 0
fi
mkdir -p "${dldir}" "${ccachedir}" "${toolsdir}" "${toolchaindir}" "${slots}" "${prplos}"
echo "  cache disk usage:"
echo "*********************************************************************"
du -h -d 2 "${cachedir}/"
echo "*********************************************************************"
section_end "Volume"

section_start "Slots" "Prepare prplos working slot"
hot="no"
if [ "${CI_RUNNER_SLOT_CLEAN}x" != "x" ]; then
  rm "${prplos}/.built" || true
fi

if [ -f "${prplos}/.built" ]; then
  echo "  reuse slot: ${prplos}"
  hot="yes"
else
  echo "  clone prplos with history, will do fresh build"
  rm -rf "${prplos}"
  git clone "${prplosurl}" "${prplos}"
  git -C "${prplos}" checkout "${prploscommit}"
fi
section_end "Slots"

# switch to prplos working directory
pushd "${prplos}" > /dev/null

section_start "Config" "Generate config"
declare -rA profiles=(
  [freedom]="qca_ipq95xx prpl debug ci"
  [ospv2]="mxl_x86_osp_tb341_v2 mxl_wlan_hostap_ng_wav700 prpl debug ci"
  [mozart]="mtk_filogic prpl debug ci"
)
touse="${profiles["${target}"]}"

if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip config"
else
{
  printf '%s\n' "---"
  printf 'Description: Configure to use download cache, ccache, host tools cache\n\n'
  printf 'diffconfig: |\n'
  printf '  CONFIG_DEVEL=y\n'
  # TODO: PPW-2006
  if [ "${target}x" != "ospv2x" ]; then
    printf '  CONFIG_AUTOREMOVE=y\n'
  fi
  printf '  CONFIG_DOWNLOAD_FOLDER=\"%s\"\n' "${dldir}"
  printf '  CONFIG_CCACHE=y\n'
  printf '  CONFIG_CCACHE_DIR=\"%s\"\n' "${ccachedir}"
  printf '  CONFIG_BUILD_LOG=y\n'
  printf '  CONFIG_BUILD_LOG_DIR=\"%s/logs\"\n' "${CI_PROJECT_DIR}"
  printf '  CONFIG_SRC_TREE_OVERRIDE=y\n'
} > "${prplos}/profiles/ci.yml"
  echo "injected ci profile:"
  echo "*********************************************************************"
  cat "${prplos}/profiles/ci.yml"
  echo "*********************************************************************"
  echo "  using profiles: ${touse}"
  echo "${touse}" | xargs scripts/gen_config.py
  echo "  config generated"
fi

# this approach to inject version information is not good
# but current ci needs this
mkdir -p files/etc/
rm files/etc/prplwrt-version || true
touch files/etc/prplwrt-version
{
printf '%s=%s\n' "OPENWRT_REPOSITORY" "${prplosurl}"
printf '%s=%s\n' "OPENWRT_VERSION" "${prploscommit}"
printf '%s=%s\n' "OPENWRT_TOOLCHAIN_VERSION" "${prploscommit}"
} >> files/etc/prplwrt-version

read -r -a arr <<< "${touse}"
for profile in "${arr[@]}" ; do
  printf "\nProfile %s:\n" "${profile}" >> files/etc/prplwrt-version
  cat "profiles/${profile}.yml" >> files/etc/prplwrt-version
done

rm prplmesh.buildinfo || true
touch prplmesh.buildinfo
declare -rA systems=(
  [freedom]="qca_ipq95xx"
  [ospv2]="mxl_x86_osp_tb341_v2"
  [mozart]="mtk_filogic"
)
system="${systems["${target}"]}"
{
printf '%s=%s\n' "TARGET_SYSTEM" "${system}"
printf '%s=%s\n' "OPENWRT_VERSION" "${prploscommit}"
printf '%s=%s\n' "OPENWRT_TOOLCHAIN_VERSION" "${prploscommit}"
printf '%s=%s\n' "PRPLMESH_VERSION" "${CI_COMMIT_SHA}"
} >> prplmesh.buildinfo

section_end "Config"

section_start "Ccache" "Prepare ccache"
export CCACHE_DIR="${ccachedir}"
export CCACHE_CONFIGPATH2="${ccachedir}/ccache.conf"
export CCACHE_BASEDIR="${cachedir}/slots"
export CCACHE_NOHASHDIR=1
if [ ! -f "${CCACHE_CONFIGPATH2}" ]; then
  touch "${CCACHE_CONFIGPATH2}"
{
  echo "compiler_type=gcc"
  echo "depend_mode=true"
  echo "sloppiness=file_macro,locale,time_macros,include_file_ctime,include_file_mtime"
} >> "${CCACHE_CONFIGPATH2}"
fi
cat "${CCACHE_CONFIGPATH2}"
section_end "Ccache"

section_start "Hosttools" "Build host tool"
if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip host tool building"
else
  if [ "${conservative}x" != "yesx" ]; then
    toolsrev="$(git -C "${prplos}" log --format=%h --abbrev=8 -n 1 -- tools)"
    if [ -f "${toolsdir}/tools-${toolsrev}-${target}.tar" ]; then
      echo "  reuse host tools: ${toolsdir}/tools-${toolsrev}-${target}.tar"
      ./scripts/ext-tools.sh --tools "${toolsdir}/tools-${toolsrev}-${target}.tar"
    fi
  fi
  time make -j"$(nproc)" tools/install
fi
section_end "Hosttools"

section_start "Download" "Download packages"
if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip download"
else
  time make -j"$(nproc)" download
fi
section_end "Download"

section_start "Toolchain" "Build toolchain"
declare -rA toolchainparams=(
  [freedom]="ipq95xx/generic"
  [ospv2]="intel_x86/lgm"
  [mozart]="mediatek/filogic"
)
if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip toolchain"
else
  if [ "${conservative}x" != "yesx" ]; then
    toolchainrev="$(git -C "${prplos}" log --format=%h --abbrev=8 -n 1 -- toolchain)"
    if [ -d "${toolchaindir}/toolchain-${toolchainrev}-${target}" ]; then
      echo "  reuse toolchain: ${toolchaindir}/toolchain-${toolchainrev}-${target}.tar.zst"
      ./scripts/ext-toolchain.sh --toolchain "${toolchaindir}/toolchain-${toolchainrev}-${target}" --overwrite-config --config "${toolchainparams["${target}"]}"
    fi
  fi
  time make -j"$(nproc)" toolchain/install
fi
section_end "Toolchain"

section_start "World" "Build world"
if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip rebuilding world"
else
  time make -j"$(nproc)"
  touch "${prplos}/.built"
fi
touch "${prplos}/.timestamp"
section_end "World"

section_start "Archive" "Cacheing products"
if [ "${hot}" == "yes" ]; then
  echo "  hot build, skip cacheing products"
else
  toolchainrev="$(git -C "${prplos}" log --format=%h --abbrev=8 -n 1 -- toolchain)"
  toolchainslot="${toolchaindir}/toolchain-${toolchainrev}-${target}"
  if [ ! -d "${toolchainslot}" ]; then
    echo "  saving to toolchain: ${toolchainslot}"
    make -j"$(nproc)" target/toolchain/install
    tarball="$(find bin/targets/ -name 'prplos-toolchain*.tar.zst')"
    echo "  from tarball: ${tarball}"
    mkdir -p "${toolchainslot}"
    tar --zstd -xpf "${tarball}" --wildcards --strip-components=2 -C "${toolchainslot}" 'prplos-toolchain*/toolchain*'
  fi

  toolsrev="$(git -C "${prplos}" log --format=%h --abbrev=8 -n 1 -- tools)"
  if [ ! -f "${toolsdir}/tools-${toolsrev}-${target}.tar" ]; then
    echo "  saving host tools: ${toolsdir}/tools-${toolsrev}-${target}.tar"
    tar -cf "${toolsdir}/tools-${toolsrev}-${target}.tar" staging_dir/host build_dir/host
  fi
fi
section_end "Archive"

section_start "PrplMesh" "Build prplmesh"
make package/prplmesh/clean
make package/prplmesh/prepare USE_SOURCE_DIR="${CI_PROJECT_DIR}"
make -j"$(nproc)"
section_end "PrplMesh"

section_start "Metrics"
staging_dir/host/bin/ccache --show-stats
staging_dir/host/bin/ccache --zero-stats
echo "*********************************************************************"
echo "builder threads: $(nproc)"
echo "cpu quota: $(cat /sys/fs/cgroup/cpu.max)"
echo "cpu frequency:"
grep "MHz" /proc/cpuinfo || true
echo "*********************************************************************"
section_end "Metrics"

popd > /dev/null

