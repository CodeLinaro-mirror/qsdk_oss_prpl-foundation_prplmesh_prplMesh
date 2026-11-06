#!/bin/bash -e

# We have to copy the source directory, because we may not have
# write access to it, and openwrt needs to at least write '.source_dir':
cp -r /home/openwrt/prplMesh_source /home/openwrt/prplMesh
# We want to make sure that we do not keep anything built from the host:
(cd /home/openwrt/prplMesh && \
    rm -rf build ipkg-* .built* .configured* .pkgdir .prepared .quilt_checked .source_dir)

# Remove the prplMesh package built from the feed in the built docker image
make package/prplmesh/clean

make package/prplmesh/prepare USE_SOURCE_DIR="/home/openwrt/prplMesh" V=s

# Run a make target in parallel. If it fails, rebuild it with V=sc to get
# a readable log, but exit immediately even if the second build succeeds
# (to let the user/CI know that the parallel build failed).
run_make() {
    if ! make -j"$(nproc)" "$@" ; then
        echo "Build of '$*' failed. Rebuilding with -j1."
        make V=sc "$@"
        exit 1
    fi
}

# The rest of prplOS is already built in the builder image, so only
# rebuild prplMesh, then regenerate the root filesystem (from all the
# packages in bin/, including the new prplMesh ipk) and the images.
# Finally regenerate the sha256sums of the images (copied to the artifacts):
run_make package/prplmesh/compile
run_make package/install
run_make target/install
run_make checksum

mkdir -p artifacts
cat << EOT >> artifacts/prplmesh.buildinfo
TARGET_SYSTEM=${TARGET_SYSTEM}
PRPLOS_VERSION=${PRPLOS_VERSION}
PRPLMESH_VERSION=${PRPLMESH_VERSION}
EOT

# If the target is OSP URX; move the build files from the intel_x86 target directory
TARGET_SYSTEM=${TARGET_SYSTEM//mxl_x86_osp_tb341*/intel_x86}
TARGET_SYSTEM=${TARGET_SYSTEM//qca_ipq95xx/ipq95xx}

TARGET_SYSTEM=${TARGET_SYSTEM//ipq807x/qualcommax}

# Mediatek Mozart has it's binaries in the mediatek folder
TARGET_SYSTEM=${TARGET_SYSTEM//mtk_filogic/mediatek}

find bin -name 'prplmesh_*.ipk' -exec cp -v {} "artifacts/prplmesh.ipk" \;
find bin/targets/"$TARGET_SYSTEM"/*/ -type f -maxdepth 1 -exec cp -v {} "artifacts/" \;
[ -d bin/targets/intel_x86/lgm/single-images ] && find bin/targets/intel_x86/lgm/single-images/ -type f -maxdepth 1 -exec cp -v {} "artifacts/" \;

# Rename the prplos image
find artifacts/ -type f -name 'prplos-*' -exec bash -c 'mv $0 ${0/\prplos/openwrt}' {} \;
cp .config artifacts/openwrt.config
cp files/etc/prplwrt-version artifacts/
