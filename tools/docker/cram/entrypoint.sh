#!/bin/bash

if [ -z ${TARGET_DEVICE_1+x} ]; then
    echo "No TARGET_DEVICE_1 specified!"
    exit 1
fi

if [ ! -d prplMesh ]; then
  echo "Please bind mount your prplMesh directory to '/home/cram/prplMesh'"
fi

# shellcheck source=tests/cram/scripts/utils.sh
source "prplMesh/scripts/utils.sh"

create_test_folders(){
    TARGET_FOLDER="tests/$1/"

    # Create CRAM test folders and copy prplOS/Mesh tests in 1 target folder
    mkdir -p tests/init
    mkdir -p "$TARGET_FOLDER/prplMesh"
    mkdir -p "$TARGET_FOLDER/prplOS"
    mkdir -p tests/post
}

copy_tests_single_target(){
    # Copy initialisation used in prplMesh CRAM
    cp -r prplMesh/init/* tests/init/ || :

    # Copy CRAM tests defined in the prplMesh repo
    cp -r prplMesh/generic/* "$TARGET_FOLDER/prplMesh/" || :
    cp -r prplMesh/"$TARGET_DEVICE_1"/* "$TARGET_FOLDER/prplMesh/" || :

    # Copy scripts used in prplOS CRAM
    cp -r prplos/.gitlab/tests/cram/scripts "$TARGET_FOLDER"

    # Copy CRAM tests defined in prplOS_tests.toml in the prplMesh repo (generic and platform specific)
    if [ -f "prplMesh/prplOS_tests.toml" ]; then
        TARGET_NAME=$(tomlq -r ".$TARGET_DEVICE_1.name_in_prplOS" prplMesh/prplOS_tests.toml)
        while read -r testfile; do
            cp "prplos/.gitlab/tests/cram/generic/$testfile" "$TARGET_FOLDER/prplOS/"
        done < <(tomlq -r ".generic.tests[]" prplMesh/prplOS_tests.toml)

        while read -r testfile; do
            cp "prplos/.gitlab/tests/cram/$TARGET_NAME/$testfile" "$TARGET_FOLDER/prplOS/"
        done < <(tomlq -r ".$TARGET_DEVICE_1.tests[]" prplMesh/prplOS_tests.toml)
    fi

    # Copy post-checks used in prplOS/prplMesh CRAM
    cp -r prplos/.gitlab/tests/cram/post/* tests/post/ || :
    cp -r prplMesh/post/* tests/post/ || :
}

copy_tests_multi_target(){
    # Copy initialisation used in prplMesh CRAM
    #cp -r prplMesh/init/* tests/init/ || :

    # Copy scripts used in prplOS CRAM
    cp -r prplos/.gitlab/tests/cram/scripts "$TARGET_FOLDER"

    # Copy WPS CRAM tests defined in the prplMesh repo
    cp -r prplMesh/wps/* "$TARGET_FOLDER/prplMesh/" || :

    # Copy post-checks used in prplOS/prplMesh CRAM
    cp -r prplos/.gitlab/tests/cram/post/* tests/post/ || :
    cp -r prplMesh/post/* tests/post/ || :
}


parse_testbed_devices_file

if [ -z ${TARGET_DEVICE_2+x} ]; then
    # Single device under test
    create_test_folders "$TARGET_DEVICE_1"
    copy_tests_single_target

    cd tests/ || exit 1
    timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose init
    timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose "$TARGET_DEVICE_1"
    timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose post
else
    # multi device CRAM test
    create_test_folders "wps"
    copy_tests_multi_target

    cd tests/ || exit 1
    timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose "wps"
fi

copy_logs_of_targets "/home/cram/logs"

if FAILED_TESTS=$(find . -name "*.t.err" |grep .); then
    echo -e "\e[91mTESTS FAILED:"
    echo -e "$FAILED_TESTS\e[0m"
    exit 1
else
    echo -e "\e[92mTESTS PASSED\e[0m"
    exit 0
fi

