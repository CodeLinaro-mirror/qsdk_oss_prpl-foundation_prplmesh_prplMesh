#!/bin/bash

file=$(realpath "${BASH_SOURCE[0]}")
dir=${file%/*}

source ${dir}/settings.sh

function clone() {
  local helper

  helper=$(echo "${urls[${feed}]}.git" | sed -E 's|https://([^/]+)/|git@\1:|')
  if [ ! -d "${feed}/.git" ]; then
    rm -rf "${feed}"
    git clone "${helper}" "${feed}"
  fi

  helper=$(echo "${urls[${profile}]}.git" | sed -E 's|https://([^/]+)/|git@\1:|')
  if [ ! -d "${profile}/.git" ]; then
    rm -rf "${profile}"
    git clone "${helper}" "${profile}" 
  fi

  if [ ! -d "${package}/.git" ]; then
    rm -rf "${package}"
    git clone "${urls[${package}]}.git" "${package}" 
  fi
}

function brief() {
  git fetch --prune
  local changelist msg
  changelist="$(git log --oneline ${tgt}..${src})"
  printf -v msg "BOT UPDATE\n\nCOMPONENT: %s\n\nMRIID: %s\n\nCOMMIT: %s\n\nChange List: \n%s\n" "${package}" "${mriid}" "${commit}" "${changelist}"
  echo "${msg}"
}

function create() {
  local master 
  master=$(git symbolic-ref refs/remotes/origin/HEAD | sed 's@^refs/remotes/origin/@@')
  git fetch --prune
  if ! git ls-remote --heads origin "${branch}" | grep -q "${branch}"; then
    git checkout -B "${branch}" "origin/${master}"
  else
    git checkout -B "${branch}" "origin/${branch}"
  fi
}

function substitute() {
  if [ -f "${makefile}" ]; then
    sed -i "s/^PKG_VERSION:=.*/PKG_VERSION:=${commit}/" "${makefile}"
    sed -i "s/^PKG_HASH:=.*/PKG_HASH:=${digest}/" "${makefile}"
  fi
  if [ -f "${yamlfile}" ]; then
    local vkey="${feed/-/_}"
    awk -v key="${feed}" -v vkey="${vkey}" -v value="$1" '
      $1 == "-" && $2 == "name:" {
        if ($NF == key || $NF == vkey) found=1
        else found=0
      }
      found && $1 == "revision:" {
        $0 = "    revision: " value
      }
      { print }
    ' "${yamlfile}" > "${yamlfile}.tmp" && mv "${yamlfile}.tmp" "${yamlfile}"
  fi
}

function push() {
  if ! git diff --quiet; then
    git add -u
    git commit -m "${commitmsg}"
    git push origin ${branch} 
  fi
}

function clean() {
  local master 
  master=$(git symbolic-ref refs/remotes/origin/HEAD | sed 's@^refs/remotes/origin/@@')
  git checkout ${master}
  git branch -D ${branch}
}

function main() {
  clone
  commitmsg="$(cd ${package} && brief)"
  (cd ${feed} && create && substitute && push)
  rev="$(cd ${feed} && git rev-parse HEAD)"
  (cd ${profile} && create && substitute ${rev} && push)
  (cd ${feed} && clean)
  (cd ${profile} && clean)
  return 0
}

main "$@"
