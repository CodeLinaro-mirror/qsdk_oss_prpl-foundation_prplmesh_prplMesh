#!/bin/bash

if [ -z ${TARGET_DEVICE_1+x} ]; then
    echo "No TARGET_DEVICE_1 specified!"
    exit 1
fi


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

get_IP_of_target(){
    IP=$(tomlq -r ".$1.IP" prplMesh/testbed_devices.toml)

    if [ "${IP}" = "null" ]; then
        exit 1
    fi

    echo $IP
}


copy_tests_multi_target(){

    echo "test"
    
    IP1=$(get_IP_of_target $TARGET_DEVICE_1) || { echo "Failed to get IP of device $TARGET_DEVICE_1, exiting" >&2; exit 1; }
    IP2=$(get_IP_of_target $TARGET_DEVICE_2) || { echo "Failed to get IP of device $TARGET_DEVICE_2, exiting" >&2; exit 1; }
    IP3=$(get_IP_of_target $TARGET_DEVICE_3) || { echo "Failed to get IP of device $TARGET_DEVICE_3, exiting" >&2; exit 1; }

    export CRAM_REMOTE_COMMAND_DEVICE_1="${CRAM_REMOTE_COMMAND_BASE}${IP1}"
    export CRAM_REMOTE_COMMAND_DEVICE_2="${CRAM_REMOTE_COMMAND_BASE}${IP2}"
    export CRAM_REMOTE_COMMAND_DEVICE_3="${CRAM_REMOTE_COMMAND_BASE}${IP3}"

    echo $CRAM_REMOTE_COMMAND_DEVICE_1
    echo $CRAM_REMOTE_COMMAND_DEVICE_2

    exit 0

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



if [ -z ${TARGET_DEVICE_2+x} ]; then
    # Single device under test
    create_test_folders "$TARGET_DEVICE_1"
    copy_tests_single_target
else
    # multi device CRAM test
    create_test_folders "multi"
    copy_tests_multi_target
fi

exit 0

cd tests/ || exit 1

timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose init
timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose "$TARGET_DEVICE_1"
timeout --kill-after=10s --verbose 1200s python3 -m cram --verbose post

if FAILED_TESTS=$(find . -name "*.t.err" |grep .); then
    echo -e "\e[91mTESTS FAILED:"
    echo -e "$FAILED_TESTS\e[0m"
    exit 1
else
    echo -e "\e[92mTESTS PASSED\e[0m"
    exit 0
fi