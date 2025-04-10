#!/bin/bash
. tools/scripts/feedata.sh
package="prplMesh"
# tokens should be set as CI environment variables
pToken="glpat-DqvUyKbo7zBYkCwsTgyk"
tToken="glptt-d1ff739fd61af24c3d1a01feef072fdafbcab0a7"

# create a branch(with predictable & unique name) in the feed repo
# example:
#   create "${merge_request_id}"
function create() {
  local feed="${feeds[${package}]}"
  local project="${pids[${feed}]}"
  local branch="${package}/$1"
  local encoded
  local status
  encoded=$(printf "%s" "$branch" | jq -sRr @uri)
  status=$(curl -s -o /dev/null -w "%{http_code}" \
                 --header "PRIVATE-TOKEN: ${pToken}" \
                 "https://gitlab.com/api/v4/projects/${project}/repository/branches/${encoded}")
  if [ "${status}" != "200" ]; then
    curl --silent --fail --show-error --request POST \
    --header "PRIVATE-TOKEN: ${pToken}" \
    --data "branch=${branch}&ref=main" \
    "https://gitlab.com/api/v4/projects/${project}/repository/branches"
  fi
}

# update the package description in the feed repo
# example:
#   update "${merge_request_id}" "${merge_request_commit_id}"
function update() {
  local branch="${package}/$1"
  local commit="$2"
  local feed="${feeds[${package}]}"
  local repo="${repos[${feed}]}"
  local makefile="${makefiles[${package}]}"
  local archive="${archives[${package}]}"
  local tarball="${archive}/${commit}/${package}-${commit}.tar.gz"
  local phash="$(curl -L ${tarball} | tee ${package}-${commit}.tar.gz | sha256sum)"
  phash="${phash%% *}"

  git clone -b "${branch}" --depth 1 "${repo}" repo
  sed -i "s/^PKG_VERSION:=.*/PKG_VERSION:=${commit}/" "repo/${makefile}"
  sed -i "s/^PKG_HASH:=.*/PKG_HASH:=${phash}/" "repo/${makefile}"
  (cd repo && git config --local user.name "bot" && git config --global user.email "bot@mind.be")
  (cd repo && git add -u && git commit -m "update" && git push https://bot:${pToken}@gitlab.com/ludai/autoy.git "${branch}")
}

function poll() {
  echo "TODO"
}

function main() {
  local mriid="$1"
  local cid="$2"
  create "${mriid}"
  sleep 10
  update "${mriid}" "${cid}"
  poll
}

# parameters:
#   1. merge request identical id, which is unique in a project
#   2. merge request commit id
# output:
#   TODO: a pipeline id in upstream's upstream, i.e. prplos, enables to poll the result
# example:
#   feed.sh "$CI_MERGE_REQUEST_IID"  "$CI_COMMIT_SHA"
main "$@"
