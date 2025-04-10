#!/bin/bash

# create a branch in a project
# example:
#   create "68433016" "bot/1397"
function create() {
  local privateToken="glpat-DqvUyKbo7zBYkCwsTgyk"
  #local projectID="68433016"
  local project="$1"
  local branch="$2"
  local encoded
  local status
  encoded=$(printf "%s" "$branch" | jq -sRr @uri)
  status=$(curl -s -o /dev/null -w "%{http_code}" \
                 --header "PRIVATE-TOKEN: ${privateToken}" \
                 "https://gitlab.com/api/v4/projects/${project}/repository/branches/${encoded}")
  if [ "${status}" != "200" ]; then
    curl --silent --fail --show-error --request POST \
    --header "PRIVATE-TOKEN: ${privateToken}" \
    --data "branch=${branch}&ref=main" \
    "https://gitlab.com/api/v4/projects/${project}/repository/branches"
  fi
}

# trigger a feed repo to the update the package
# package and full commit id is required in addition to project/branch
# example:
#   trigger "68433016" "bot/1397" "prplmesh" "7f57a2372d84e5d812eda55dc52cc55602e599f7"
function trigger() {
  local token="glptt-d1ff739fd61af24c3d1a01feef072fdafbcab0a7"

  local project="$1"
  local branch="$2"
  local package="$3"
  local commit="$4"
  curl -X POST --fail \
       -F token="${token}" \
       -F ref="${branch}" \
       -F "variables[package]=${package}" \
       -F "variables[commit]=${commit}" \
       "https://gitlab.com/api/v4/projects/${project}/trigger/pipeline"
}

function poll() {
  echo "TODO"
}

function main() {
  local project="$1"
  local mriid="$2"
  local package="$3"
  local cid="$4"
  create "${project}" "autofeed/${mriid}"
  sleep 10
  trigger "${project}" "autofeed/${mriid}" "${package}" "${cid}"
  sleep 10
  poll
}

# parameters:
#   1. feed project id
#   2. merge request identical id, which is unique in a project
#   3. package name 
#   4. merge request commit id
# output:
#   TODO: a pipeline id in upstream's upstream, i.e. prplos, enables to poll the result
# example:
#   autofeed.sh "68433016" "$CI_MERGE_REQUEST_IID" "prplmesh" "$(git rev-parse HEAD)"
main "$@"
