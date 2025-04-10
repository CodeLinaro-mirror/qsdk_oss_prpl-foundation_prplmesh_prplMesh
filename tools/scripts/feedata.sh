#!/bin/bash

# package name -> feed name kv mapping
declare -A feeds
feeds=( \
  "prplMesh" \
#  "feed-prplmesh" \
  "autoy" \
)

# name -> url kv mapping
declare -A repos 
repos=( \
  "prplMesh" \
  "https://gitlab.com/prpl-foundation/prplmesh/prplMesh.git" \
  "feed-prplmesh" \
  "https://gitlab.com/prpl-foundation/prplos/feeds/feed-prplmesh.git" \
  "autoy" \
  "https://gitlab.com/ludai/autoy.git" \
)

# package name -> package file kv mapping
declare -A makefiles
makefiles=( \
  "prplMesh" \
  "prplmesh/Makefile" \
)

# package -> archive url kv mapping
declare -A archives
archives=( \
  "prplMesh" \
  "https://gitlab.com/prpl-foundation/prplmesh/prplMesh/-/archive" \
)

# project -> project id kv mapping
# copy it from the settings of the project
declare -A pids 
pids=( \
  "feed-prplmesh" \
  "65898303" \
  "prplMesh" \
  "20535651" \
  "autoy" \
  "68433016" \
)

