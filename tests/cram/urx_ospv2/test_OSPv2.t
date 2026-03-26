Create R alias:

$ alias R="${CRAM_REMOTE_COMMAND:-}"

$ R cat /etc/board.json |yq -r ".model.name"
  mxl,osp-tb341-v2
