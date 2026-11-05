#!/usr/bin/env bash
# 환경 점검: 이 하네스가 쓰는 것 (도구, 모델 지정, 파이썬 패키지, h 의 전달과 시험 관문과 자동 검토, 팀원 자동) 이
# 이 환경에서 되는지 한 번씩 해 보고 번호마다 ok 를 찍는다. 하네스 (setup.sh, h, go) 는 이 파일을 부르지도 깔지도 않는다
#   bash check.sh           1 ~ 7 (2 ~ 3분)
#   bash check.sh preview   뼈대 웹 앱을 3000 포트에 띄운다. 8 은 사람이 미리보기에서 본다 (끝내기 Ctrl+C)
# 쓰는 곳은 임시 폴더 하나뿐이다 (HARNESS_CHECK_DIR, 기본 ${TMPDIR:-/tmp}/harness_check). 작업 폴더에는 쓰지 않는다
set -u
here="$(cd "$(dirname "$0")" && pwd)"
base="${HARNESS_CHECK_DIR:-${TMPDIR:-/tmp}/harness_check}"
codex="${HARNESS_CODEX:-codex}"
model="${HARNESS_MODEL-gpt-6.1-sol}"
port="${HARNESS_CHECK_PORT:-3000}"
wait_sec="${HARNESS_CHECK_WAIT:-180}"

say() { printf '%s. %s: %s\n' "$1" "$2" "$3"; }
cap() {  # 초 명령… → 그 시간 안에 끝나게 (timeout 이 없으면 그냥 돌린다)
  local s="$1"; shift
  if command -v timeout >/dev/null 2>&1; then timeout "$s" "$@"; else "$@"; fi
}
last() { tail -n 1 "$1" 2>/dev/null | cut -c1-80; }  # 기록 끝 한 줄 (안 된 까닭)
find_py() {  # 전역 PY = 3.8 이상인 파이썬 (HARNESS_CHECK_PY, python3, python 차례). 없으면 1
  local c
  PY=""
  for c in "${HARNESS_CHECK_PY:-}" python3 python; do
    [ -n "$c" ] || continue
    command -v "$c" >/dev/null 2>&1 || continue
    if "$c" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' >/dev/null 2>&1; then PY="$(command -v "$c")"; return 0; fi
  done
  return 1
}
venv_py() { if [ -x "$1/bin/python" ]; then printf '%s' "$1/bin/python"; else printf '%s' "$1/Scripts/python.exe"; fi; }
web_py() {  # 전역 WPY = fastapi 와 uvicorn 을 들이는 파이썬, HOW = 어떻게 됐나. 있으면 그대로, 없으면 venv 에 깔고, 안 되면 pip --user
  local v
  WPY=""; HOW=""
  find_py || { HOW="파이썬 없음"; return 1; }
  if "$PY" -c 'import fastapi, uvicorn' >/dev/null 2>&1; then WPY="$PY"; HOW="이미 있음"; return 0; fi
  v="$(venv_py "$base/venv")"
  if [ -x "$v" ] && "$v" -c 'import fastapi, uvicorn' >/dev/null 2>&1; then WPY="$v"; HOW="venv"; return 0; fi
  if "$PY" -m venv "$base/venv" >"$base/pip.log" 2>&1; then
    v="$(venv_py "$base/venv")"
    if cap 300 "$v" -m pip install -q fastapi uvicorn >>"$base/pip.log" 2>&1 && "$v" -c 'import fastapi, uvicorn' >/dev/null 2>&1; then
      WPY="$v"; HOW="venv 에 설치"; return 0
    fi
  fi
  if cap 300 "$PY" -m pip install -q --user fastapi uvicorn >>"$base/pip.log" 2>&1 && "$PY" -c 'import fastapi, uvicorn' >/dev/null 2>&1; then
    WPY="$PY"; HOW="pip --user"; return 0
  fi
  HOW="$(last "$base/pip.log")"
  return 1
}

# 8: 뼈대 웹 앱을 띄우고 사람이 미리보기에서 로그인이 이어지는지 본다
if [ "${1:-}" = preview ]; then
  mkdir -p "$base" || exit 1
  if ! web_py; then say 8 "미리보기 로그인" "안 됨 (fastapi 를 들이지 못함: $HOW)"; exit 1; fi
  rm -rf "$base/app"; cp -r "$here/skeleton/webapp" "$base/app" || exit 1
  cd "$base/app" || exit 1
  echo "8. 미리보기 로그인: 뼈대 웹 앱을 $port 포트에 띄운다. 미리보기 칸에 포트 $port, 주소 / 를 넣는다"
  echo "   operator / operator 로 로그인 → 새로고침 → 로그인이 남는가, 단추를 눌러도 새 창이 뜨지 않는가. 둘 다 그러면 8 은 yes"
  echo "   끝내기: Ctrl+C"
  DB_PATH="$base/app/app.db" DATA_DIR="$base/app/data" "$WPY" -m uvicorn app:app --host 0.0.0.0 --port "$port"
  exit $?
fi

rm -rf "$base"; mkdir -p "$base" || { echo "임시 폴더를 만들지 못함: $base"; exit 1; }
# 임시 폴더 위의 git 저장소를 찾지 않게 한다 (setup.sh 가 이 안에 저장소를 따로 만든다)
ceil="$base"; if command -v cygpath >/dev/null 2>&1; then ceil="$(cygpath -m "$base")"; fi
export GIT_CEILING_DIRECTORIES="$ceil"
ver="$("$codex" --version 2>/dev/null | head -n 1)"
echo "환경 점검 (2 ~ 3분, 임시 폴더 $base. 참고: ${ver:-codex 판 모름})"

# 1 기본 도구: h 가 기대는 것 (bash 4 이상, git, find -printf, /proc, 파이썬). setsid, timeout, node 는 없어도 돈다
miss=""; opt=""
[ "${BASH_VERSINFO[0]:-0}" -ge 4 ] || miss="$miss bash4"
command -v git >/dev/null 2>&1 || miss="$miss git"
( cd "$base" && find . -maxdepth 0 -printf '%P' ) >/dev/null 2>&1 || miss="$miss find-printf"
[ -r "/proc/$$/stat" ] || miss="$miss /proc"
find_py || miss="$miss python3"
for t in setsid timeout node watch; do command -v "$t" >/dev/null 2>&1 || opt="$opt $t"; done  # watch 가 없으면 세션 보기는 while 줄로
if [ -z "$miss" ]; then say 1 "기본 도구" "ok${opt:+ (없지만 괜찮음:$opt)}"; else say 1 "기본 도구" "안 됨 (없음:$miss)"; fi

# 2 모델 지정: go 와 h 처럼 -m 과 생각 깊이를 붙여 codex exec 한 번. 안 되면 high, 그다음 기본 모델로 갈라 본다
ask() {  # 깊이 모델 → codex exec 한 번 (h 와 같은 꼴). 답이 오면 0
  local eff="$1" m="$2" a="$base/ask_$1.md" args=()
  if [ -n "$m" ]; then args=(-m "$m" -c "model_reasoning_effort=$eff"); fi
  rm -f "$a"
  cap 150 "$codex" exec --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check ${args[@]+"${args[@]}"} \
    -C "$base" -o "$a" "Reply with the single word: ok" </dev/null >"$base/ask_$1.log" 2>&1
  [ -s "$a" ]
}
mvar="$model"
if ! command -v "$codex" >/dev/null 2>&1; then say 2 "모델 지정" "안 됨 (codex 명령 없음)"; mvar=""
elif [ -z "$model" ]; then
  if ask default ""; then say 2 "모델 지정" "ok (Codex 기본 설정)"; else say 2 "모델 지정" "안 됨 (codex exec: $(last "$base/ask_default.log"))"; fi
elif ask xhigh "$model"; then say 2 "모델 지정 ($model, 생각 깊이 xhigh)" "ok"
elif ask high "$model"; then say 2 "모델 지정 ($model)" "안 됨: xhigh 만 안 됨 (high 는 됨)"
elif ask default ""; then say 2 "모델 지정 ($model)" "안 됨: 모델 이름이 안 통함 (기본 모델은 됨. codex 를 열어 /model 의 이름을 적어 온다)"; mvar=""
else say 2 "모델 지정 ($model)" "안 됨: codex exec 가 안 됨 ($(last "$base/ask_default.log"))"
fi

# 4 ~ 7 의 자리: 임시 폴더에 하네스를 깔고, 메인이 product 에 보내고 product 가 받은 뒤 팀원에게 보낸다 (팀원은 뒤에서 돈다)
ws="$base/ws"; mkdir -p "$ws"
body="$base/body.md"; mbody="$base/mbody.md"
printf '## 할 일\n점검\n\n## 낼 것\n점검\n\n## 끝난 조건\n점검\n' > "$body"
printf '## 할 일\n이 폴더에 ok.txt 를 만들고 ok 한 줄을 쓴다\n\n## 낼 것\nok.txt\n\n## 끝난 조건\nok.txt 에 ok 한 줄\n' > "$mbody"
export HARNESS_MODEL="$mvar" HARNESS_CODEX="$codex" HARNESS_REVIEW=1 HARNESS_MEMBER_AUTO=1 HARNESS_REVIEW_EFFORT=medium HARNESS_MEMBER_EFFORT=medium
unset HARNESS_ROLE HARNESS_REVIEW_SYNC HARNESS_MEMBER_SYNC 2>/dev/null || true
step=""
if ! ( cd "$ws" && HARNESS_PLATFORM=linux bash "$here/setup.sh" ) >"$base/setup.log" 2>&1 || [ ! -f "$ws/h" ]; then step="setup: $(last "$base/setup.log")"
elif ! ( cd "$ws" && bash ./h send product 점검 "$body" ) >"$base/send.log" 2>&1; then step="send: $(last "$base/send.log")"
elif ! ( cd "$ws/product" && bash ../h take 1 ) >"$base/take.log" 2>&1; then step="take: $(last "$base/take.log")"
fi
member=""
if [ -z "$step" ] && ( cd "$ws/product" && bash ../h send product_1 점검 "$mbody" ) >"$base/member_send.log" 2>&1; then member=1; fi

# 3 파이썬 패키지: 뼈대 웹 앱이 쓰는 fastapi 와 uvicorn (막히면 뼈대 README 의 표준 라이브러리 길)
if web_py; then say 3 "파이썬 패키지 (fastapi, uvicorn)" "ok ($HOW)"; else say 3 "파이썬 패키지 (fastapi, uvicorn)" "안 됨 ($HOW)"; fi

# 4 ~ 6: product 가 done 하면 시험 관문이 run_tests.sh 를 돌리고 자동 검토가 판정을 쓴다 (여기서는 끝날 때까지 기다린다)
if [ -z "$step" ]; then
  printf '#!/usr/bin/env bash\necho "Ran 1 test in 0.0s"\necho OK\n' > "$ws/product/run_tests.sh"
  ( cd "$ws/product" && printf '## 요약\n점검\n' | HARNESS_REVIEW_SYNC=1 bash ../h done 1 ) >"$base/done.log" 2>&1
  if grep -q '^전달문 1 상태: 처리완료' "$base/done.log"; then say 4 "h 깔기와 전달 (setup, send, take, done)" "ok"
  else say 4 "h 깔기와 전달 (setup, send, take, done)" "안 됨 (done: $(last "$base/done.log"))"; fi
  if grep -q '^시험: 통과 1/1' "$base/done.log"; then say 5 "시험 관문 (done 때 run_tests.sh)" "ok"
  else say 5 "시험 관문 (done 때 run_tests.sh)" "안 됨 ($(grep '^시험' "$base/done.log" | head -n 1))"; fi
  v=""; for f in "$ws/verify/delivery/main/out_/검토1_"*.md; do [ -e "$f" ] && v="$(grep -m 1 '^판정:' "$f")"; done
  case "$v" in
    '판정: 이상 없음'*|'판정: 이상 있음'*) v="${v#판정: }"; say 6 "자동 검토 (codex exec 로 판정)" "ok (판정이 나옴: ${v%%:*})";;
    '') say 6 "자동 검토 (codex exec 로 판정)" "안 됨 (판정 파일 없음: $(last "$base/done.log"))";;
    *) say 6 "자동 검토 (codex exec 로 판정)" "안 됨 (${v#판정: })";;
  esac
else
  for n in 4 5 6; do say "$n" "h 와 그 뒤" "안 됨 ($step)"; done
fi

# 7 팀원 자동: 팀장이 보낸 일을 팀원이 화면 없이 돌고 보고가 팀장에게 온다
if [ -n "$member" ]; then
  rep=""; k=0
  while [ "$k" -lt "$wait_sec" ]; do
    for f in "$ws/product_1/delivery/product/out_/보고"*.md; do [ -e "$f" ] && rep="$f"; done
    [ -n "$rep" ] && break
    sleep 2; k=$((k + 2))
  done
  case "$rep" in
    '') ( cd "$ws/product" && bash ../h stop product_1 ) >/dev/null 2>&1
        say 7 "팀원 자동 (화면 없이 돌고 보고가 옴)" "안 됨 (${wait_sec}초 안에 보고가 오지 않음: $(last "$ws/product_1/.h/실행2.log"))";;
    *_질문.md|*_질문_[0-9]*.md) say 7 "팀원 자동 (화면 없이 돌고 보고가 옴)" "ok (질문으로 끝남)";;
    *) say 7 "팀원 자동 (화면 없이 돌고 보고가 옴)" "ok";;
  esac
else
  say 7 "팀원 자동 (화면 없이 돌고 보고가 옴)" "안 됨 (${step:-팀원에게 보내기: $(last "$base/member_send.log")})"
fi
echo "끝. 번호마다 yes / no 로 적는다 (기록은 $base). 8 은 bash $0 preview 로 띄워 사람이 본다"
echo "세션 보기 미리 보기 (한글이 깨지지 않는지): cd $ws && watch -n 10 ./h status (끝내기 Ctrl+C)"
