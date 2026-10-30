#!/usr/bin/env bash
# 세션 열기: ./go main 은 루트에서, ./go research (product, verify) 는 그 폴더를 작업 폴더로 Codex 를 연다. 첫 글로 역할 이름을 보낸다
# 모든 세션을 yolo (--dangerously-bypass-approvals-and-sandbox, 승인 창과 샌드박스 없음) 로 연다. 기본 모드는 HARNESS_YOLO=0 ./go research
# yolo 라 밖에 쓰는 것을 막지 못한다. 외주가 h done 하면 h 가 밖에서 바뀐 파일을 잡아 verify 자동 검토에 넘긴다
# go 가 없으면 같은 일: codex --yolo -C research research
set -e
here="$(cd "$(dirname "$0")" && pwd)"
role="${1:-main}"
[ $# -gt 0 ] && shift
codex_bin="${HARNESS_CODEX:-codex}"
yolo=(--dangerously-bypass-approvals-and-sandbox)
if [ "${HARNESS_YOLO:-1}" = 0 ]; then yolo=(); fi
if [ "$role" = main ]; then
  cd "$here"
  exec "$codex_bin" "${yolo[@]}" "$@" main
fi
if [ ! -d "$here/$role/delivery" ]; then
  echo "go: $role 세션 폴더가 없다 (있는 것: $(ls -d "$here"/*/delivery 2>/dev/null | sed "s#$here/##; s#/delivery##" | tr '\n' ' '))" >&2
  exit 1
fi
exec "$codex_bin" "${yolo[@]}" -C "$here/$role" "$@" "$role"
