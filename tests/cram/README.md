<!--
SPDX-License-Identifier: BSD-2-Clause-Patent
Copyright (c) 2026 the prplMesh contributors
This code is subject to the terms of the BSD+Patent license.
See LICENSE file for more details.
-->

### CRAM
PrplMesh CI can execute prplOS and prplMesh specified (this folder) CRAM tests.
The CI checks out the prplOS version specified in `tools/docker/builder/openwrt/build.sh`, and copies the tests specified in `prplOS_tests.toml`.

### Running prplOS CRAM tests with `prplOS_tests.toml`
In order to let the CI run prplOS cram tests, add or adapt a platform segment in `prplOS_tests.toml`:

```
[freedom]                              <--- platform name in prplMesh CI
name_in_prplOS = "wnc-freedom"         <--- platform name in prplOS CI (.gitlab/tests/cram/wnc-freedom)
tests = [                              <--- tests to copy from the platform specific folder
    "020-ubus.t",
    ...
```
Copied generic tests and generic tests specified in the prplMesh `generic` folder will be ran on each DUT.
PrplOS post tests (`.gitlab/tests/cram/post/`) are always merged with prplMesh post tests and executed for each DUT.

### Test order
1. Run tests specified in the prplMesh _init_ folder (`./tests/cram/init`)
2. Merge _generic_ tests from prplMesh (`./tests/cram/generic`) and prplOS (`.gitlab/tests/cram/generic/`) and run them
3. Merge _platform specic_ tests from prplMesh (`./tests/cram/freedom`) and prplOS (`.gitlab/tests/cram/wnc-freedom`) and run them
4. Merge _post_ tests from prplMesh (`./tests/cram/post`) and prplOS (`.gitlab/tests/cram/post`) and run them

### CRAM folder structure:
```
  cram                                      <--- this folder
    |
    +- prplOS_tests.toml                    <--- describes what prplOS tests to copy, per platform
    +- init                                 <--- DUT initialisation/tests
    |   |
    |   +- 0_enable_radios_prplmesh.t       <--- enables radios and prplmesh on the DUT
    |   +- ..
    +- generic                              <--- generic tests, always executed on each DUT
    |   |
    |   +- 10_check_radios_prplmesh.t
    |   +- ...
    +- freedom / urx_ospv2 / ..             <--- DUT specific tests
    |   |
    |   +- test_xxx.t
    |   +- ...
    +- post                                 <--- DUT post scripts/tests, merged to prplOS post tests (eg crash check)
        |
        +- 9999_disable_radios_prplmesh.t
        +- ...
```

## CRAM docker image generation
Building a CRAM docker image is possible with the `image-build.sh` script, specify a PRPLOS revision if you want to copy prplOS CRAMtests in the image:
```
export PRPLOS_HASH=latest-24.10
./tools/docker/image-build.sh --image cram --tag cram-latest-24.10 --argument PRPLOS_HASH
```

This builds a local image on the registry specified with `DOCKER_REGISTRY` in `./tools/functions.sh`:<br />
eg: `registry.gitlab.com/prpl-foundation/prplmesh/prplmesh/prplmesh-cram:cram-latest-24.10`

### Running CRAM tests locally with docker
Assuming you have a prplOS device connected to your local PC, reachable on 192.168.1.1,<br />
you can run this in the prplMesh repository:

```
export TARGET_LAN_IP=192.168.1.1
export CRAM_REMOTE_COMMAND="ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@$TARGET_LAN_IP"
export TARGET_DEVICE=freedom

docker run \
  -v "./tests/cram:/home/cram/prplMesh"\
  -e CRAM_REMOTE_COMMAND \
  -e TARGET_LAN_IP \
  -e TARGET_DEVICE \
  registry.gitlab.com/prpl-foundation/prplmesh/prplmesh/prplmesh-cram:cram-latest-24.10
```

The prplMesh CRAM test folder is mounted to the container in order to have access to its tests and `prplOS_tests.toml` file.<br />
`TARGET_DEVICE` should match the platform name in prplMesh CI.
