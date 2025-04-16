#!/bin/bash

declare -a watches 
watches=(
  "https://gitlab.com/prpl-foundation/prplmesh/prplMesh"
  "https://gitlab.com/prpl-foundation/prplos/feeds/feed-prplmesh"
  "https://gitlab.com/ludai/autoy"
  "https://gitlab.com/ludai/autoz"
  "https://gitlab.com/prpl-foundation/prplos/prplos"
)
declare -A feeds
feeds=( \
  "prplMesh" \
  "autoy:prplmesh/Makefile" \
)
declare -A profiles
profiles=(
  "autoy"
  "autoz:profiles/prpl.yml"
)

package="${CI_PROJECT_NAME:=prplMesh}"
pid="${CI_PROJECT_ID:=20535651}"
mriid="${CI_MERGE_REQUEST_IID:=3935}"
url="${CI_PROJECT_URL:=https://gitlab.com/prpl-foundation/prplmesh/prplMesh}"
tgt="${CI_MERGE_REQUEST_TARGET_BRANCH_SHA:=2c1d837186829cda33a3015d6369b87a006f1416}"
src="${CI_MERGE_REQUEST_SOURCE_BRANCH_SHA:=98120f91c2432268f98d291681f1249fd9f64ee8}"
commit="${CI_COMMIT_SHA:=e036e35fcec508f6a718567aa773684323f82731}"
commitmsg="bot update"

branch="${package}/${mriid}"
encoded=$(printf "%s" "$branch" | jq -sRr @uri)
tarball="${package}-${commit}.tar.gz"
archive="${url}/-/archive/${commit}/${tarball}"
digest="$(curl -L ${archive} | tee ${tarball} | sha256sum)"
digest="${digest%% *}"
rm -f "${tarball}"

declare -A urls
function build_url_map() {
  local name
  for w in "${watches[@]}"; do
    name="${w##*/}"
    urls["${name}"]="${w}"
  done
}
build_url_map

function extract_feed_info() {
  local entry name makefile
  entry="${feeds[${package}]}"
  name="${entry%:*}"
  makefile=${entry#*:}
  echo "${name} ${makefile}"
}
read -r feed makefile <<< "$(extract_feed_info)"

function extract_profile_info(){
  local entry name yamlfile 
  entry="${profiles[${feed}]}"
  name="${entry%:*}"
  yamlfile=${entry#*:}
  echo "${name} ${yamlfile}"
}
read -r profile yamlfile <<< "$(extract_profile_info)"

