#!/bin/bash

# log section helper
function section_start() {
  local section_title="${1}"
  local section_description="${2:-$section_title}"
  local collapse_state="${3:-collapsed}"
  local collapsed_flag="true"
  if [ "$collapse_state" = "expanded" ]; then
    collapsed_flag="false"
  fi
  echo -e "section_start:$(date +%s):${section_title}[collapsed=${collapsed_flag}]\r\e[0K\e[95m📦 ${section_description}\e[0m"
}

function section_end() {
  local section_title="${1}"
  echo -e "section_end:$(date +%s):${section_title}\r\e[0K"
}


