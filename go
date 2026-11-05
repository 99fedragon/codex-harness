#!/usr/bin/env bash
# 세션 열기: ./go main 은 루트에서, ./go research (product, verify) 는 그 폴더를 작업 폴더로 Codex 를 연다. 첫 글로 역할 이름을 보낸다
# 모든 세션을 yolo (--dangerously-bypass-approvals-and-sandbox, 승인 창과 샌드박스 없음) 로 연다. 기본 모드는 HARNESS_YOLO=0 ./go research
# 모델과 생각 깊이도 붙인다: 모델 HARNESS_MODEL (기본 gpt-6.1-sol, 빈 값이면 붙이지 않는다), 깊이는 메인 xhigh, 팀장 high, 팀원 꼴 medium (HARNESS_EFFORT 로 덮는다)
# yolo 라 밖에 쓰는 것을 막지 못한다. 팀장이 h done 하면 h 가 밖에서 바뀐 파일을 잡아 verify 자동 검토에 넘긴다
# go 가 없으면 같은 일: codex --yolo -m gpt-6.1-sol -c model_reasoning_effort=high -C research research
set -e
here="$(cd "$(dirname "$0")" && pwd)"
role="${1:-main}"
[ $# -gt 0 ] && shift
codex_bin="${HARNESS_CODEX:-codex}"
yolo=(--dangerously-bypass-approvals-and-sandbox)
if [ "${HARNESS_YOLO:-1}" = 0 ]; then yolo=(); fi
case "$role" in
  main) effort=xhigh;;
  *_[0-9]|*_[0-9][0-9]) effort=medium;;
  *) effort=high;;
esac
effort="${HARNESS_EFFORT:-$effort}"
model="${HARNESS_MODEL-gpt-6.1-sol}"
margs=()
if [ -n "$model" ]; then
  margs=(-m "$model" -c "model_reasoning_effort=$effort")
  echo "go: 모델 $model, 생각 깊이 $effort (이 이름이 안 되면 HARNESS_MODEL= ./go $role 로 열고 /model 로 고른다)" >&2
fi
if [ "$role" = main ]; then
  cd "$here"
  exec "$codex_bin" "${yolo[@]}" "${margs[@]}" "$@" main
fi
if [ ! -d "$here/$role/delivery" ]; then
  echo "go: $role 세션 폴더가 없다 (있는 것: $(ls -d "$here"/*/delivery 2>/dev/null | sed "s#$here/##; s#/delivery##" | tr '\n' ' '))" >&2
  exit 1
fi
exec "$codex_bin" "${yolo[@]}" "${margs[@]}" -C "$here/$role" "$@" "$role"
