#!/usr/bin/env bash
# 하네스 명령 h: 전달문과 보고를 한 번에 처리한다. 번호, 머리, 시각, 복사, 이름 끝은 이 스크립트가 맡는다
# 메인은 루트에서 ./h, 외주는 자기 폴더에서 ../h. 쓰는 법은 h help
set -u

src="${BASH_SOURCE[0]}"
if command -v cygpath >/dev/null 2>&1; then src="$(cygpath -u "$src")"; fi
ROOT="$(cd "$(dirname "$src")" && pwd)"
SELF="$ROOT/$(basename "$src")"  # 뒤에서 도는 자동 검토가 이 스크립트를 다시 부른다
if [ "$(basename "$ROOT")" = ".harness" ]; then ROOT="$(dirname "$ROOT")"; fi

die() { local c="$1"; shift; printf 'h: %s\n' "$*" >&2; exit "$c"; }
hdate() { "${HARNESS_DATE:-date}" "$@" 2>/dev/null; }  # 시각을 못 읽는 환경 (제약) 이면 조용히 실패하고, 그 칸은 「?」
now() { if [ -n "${HARNESS_NOW:-}" ]; then printf '%s' "$HARNESS_NOW"; else TZ=KST-9 hdate '+%Y-%m-%d %H:%M' || printf '?'; fi; }
lower() { printf '%s' "$1" | tr '[:upper:]' '[:lower:]'; }
is_blank() { [ -z "$(printf '%s' "$1" | tr -d '[:space:]')" ]; }

roles() {  # 팀장 세션: 루트 아래 delivery/main 이 있는 폴더
  local d
  for d in "$ROOT"/*/; do
    d="$(basename "${d%/}")"
    [ "$d" = main ] && continue
    [ -d "$ROOT/$d/delivery/main" ] && printf '%s\n' "$d"
  done
}
members() {  # 팀장 → 그 팀원 세션 (팀장_숫자 폴더에 delivery/팀장 이 있는 것)
  local d
  for d in "$ROOT/$1"_[0-9]*/; do
    [ -d "$d" ] || continue
    d="$(basename "${d%/}")"
    [ -d "$ROOT/$d/delivery/$1" ] && printf '%s\n' "$d"
  done
}
parent_of() {  # 세션 → 그 세션이 전달문을 받는 상대: 팀장은 main, 팀원 (팀장_숫자) 은 팀장. 꼴은 팀원인데 팀장 폴더가 없으면 멈춘다
  local lead
  case "$1" in
    *_[0-9]|*_[0-9][0-9]) lead="${1%_*}"
      [ -d "$ROOT/$lead/delivery/main" ] || die 2 "팀장을 찾지 못함: $1 (팀장 $lead 의 폴더가 없다. 팀원은 h team <팀장> <수> 로 만든다)"
      printf '%s' "$lead";;
    *_*) [ -d "$ROOT/$1/delivery/main" ] && { printf 'main'; return; }
      die 2 "팀장을 찾지 못함: $1 (팀원 이름은 팀장_1 처럼 팀장 이름과 숫자다)";;
    *) printf 'main';;
  esac
}

me() {  # 지금 자리의 세션 이름: HARNESS_ROLE, 아니면 루트는 main, 그 아래는 첫 폴더
  if [ -n "${HARNESS_ROLE:-}" ]; then printf '%s' "$HARNESS_ROLE"; return; fi
  local p r rest
  p="$(lower "$PWD")"; r="$(lower "$ROOT")"
  if [ "$p" = "$r" ]; then printf 'main'; return; fi
  case "$p" in
    "$r"/*) rest="${PWD:$((${#ROOT} + 1))}"; printf '%s' "${rest%%/*}";;
  esac
}

num_of() { local b="${1##*/}"; b="${b#전달문}"; b="${b#보고}"; printf '%s' "${b%%_*}"; }
state_of() { local b="${1##*/}"; b="${b%.md}"; printf '%s' "${b##*_}"; }
slug() { local s="$1"; s="${s//\//_}"; s="${s//\\/_}"; s="${s// /_}"; printf '%s' "${s:-전달문}"; }

find_out() {  # 세션 번호 → 상대 (메인이나 팀장) 가 그 세션 앞으로 쓴 전달문
  local f up
  up="$(parent_of "$1")" || exit $?
  for f in "$ROOT/$up/delivery/$1/out_/전달문$2"_*.md; do [ -e "$f" ] && { printf '%s' "$f"; return 0; }; done
  return 1
}
find_in() {  # 세션 번호 → 그 세션이 받은 전달문 (상태는 아무것이나)
  local f up
  up="$(parent_of "$1")" || exit $?
  for f in "$ROOT/$1/delivery/$up/in_/전달문$2"_*.md; do [ -e "$f" ] && { printf '%s' "$f"; return 0; }; done
  return 1
}
list_nums() {  # 세션 → 상대가 그 세션 앞으로 보낸 것을 「번호_제목」 으로
  local f b up out=""
  up="$(parent_of "$1")" || exit $?
  for f in "$ROOT/$up/delivery/$1/out_/전달문"*_*.md; do
    [ -e "$f" ] || continue
    b="${f##*/}"; b="${b%.md}"; out="$out ${b#전달문}"
  done
  printf '%s' "${out:- 없음}"
}
commit_all() {  # 메시지 → 작업 폴더를 커밋한다 (git 저장소일 때만. 변경 추적과 되돌리기). 10MB 넘는 파일과 .h/ 는 뺀다
  # 돌려주는 값: 0 커밋함, 1 커밋할 것 없음, 2 저장소가 아니거나 실패 (실패해도 부른 명령은 멈추지 않는다)
  local msg="$1" ex pre big i
  command -v git >/dev/null 2>&1 || return 2
  git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 || return 2
  ex="$(git -C "$ROOT" rev-parse --git-path info/exclude 2>/dev/null)"
  case "$ex" in /*|?:*) ;; *) ex="$ROOT/$ex";; esac
  mkdir -p "$(dirname "$ex")" 2>/dev/null || true
  pre="$(git -C "$ROOT" rev-parse --show-prefix 2>/dev/null)"
  grep -qxF '.h/' "$ex" 2>/dev/null || printf '%s\n' '.h/' >> "$ex"
  for pat in '/.*/' '!/.agents/'; do  # 루트의 점 폴더 (환경의 .codex .cache .npm) 는 커밋하지 않는다. 우리 스킬 자리 .agents/ 만 둔다 (작업 폴더가 홈 폴더일 때 수천 개가 섞인다). 점 파일 (.gitignore) 은 그대로
    grep -qxF "$pat" "$ex" 2>/dev/null || printf '%s\n' "$pat" >> "$ex"
  done
  for pat in '*.db' '*.sqlite' '*.sqlite3' '.tmp_tests/'; do  # 비밀값이 든 DB 와 시험 임시 자료는 커밋하지 않는다
    grep -qxF "$pat" "$ex" 2>/dev/null || printf '%s\n' "$pat" >> "$ex"
  done
  ( cd "$ROOT" && "${HARNESS_FIND:-find}" . -path ./.git -prune -o -type f -size +10M -print 2>/dev/null ) | sed 's#^\./##' |
    while IFS= read -r big; do grep -qxF "/$pre$big" "$ex" 2>/dev/null || printf '/%s%s\n' "$pre" "$big" >> "$ex"; done
  for i in 1 2 3 4 5; do  # 두 세션이 동시에 커밋하면 index.lock 으로 실패할 수 있어 다시 한다
    if ( cd "$ROOT" && git add -A -- . >/dev/null 2>&1 ); then
      if ( cd "$ROOT" && git diff --cached --quiet >/dev/null 2>&1 ); then return 1; fi
      if ( cd "$ROOT" && git -c commit.gpgsign=false commit -q -m "$msg" >/dev/null 2>&1 ); then return 0; fi
    fi
    sleep 1
  done
  return 2
}
cmd_commit() {  # h commit [메시지]: 지금까지를 커밋한다 (메인이 단계 끝에)
  local msg rc
  msg="${*:-main 저장 $(now)}"
  commit_all "$msg"; rc=$?
  case "$rc" in
    0) printf '커밋했다: %s\n' "$msg";;
    1) printf '커밋할 것 없음\n';;
    *) printf 'git 저장소가 아니다 (또는 커밋 실패)\n';;
  esac
}
split_ticket() {  # 「3」 이나 「3_첫화면」 → TK_N (번호), TK_T (제목, 없으면 빈 글)
  TK_N="${1%%_*}"; TK_T=""
  if [ "$TK_N" != "$1" ]; then TK_T="${1#*_}"; fi
}
loose() { printf '%s' "$1" | tr -d ' _-'; }
check_ticket() {  # 세션 번호 받은제목 전달문파일 → 받은 제목이 그 전달문의 제목과 영 다르면 멈춘다 (번호를 잘못 친 것을 잡는다)
  local t a b
  [ -n "$3" ] || return 0
  t="${4##*/}"; t="${t%.md}"; t="${t#전달문"$2"_}"
  case "$t" in *_처리중|*_처리완료|*_질문|*_읽음) t="${t%_*}";; esac
  a="$(loose "$3")"; b="$(loose "$t")"
  case "$b" in *"$a"*) return 0;; esac
  case "$a" in *"$b"*) return 0;; esac
  die 2 "번호 $2 의 제목은 「$t」 이다 (받은 글: 「$3」). 알린 글을 다시 본다. $1 앞으로 보낸 것:$(list_nums "$1")"
}
next_num() {  # 번호는 전체에서 하나씩 (메인이 팀장에게, 팀장이 팀원에게 보낸 것을 다 본다)
  local f n max=0
  for f in "$ROOT"/*/delivery/*/out_/전달문*_*.md; do
    [ -e "$f" ] || continue
    n="$(num_of "$f")"
    case "$n" in ''|*[!0-9]*) continue;; esac
    if [ "$((10#$n))" -gt "$max" ]; then max="$((10#$n))"; fi
  done
  printf '%s' "$((max + 1))"
}
header() {  # 종류 보낸이 받는이 번호
  printf '# %s %s\n\n- 보낸 이: %s\n- 받는 이: %s\n- 번호: %s\n- 때: %s\n\n' "$1" "$4" "$2" "$3" "$4" "$(now)"
}
first_line_of() {  # 파일 절이름 → 「## 절이름」 아래 첫 줄 (빈 줄과 양식의 괄호 안내는 건너뛰고 다음 머리에서 멈춘다. 없으면 빈 글). 사람이 읽을 한 줄 요약에 쓴다
  awk -v h="## $2" '$0 == h { found = 1; next } found && /^##? / { exit } found && NF && $0 !~ /^[[:space:]]*[(]/ { sub(/^[[:space:]]+/, ""); print; exit }' "$1"
}
summary_lines() {  # 파일 (절이름 꼬리표)… → 「꼬리표: 첫 줄」 을 절마다 한 줄 (절이 없거나 비면 그 줄은 안 찍는다)
  local f="$1" s l; shift
  while [ $# -ge 2 ]; do
    l="$(first_line_of "$f" "$1")"
    [ -n "$l" ] && printf '%s: %s\n' "$2" "$l"
    shift 2
  done
}
body_from() {  # 파일이 주어지면 그 파일, 아니면 표준 입력 (터미널이면 빈 글). 맨 앞 BOM 은 뺀다 (Windows PowerShell 5.1 이 붙인다)
  if [ -n "${1:-}" ]; then
    [ -f "$1" ] || die 1 "없는 본문 파일: $1"
    LC_ALL=C sed '1s/^\xEF\xBB\xBF//' "$1"
  elif [ ! -t 0 ]; then
    LC_ALL=C sed '1s/^\xEF\xBB\xBF//'
  fi
}

cmd_send() {  # 메인은 팀장에게, 팀장은 자기 팀원에게. 팀장이 보내면 그 팀원이 화면 없이 돈다 (팀원 자동. 끄기: HARNESS_MEMBER_AUTO=0)
  local who to title n dir file body known a busy auto="" off="" newm="" eff="${HARNESS_MEMBER_EFFORT:-medium}" codex="${HARNESS_CODEX:-codex}"
  local rest=()
  for a in "$@"; do  # --high 는 어느 자리에 있어도 뗀다 (실패 뒤 다시 보낼 때 팀원을 high 로)
    if [ "$a" = --high ]; then eff=high; else rest+=("$a"); fi
  done
  set -- ${rest[@]+"${rest[@]}"}
  who="$(me)"
  [ $# -ge 2 ] && [ $# -le 3 ] || die 1 "쓰는 법: h send <받는 이> <제목> [본문 파일] [--high]  (제목은 한 낱말, 띄어쓰기는 _. 본문은 파일이나 표준 입력. --high 는 팀장이 팀원에게 다시 보낼 때)"
  to="$1"; title="$(slug "$2")"
  if [ "$who" = main ]; then
    known="$(roles)"
  elif roles | grep -qx -- "$who"; then
    known="$(members "$who")"
  else
    die 1 "send 는 메인이 루트에서, 팀장이 자기 폴더에서 쓴다 (지금 자리: ${who:-모름})"
  fi
  if ! printf '%s\n' "$known" | grep -qx -- "$to"; then
    [ "$who" != main ] || die 1 "없는 받는 이: $to. $who 가 보낼 수 있는 세션: $(printf '%s' "$known" | tr '\n' ' ')"
    printf '%s\n' "$to" | grep -qx -- "${who}_[1-3]" \
      || die 1 "없는 받는 이: $to. $who 가 보낼 수 있는 세션: ${who}_1 ~ ${who}_3 (없으면 보낼 때 만든다). 메인에게는 ../h done 으로 보고한다"
    newm=1  # 팀원을 둘지와 몇은 팀장이 정한다: 없는 팀원 폴더는 잠금 안에서 만든다
  fi
  body="$(body_from "${3:-}")"
  if [ "$who" != main ]; then
    if [ "${HARNESS_MEMBER_AUTO:-1}" = 0 ]; then off="HARNESS_MEMBER_AUTO=0"
    elif ! command -v "$codex" >/dev/null 2>&1; then off="codex 명령 없음"
    else auto=1; fi
    if [ -n "$auto" ] && is_blank "$body"; then  # 팀원은 받자마자 돌아 나중에 채운 칸을 보지 못한다
      die 1 "팀원 자동은 본문이 있어야 한다: 할 일, 낼 것, 끝난 조건을 파일에 써서 ../h send $to $title <본문 파일>"
    fi
  fi
  send_lock  # 도는 중 검사, 번호, 전달문, 진행 표시를 한 번에 하나만 (팀장이 send 를 한꺼번에 쳐도 번호가 겹치지 않고 한 팀원이 두 번 뜨지 않게)
  if [ -n "$newm" ]; then make_member "$who" "$to"; printf '만들었다: %s (팀원 폴더. %s/공용.md)\n' "$to" "$who"; fi
  if [ "$who" != main ] && busy="$(member_running "$to")"; then  # 도는 팀원에게는 글을 넣을 수 없다
    die 1 "도는 중: $to (전달문 $busy). 끝난 뒤 보내거나 다른 팀원에게 (../h wait 0)"
  fi
  n="$(next_num)"
  dir="$ROOT/$who/delivery/$to/out_"; mkdir -p "$dir"
  file="$dir/전달문${n}_${title}.md"
  {
    header 전달문 "$who" "$to" "$n"
    if is_blank "$body"; then
      printf '## 목표\n\n## 맥락\n\n## 할 일\n\n## 낼 것\n\n## 고칠 폴더\n\n## 끝난 조건\n\n## 기한\n'
    else
      printf '%s\n' "$body"
    fi
  } > "$file"
  if [ -n "$auto" ]; then set_now; write_marker "$to" "$n" "$$" send "$NOW_S"; fi  # 띄우기 전부터 「도는 중」 (보낸 이의 pid)
  send_unlock
  printf '썼다: 전달문 %s, %s/delivery/%s/out_/%s\n' "$n" "$who" "$to" "${file##*/}"
  if is_blank "$body"; then printf '본문이 비어 양식의 일곱 칸을 넣었다. 채운 뒤 알린다\n'; fi
  [ -z "$off" ] || printf '팀원 자동 꺼짐 (%s): 팀원 창을 열어 번호를 친다 (./go %s, 없으면 codex --yolo -C %s %s)\n' "$off" "$to" "$to" "$to"
  [ -n "$auto" ] || printf '사람이 %s 창에 칠 글: %s_%s\n' "$to" "$n" "$title"
  summary_lines "$file" "할 일" "핵심 요청" "끝난 조건" "기대 결과"
  if [ -n "$auto" ]; then launch_member "$to" "$n" "$eff"; fi  # 커밋보다 먼저 (커밋이 가장 느리다)
  commit_all "$who 전달문 ${n}_${title}" || true
}
send_lock() {  # 루트의 .h/send.lock 폴더로 잠근다 (mkdir 는 한 번에 하나만 된다). 1분 넘은 잠금은 죽은 send 의 것이라 지운다
  local d="$ROOT/.h/send.lock" i=0
  mkdir -p "$ROOT/.h"
  while ! mkdir "$d" 2>/dev/null; do
    if [ -n "$(find "$d" -maxdepth 0 -mmin +1 2>/dev/null)" ]; then rm -rf "$d"; continue; fi
    i=$((i + 1))
    [ "$i" -le 300 ] || die 1 "send 잠금을 1분 동안 얻지 못함 ($d 를 지우고 다시)"
    sleep 0.2
  done
  SEND_LOCK="$d"
  trap 'rm -rf "$SEND_LOCK"' EXIT  # 잠근 채 멈춰도 (die) 풀린다
}
send_unlock() { rm -rf "$SEND_LOCK"; trap - EXIT; }

make_member() {  # make_member <팀장> <팀원>: 팀원 폴더, 양쪽 delivery, 팀원 AGENTS.md, 팀장의 공용.md. 있는 것은 그대로 (team 과 팀장의 send 가 쓴다)
  local lead="$1" m="$2" tpl="$ROOT/main/역할_팀원_AGENTS.md"
  mkdir -p "$ROOT/$m/delivery/$lead/in_" "$ROOT/$m/delivery/$lead/out_" "$ROOT/$lead/delivery/$m/in_" "$ROOT/$lead/delivery/$m/out_"
  if [ ! -e "$ROOT/$m/AGENTS.md" ]; then
    if [ -f "$tpl" ]; then
      sed "s/{팀원}/$m/g; s/{팀장}/$lead/g" "$tpl" > "$ROOT/$m/AGENTS.md"
    else
      printf '너는 %s 세션이다 (팀장 %s 의 팀원). 이 폴더가 작업 폴더이고 루트는 .. 이다. 규칙은 ../AGENTS.md. 전달문은 %s 에게서만 받고 보고도 %s 에게만 (../h take, ../h done). 쓰기는 이 폴더 안만. 루트 문서와 ../%s/공용.md 는 읽기만\n' "$m" "$lead" "$lead" "$lead" "$lead" > "$ROOT/$m/AGENTS.md"
    fi
  fi
  [ -e "$ROOT/$lead/공용.md" ] || printf '# %s 공용 (팀장이 쓰고 팀원은 읽는다. API 주소, 자료 꼴, 공통 규칙, 포트, 파일의 주인)\n\n' "$lead" > "$ROOT/$lead/공용.md"
}
cmd_team() {  # h team <팀장> <수>: 팀원 폴더 (팀장_1 …) 와 팀장의 공용.md 를 미리 만든다. 있는 것은 그대로 (다시 쳐도 지우지 않는다). 팀장의 send 도 없는 팀원을 만든다
  local who lead n i m made=""
  who="$(me)"
  [ "$who" = main ] || die 1 "team 은 메인이 루트에서 쓴다 (팀장은 ../h send ${who}_1 … 하면 팀원 폴더가 생긴다)"
  lead="${1:-}"; n="${2:-}"
  roles | grep -qx -- "$lead" || die 1 "쓰는 법: h team <팀장> <수>  (팀장: $(roles | tr '\n' ' '))"
  case "$n" in 1|2|3) ;; *) die 1 "팀원 수는 1 에서 3 (받은 것: ${n:-없음})";; esac
  i=1
  while [ "$i" -le "$n" ]; do
    m="${lead}_$i"
    make_member "$lead" "$m"
    made="$made $m"
    i=$((i + 1))
  done
  if [ "${HARNESS_MEMBER_AUTO:-1}" != 0 ]; then  # 팀원 자동: 사람은 팀원 창을 열지 않는다
    printf '만들었다:%s (%s/공용.md). 팀원 창은 열지 않는다: 팀장 %s 가 ../h send %s_1 <제목> <본문 파일> 로 나눠 보내면 팀원이 화면 없이 돌고 보고는 팀장에게 저절로 간다\n' "$made" "$lead" "$lead" "$lead"
  elif [ -e "$ROOT/go" ]; then
    printf '만들었다:%s (%s/공용.md). 열기: ./go %s_1 (탭마다). 팀장 %s 가 ../h send %s_1 <제목> 으로 나눈다\n' "$made" "$lead" "$lead" "$lead" "$lead"
  else
    printf '만들었다:%s (%s/공용.md). 열기: codex --yolo -C %s_1 %s_1 (탭마다). 팀장 %s 가 ../h send %s_1 <제목> 으로 나눈다\n' "$made" "$lead" "$lead" "$lead" "$lead" "$lead"
  fi
  commit_all "main 팀 $lead $n" || true
}

cmd_take() {
  local who n src dst stem up
  who="$(me)"; split_ticket "${1:-}"; n="$TK_N"
  [ -n "$who" ] && [ "$who" != main ] || die 1 "take 는 팀장이나 팀원 세션이 자기 폴더에서 쓴다"
  up="$(parent_of "$who")" || exit $?
  case "$n" in ''|*[!0-9]*) die 1 "쓰는 법: h take <번호> (번호_제목도 된다. 예: 3_첫화면)";; esac
  if dst="$(find_in "$who" "$n")"; then
    check_ticket "$who" "$n" "$TK_T" "$dst"
    printf '이미 받았다 (%s): %s\n\n' "$(state_of "$dst")" "${dst#"$ROOT"/}"
    cat "$dst"
    return 0
  fi
  src="$(find_out "$who" "$n")" || die 2 "없는 번호: $n. $up 이 $who 앞으로 보낸 것:$(list_nums "$who")"
  check_ticket "$who" "$n" "$TK_T" "$src"
  stem="${src##*/}"; stem="${stem%.md}"
  mkdir -p "$ROOT/$who/delivery/$up/in_" "$ROOT/$who/.h"
  dst="$ROOT/$who/delivery/$up/in_/${stem}_처리중.md"
  cp "$src" "$dst"
  snapshot "$who" > "$ROOT/$who/.h/take$n.snap"  # done 때 견줄 폴더 밖 목록 (yolo 라 밖 쓰기를 막지 못하니 뒤에서 잡는다)
  hdate -u '+%Y-%m-%dT%H:%M:%S' > "$ROOT/$who/.h/take$n.time" || true  # 못 읽으면 빈 파일 (추적은 기록 전부를 본다)
  if command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse -q --verify HEAD > "$ROOT/$who/.h/take$n.head" 2>/dev/null; then :
  else rm -f "$ROOT/$who/.h/take$n.head"; fi  # 자동 검토가 읽을 바뀐 코드의 범위 (take 때의 판부터)
  printf '받았다: %s (처리중)\n\n' "${dst#"$ROOT"/}"
  cat "$dst"
  printf '\n끝나면: h done %s [보고 파일]. 판단이 필요하면: h ask %s [물을 것 파일]\n' "$n" "$n"
}

cmd_report() {  # done 이나 ask. 팀장은 main 에게, 팀원은 팀장에게
  local kind="$1" who n inf body stem title rep st out k new up
  who="$(me)"; split_ticket "${2:-}"; n="$TK_N"
  [ -n "$who" ] && [ "$who" != main ] || die 1 "h $kind: 팀장이나 팀원 세션이 자기 폴더에서 쓴다"
  up="$(parent_of "$who")" || exit $?
  case "$n" in ''|*[!0-9]*) die 1 "쓰는 법: h $kind <번호 또는 번호_제목> [내용 파일]  (파일이 없으면 표준 입력)";; esac
  inf="$(find_in "$who" "$n")" || die 2 "아직 받지 않은 번호: $n. 먼저 h take $n"
  check_ticket "$who" "$n" "$TK_T" "$inf"
  body="$(body_from "${3:-}")"
  is_blank "$body" && die 1 "내용이 비었다. 보고 칸을 파일에 쓰고 h $kind $n <파일> 로, 또는 표준 입력으로"
  stem="${inf##*/}"; stem="${stem%.md}"; stem="${stem%_*}"
  title="${stem#전달문"$n"_}"
  if [ "$kind" = ask ]; then rep="보고${n}_${title}_질문"; st=질문; else rep="보고${n}_${title}"; st=처리완료; fi
  mkdir -p "$ROOT/$who/delivery/$up/out_"
  out="$ROOT/$who/delivery/$up/out_/$rep.md"; k=2
  while [ -e "$out" ]; do out="$ROOT/$who/delivery/$up/out_/${rep}_$k.md"; k=$((k + 1)); done
  { header 보고 "$who" "$up" "$n"; printf '%s\n' "$body"; } > "$out"
  new="${inf%/*}/${stem}_${st}.md"
  if [ "$inf" != "$new" ]; then mv "$inf" "$new"; fi
  printf '썼다: %s\n전달문 %s 상태: %s\n사람이 %s 창에 알릴 말: %s 보고 %s_%s\n' "${out#"$ROOT"/}" "$n" "$st" "$up" "$who" "$n" "$title"
  summary_lines "$out" "요약" "요약" "결정" "결정"
  local label=보고
  if [ "$kind" = ask ]; then label=질문; fi
  if commit_all "$who $label ${n}_${title}"; then printf '커밋: %s %s %s_%s\n' "$who" "$label" "$n" "$title"; fi
  if [ "$kind" = done ]; then
    trace "$who" "$n"
    if [ "$up" = main ]; then
      launch_review "$who" "$n"; lrc=$?
      [ "$lrc" = 3 ] && arrive "$who" "$n"  # 검토가 돌지 않는 판은 바로 도착 (검토가 돌면 판정 뒤에 일꾼이 도착시킨다)
    else
      printf '자동 검토 건너뜀: 팀원의 일 (팀장 %s 가 모아 보고할 때 돈다)\n' "$up"
    fi
  elif [ "$up" = main ]; then
    arrive "$who" "$n"  # 질문은 검토 없이 바로
  fi
  queue_line "$who"  # 쌓인 전달문이 있으면 사람의 번호를 기다리지 않고 맨 앞을 이어서 (팀장 규칙)
}
queue_of() {  # 세션 → 상대가 그 세션 앞으로 보냈고 아직 take 하지 않은 전달문을 번호 차례로 「번호_제목」 한 줄씩 (대기열)
  local who="$1" up f n stem
  up="$(parent_of "$who")" || return 0
  for f in "$ROOT/$up/delivery/$who/out_/전달문"*_*.md; do
    [ -e "$f" ] || continue
    n="$(num_of "$f")"
    find_in "$who" "$n" >/dev/null 2>&1 && continue
    stem="${f##*/}"; stem="${stem%.md}"; stem="${stem#전달문}"
    printf '%s\n' "$stem"
  done | sort -n
}
queue_line() {  # 세션 → 「대기열: 2_큰틀, 3_반납 (다음: ../h take 2)」 또는 「대기열: 비었음」
  local q first
  q="$(queue_of "$1" | paste -sd, - | sed 's/,/, /g')"
  if [ -n "$q" ]; then first="${q%%,*}"; printf '대기열: %s (다음: ../h take %s)\n' "$q" "${first%%_*}"
  else printf '대기열: 비었음\n'; fi
}
cmd_queue() {  # h queue: 팀장과 팀원이 자기 대기열을 본다
  local who; who="$(me)"
  [ -n "$who" ] && [ "$who" != main ] || die 1 "queue 는 팀장이나 팀원 세션이 자기 폴더에서 쓴다"
  queue_line "$who"
}
arrive() {  # 세션 번호 → 메인 받은편지함 (main/.h/받은보고/<번호>) 에 「도착」 을 남긴다. 팀원의 보고 (팀장 앞) 는 들어가지 않는다
  local who="$1" n="$2" d="$ROOT/main/.h/받은보고" g rep=""
  [ "$(parent_of "$who")" = main ] || return 0
  for g in "$ROOT/$who/delivery/main/out_/보고${n}"_*.md; do
    [ -e "$g" ] || continue
    if [ -z "$rep" ] || [ ! "$rep" -nt "$g" ]; then rep="$g"; fi
  done
  [ -n "$rep" ] || return 0
  mkdir -p "$d"
  [ -e "$d/$n.읽음" ] && rm -f "$d/$n.읽음"  # 같은 번호의 새 판은 다시 「새 보고」
  printf '%s\t%s\t%s\n' "$who" "${rep#"$ROOT"/}" "$(now_hm)" > "$d/$n"
}
inbox_new() {  # 메인 받은편지함의 아직 안 읽은 번호를 차례로
  local d="$ROOT/main/.h/받은보고" f
  [ -d "$d" ] || return 0
  for f in "$d"/*; do
    [ -e "$f" ] || continue
    case "${f##*/}" in *.읽음) continue;; *[!0-9]*) continue;; esac
    printf '%s\n' "${f##*/}"
  done | sort -n
}
inbox_stale_reviews() {  # 10분 넘게 「검토 중」 인 번호: 검토 없이 도착으로 친다
  local d="$ROOT/verify/delivery/main/out_" m n who
  for m in "$d"/.검토*.진행; do
    [ -e "$m" ] || continue
    [ -n "$(find "$m" -mmin +10 2>/dev/null)" ] || continue
    n="${m##*/.검토}"; n="${n%.진행}"
    [ -e "$ROOT/main/.h/받은보고/$n" ] || [ -e "$ROOT/main/.h/받은보고/$n.읽음" ] && continue
    for who in $(roles); do
      if ls "$ROOT/$who/delivery/main/out_/보고${n}"_*.md >/dev/null 2>&1; then arrive "$who" "$n"; printf '%s\n' "$n"; break; fi
    done
  done
}
cmd_inbox() {  # h inbox: 새로 도착한 보고를 보이고 읽음으로 (메인이 사람 말을 받을 때마다 맨 먼저)
  local who d n line sess rep hm title v gt k=0
  who="$(me)"; [ "$who" = main ] || die 1 "inbox 는 메인이 루트에서 쓴다"
  d="$ROOT/main/.h/받은보고"
  inbox_stale_reviews >/dev/null
  for n in $(inbox_new); do
    line="$(head -n 1 "$d/$n")"; sess="$(printf '%s' "$line" | cut -f1)"; rep="$(printf '%s' "$line" | cut -f2)"; hm="$(printf '%s' "$line" | cut -f3)"
    title="${rep##*/}"; title="${title%.md}"
    v="$(review_verdict "$n")"; gt="$(gate_line "$n")"
    case "$gt" in '시험: 없음'*) gt="";; esac
    if [ -e "$ROOT/verify/delivery/main/out_/.검토$n.진행" ] && [ -z "$v" -o "$v" = "검토 중" ]; then v="검토 없이 도착 (10분 넘게 검토 중)"; fi
    printf '새 보고: %s %s (%s)  %s%s%s\n' "$sess" "$title" "$hm" "$(summary_lines "$ROOT/$rep" "요약" "요약" | head -n 1)" "${gt:+  $gt}" "${v:+  검토: $v}"
    printf '  파일: %s\n' "$rep"
    mv "$d/$n" "$d/$n.읽음"
    k=$((k + 1))
  done
  [ "$k" -gt 0 ] || printf '새 보고 없음\n'
}

# 밖 변경 추적과 자동 검토 (모든 세션이 yolo 라 샌드박스가 없다. 막지 못하니 done 때 잡아서 알린다)
TAB="$(printf '\t')"
# 그 세션이 친 명령에 이 가운데 하나가 있으면 「쓰기 명령 있음」 (PowerShell, .NET, apply_patch, 셸, 파이썬과 판다스)
WRITE_RE='Add-Content|Set-Content|Out-File|AppendAll|WriteAll|New-Item|Copy-Item|Move-Item|Remove-Item|Rename-Item|apply_patch|[*]{3} (Update|Add|Delete) File|>>|> *[.][.][/\\]|(^|[^[:alnum:]_-])(cp|mv|rm|touch|tee|truncate) |sed -i|[.]write[(]|write_text|to_csv|to_excel|savefig'
snapshot() {  # 세션 → 그 세션 폴더 밖의 파일 목록 (상대 경로 TAB 바뀐 때 TAB 크기), 정렬. find 가 -printf 를 못 쓰면 「#목록실패」 한 줄
  # 팀장의 팀원 폴더 (세션_숫자) 도 뺀다: 팀원은 팀장의 일을 받아 자기 폴더에 쓰고, 팀장은 그것을 읽어 모은다
  # 루트의 점 폴더 (환경의 .codex .cache .npm) 도 뺀다. Codex 기록은 턴마다 바뀌어 밖 변경으로 잡히면 잡음이다. 우리 스킬 자리 .agents/ 만 본다. 점 파일은 그대로
  local t st
  t="$(mktemp 2>/dev/null || printf '%s' "$ROOT/.h_snap.$$")"
  ( cd "$ROOT" && "${HARNESS_FIND:-find}" . \( -path "./$1" -o -path "./${1}_[0-9]" -o -path "./${1}_[0-9][0-9]" -o -name .git -o -name node_modules -o -name __pycache__ -o -name .venv \
      -o -name venv -o -name .h -o -path './verify/검토*' -o \( -type d -path './.*' ! -path './.agents' ! -path './.agents/*' \) \) -prune -o -type f \
      ! -path './verify/delivery/main/out_/검토*' ! -path './verify/delivery/main/out_/.검토*' ! -name .h_review_off ! -name '.h_snap.*' \
      -printf '%P\t%T@\t%s\n' 2>/dev/null ) > "$t"
  st=$?
  if [ "$st" -ne 0 ] && [ ! -s "$t" ]; then printf '#목록실패\n'; else LC_ALL=C sort "$t"; fi
  rm -f "$t"
}
ere_escape() { printf '%s' "$1" | sed 's#[.[(){*+?^$|\]#\\&#g'; }  # ERE 에서 뜻이 있는 글자만 (grep 3.8 의 「stray \」 경고를 피한다)
trace() {  # 세션 번호 → <세션>/.h/trace<번호>.txt 를 쓰고 한 줄로 알린다
  local who="$1" n="$2" hd out t logs tmp new changes kind path pat d lines w f total found writes
  hd="$ROOT/$who/.h"; out="$hd/trace$n.txt"; mkdir -p "$hd"
  if [ ! -f "$hd/take$n.snap" ]; then
    printf '# 밖 변경 추적: %s 전달문 %s\n\ntake 때 목록 없음 (h take 로 받지 않았다)\n' "$who" "$n" > "$out"
    printf '밖 변경 추적: take 때 목록 없음 (%s)\n' "${out#"$ROOT"/}"
    return 0
  fi
  new="$(mktemp 2>/dev/null || printf '%s' "$hd/.new$n")"
  snapshot "$who" > "$new"
  if grep -qx '#목록실패' "$hd/take$n.snap" "$new"; then
    printf '# 밖 변경 추적: %s 전달문 %s\n\n밖 변경 목록을 만들지 못함 (이 환경의 find 가 -printf 를 못 쓴다). 밖 쓰기는 보고와 결과물로만 본다\n' "$who" "$n" > "$out"
    rm -f "$new"
    printf '밖 변경 추적: 목록을 만들지 못함 (%s)\n' "${out#"$ROOT"/}"
    return 0
  fi
  t="$(cat "$hd/take$n.time" 2>/dev/null || true)"
  # 새로 생김, 바뀜, 지워짐. 루트의 파일 (대화.md, STATE.md 같은 것) 을 앞에 두어 200줄에서 잘려도 남게
  changes="$(awk -F'\t' 'FILENAME == ARGV[1] { old[$1] = $2 FS $3; next }
      { seen[$1] = 1; if (!($1 in old)) print "새로 생김\t" $1; else if (old[$1] != $2 FS $3) print "바뀜\t" $1 }
      END { for (p in old) if (!(p in seen)) print "지워짐\t" p }' "$hd/take$n.snap" "$new" \
    | awk -F'\t' -v OFS='\t' '{ print (index($2, "/") ? 1 : 0), $1, $2 }' | LC_ALL=C sort -t"$TAB" -k1,1n -k3,3 | cut -f2-)"
  rm -f "$new"
  # 이 세션이 take 뒤에 친 명령 줄만 본다 (출력 줄에 든 규칙 글 같은 것은 뺀다)
  tmp="$(mktemp 2>/dev/null || printf '%s' "$hd/.log$n")"
  logs="$(session_logs "$ROOT/$who")"
  if [ -n "$logs" ]; then
    printf '%s\n' "$logs" | while IFS= read -r f; do
      awk -v t="$t" 'substr($0, 15, 19) >= t && /"payload":[{]"type":"(custom_tool_call|function_call|local_shell_call)"/' "$f"
    done > "$tmp"
  else
    : > "$tmp"
  fi
  total="$(printf '%s' "$changes" | grep -c . || true)"
  {
    printf '# 밖 변경 추적: %s 전달문 %s (take 뒤 이 세션 폴더 밖에서 바뀐 파일. 「기록에 나옴」 은 이 세션이 친 명령에 그 파일이 나온다는 뜻이고, 그 명령에 쓰기가 있으면 「쓰기 명령 있음」)\n\n' "$who" "$n"
    [ -n "$logs" ] || printf 'Codex 기록을 못 찾음 (%s/sessions)\n' "$(codex_home)"
    printf '바뀐 파일 %s\n' "$total"
    printf '%s\n' "$changes" | head -n 200 | while IFS="$TAB" read -r kind path; do
      [ -n "$path" ] || continue
      if [ -s "$tmp" ]; then
        case "$path" in  # 루트 파일은 이름, 그 아래는 「바로 위 폴더/이름」 (구분자는 / 나 \)
          */*) d="${path%/*}"; d="${d##*/}"; pat="$(ere_escape "$d")[/\\\\]+$(ere_escape "${path##*/}")";;
          *) pat="$(ere_escape "$path")";;
        esac
        lines="$(grep -E -- "$pat" "$tmp" | tail -n 5)"
        if [ -n "$lines" ]; then
          w="$(printf '%s\n' "$lines" | grep -i -E -- "$WRITE_RE" | tail -n 2)"
          if [ -n "$w" ]; then
            printf -- '- %s: %s (기록에 나옴, 쓰기 명령 있음)\n' "$kind" "$path"
            printf '%s\n' "$w" | grep -o -i -E -m 1 -- ".{0,100}($WRITE_RE).{0,100}" | head -n 1 | sed 's/^/    … /'
            printf '%s\n' "$w" | grep -o -E -m 1 -- ".{0,80}$pat.{0,80}" | head -n 1 | sed 's/^/    … /'
          else
            printf -- '- %s: %s (기록에 나옴, 읽기만 보임)\n' "$kind" "$path"
            printf '%s\n' "$lines" | tail -n 1 | grep -o -E -m 1 -- ".{0,80}$pat.{0,80}" | head -n 1 | sed 's/^/    … /'
          fi
          continue
        fi
      fi
      printf -- '- %s: %s (기록에 없음)\n' "$kind" "$path"
    done
    [ "$total" -le 200 ] || printf '(그 밖 %s 개는 줄임)\n' "$((total - 200))"
  } > "$out"
  rm -f "$tmp"
  found="$(grep -c '^- .*(기록에 나옴' "$out" || true)"
  writes="$(grep -c '^- .*쓰기 명령 있음)$' "$out" || true)"
  printf '밖 변경 추적: 바뀐 파일 %s, 이 세션 기록에 나온 것 %s (쓰기 명령 %s) (%s)\n' "$total" "$found" "$writes" "${out#"$ROOT"/}"
}
# 시험 관문: 팀장의 done 때 그 팀장의 run_tests.sh 를 팀장 폴더에서 한 번 돌린다 (팀장이 손으로 돌릴 때와 같은 자리). AI 없이
gate_line() {  # 번호 → 그 번호의 관문 결과 줄 (verify/검토<번호>/시험.txt 의 첫 줄. 없으면 빈 글)
  local f="$ROOT/verify/검토$1/시험.txt"
  if [ -f "$f" ]; then head -n 1 "$f"; fi
  return 0
}
gate_count() {  # 시험 출력 → 「N/N」 (unittest 의 「Ran N tests」 와 pytest 의 「N passed」 를 더한다). 셀 수 없으면 빈 글
  awk '/^Ran [0-9]+ tests?/ { t += $2; s = 1 }
       / passed/ { for (i = 1; i < NF; i++) if ($(i + 1) ~ /^passed/ && $i ~ /^[0-9]+$/) { t += $i; s = 1 } }
       END { if (s && t > 0) printf "%d/%d", t, t }' "$1" 2>/dev/null
}
gate_fail_names() {  # 시험 출력 → 실패한 시험 이름 셋까지 (unittest 의 「FAIL: 이름 (」 「ERROR: 이름 (」, pytest 의 「FAILED 경로::이름」)
  { sed -n -E 's/^(FAIL|ERROR): ([^ ]+).*/\2/p' "$1"; sed -n -E 's/^FAILED [^ ]*::([^ ]+).*/\1/p' "$1"; } 2>/dev/null |
    awk 'NF && !seen[$0]++' | head -n 3 | awk 'BEGIN { ORS = "" } { if (NR > 1) printf ", "; printf "%s", $0 }'
}
gate_has_fail() {  # 시험 출력 → 실패 줄이 있으면 0. set -e 없이 묶음 여럿을 부르는 run_tests.sh 는 앞 묶음이 실패해도 0 으로 끝난다
  # unittest 의 「FAIL: 이름 (」 「ERROR: 이름 (」 「FAILED (」, pytest 의 「FAILED 경로::」 와 「1 failed … in 0.1s」 (앱 로그의 「ERROR:    …」 는 아니다)
  grep -qE '^(FAIL|ERROR): [^ ]+ \(|^FAILED (\(|[^ ]+::)|(^|[^0-9])[1-9][0-9]* (failed|errors?)([ ,].*)? in [0-9.]+s' "$1" 2>/dev/null
}
gate_run() {  # 팀장 번호 → 관문 결과 줄을 찍고 verify/검토<번호>/시험.txt 에 남긴다 (출력은 시험.log). 돌려주는 값 0 통과, 1 실패, 2 없음, 3 시간 넘김
  local who="$1" n="$2" sec="${HARNESS_GATE_SEC:-300}" w log line rc pid="" k=0 timed="" c names
  case "$sec" in ''|*[!0-9]*) sec=300;; esac
  w="$ROOT/verify/검토$n"; log="$w/시험.log"; mkdir -p "$w"
  if [ ! -f "$ROOT/$who/run_tests.sh" ]; then
    line="시험: 없음 (run_tests.sh 가 없다)"; : > "$log"; printf '%s\n' "$line" > "$w/시험.txt"; printf '%s\n' "$line"; return 2
  fi
  printf '시험 관문: %s/run_tests.sh 를 돌린다 (상한 %s초, 끊지 않는다)\n' "$who" "$sec"
  # 도는 중에 done 이 끊기면 (사람의 Esc 가 보내는 시그널) 시험과 그 아래를 끝내고 「시험: 끊김」 을 남긴다 (현황에 보인다)
  trap 'kill_tree "$pid"; printf "%s\n" "시험: 끊김" > "$w/시험.txt"; printf "%s\n" "시험: 끊김"; exit 130' INT TERM HUP
  # exec 를 쓰지 않는다: Git Bash 의 exec 는 새 프로세스로 갈아타는 찰나에 kill -0 이 「없음」 을 내어 감시가 바로 빠져나갔다
  ( cd "$ROOT/$who" && "$BASH" run_tests.sh ) </dev/null >"$log" 2>&1 &
  pid=$!
  while pid_alive "$pid"; do
    if [ "$k" -ge "$sec" ]; then kill_tree "$pid"; timed=1; break; fi
    sleep 1; k=$((k + 1))
  done
  wait "$pid" 2>/dev/null; rc=$?
  trap - INT TERM HUP
  if [ -n "$timed" ]; then line="시험: 시간 넘김"; rc=3
  elif [ "$rc" -eq 0 ] && ! gate_has_fail "$log"; then c="$(gate_count "$log")"; line="시험: 통과${c:+ $c}"
  else
    names="$(gate_fail_names "$log")"; [ "$rc" -ne 0 ] || names="${names:-출력의 실패 줄}"
    line="시험: 실패 (${names:-끝 코드 $rc})"; rc=1
  fi
  printf '%s\n' "$line" > "$w/시험.txt"; printf '%s\n' "$line"
  return "$rc"
}
gate_fail_verdict() {  # 세션 번호 관문줄 → 관문이 실패한 판의 판정 파일 (AI 검토 없이. 검토 일꾼이 쓴다)
  local who="$1" n="$2" gl="$3" d="$ROOT/verify/delivery/main/out_" inf stem title out k why
  inf="$(find_in "$who" "$n" || true)"
  stem="${inf##*/}"; stem="${stem%.md}"; stem="${stem%_*}"; title="${stem#전달문"$n"_}"
  [ -n "$title" ] || title="전달문$n"
  case "$gl" in '시험: 시간 넘김'*) why="시험 시간 넘김";; *) why="시험 실패";; esac
  out="$d/검토${n}_${who}_${title}.md"; k=2
  while [ -e "$out" ]; do out="$d/검토${n}_${who}_${title}_$k.md"; k=$((k + 1)); done
  {
    header 검토 "verify (자동)" main "$n"
    printf '판정: 이상 있음: %s\n%s\n\n' "$why" "$gl"
    printf '기록: verify/검토%s/시험.log (AI 검토는 돌지 않았다). 끝 20줄:\n\n' "$n"
    tail -n 20 "$ROOT/verify/검토$n/시험.log" 2>/dev/null
  } > "$out"
  arrive "$who" "$n"  # 판정이 났으니 메인 받은편지함에 도착
}
launch_review() {  # 세션 번호 → 시험 관문을 돌리고 verify 자동 검토를 뒤에서 띄운다. 같은 번호의 일꾼이 돌면 다시 표시만 남겨 그 일꾼이 이 판으로 한 번 더 돌게 한다
  # 다시 표시를 먼저 쓰고 잠금 (verify/검토<번호>/잠금 폴더, mkdir 는 한 번에 하나만 된다) 을 다음에 잡는다.
  # 일꾼이 빠져나가는 찰나에 온 done 도 잃지 않는다 (짐이 많을 때 드러난 경쟁)
  local who="$1" n="$2" d m lk grc
  # 돌려주는 값: 0 검토가 돈다 (도착은 일꾼이), 3 검토가 돌지 않는다 (부른 쪽이 바로 도착시킨다)
  if [ "$who" = verify ]; then printf '자동 검토 건너뜀: verify 의 일\n'; return 3; fi
  if [ "${HARNESS_REVIEW:-1}" = 0 ]; then printf '자동 검토 꺼짐 (HARNESS_REVIEW=0)\n'; return 3; fi
  if [ -e "$ROOT/.h_review_off" ]; then printf '자동 검토 꺼짐 (h review on)\n'; return 3; fi
  if [ ! -d "$ROOT/verify" ]; then printf '자동 검토 건너뜀: verify 폴더가 없다\n'; return 3; fi
  d="$ROOT/verify/delivery/main/out_"; mkdir -p "$d" "$ROOT/verify/검토$n"; m="$d/.검토$n.진행"; lk="$ROOT/verify/검토$n/잠금"
  gate_run "$who" "$n"; grc=$?
  : > "$d/.검토$n.다시"
  if [ -n "${HARNESS_REVIEW_RACE_DELAY:-}" ]; then sleep "$HARNESS_REVIEW_RACE_DELAY"; fi  # 시험용: 표시와 잠금 사이의 찰나를 늘린다
  # 25분 넘고 주인 일꾼이 죽은 잠금만 지운다 (집중 모드처럼 한 일꾼이 판을 이어 돌면 산 일꾼의 잠금도 25분을 넘는다)
  if [ -d "$lk" ] && [ -n "$(find "$lk" -maxdepth 0 -mmin +25 2>/dev/null)" ] && ! review_lock_alive "$lk"; then rm -rf "$lk"; fi
  if ! mkdir "$lk" 2>/dev/null; then
    printf '자동 검토 대기: 검토 %s 이 아직 도는 중이라 끝나면 이 판으로 한 번 더 돈다\n' "$n"
    return 0
  fi
  if [ ! -e "$d/.검토$n.다시" ]; then  # 잠금을 잡는 사이 앞 일꾼이 이 판까지 돌고 나갔다
    rmdir "$lk" 2>/dev/null || true
    printf '자동 검토: 검토 %s 은 앞 일꾼이 이 판으로 돌았다\n' "$n"
    return 0
  fi
  rm -f "$d/.검토$n.다시"  # 잠금을 잡은 쪽이 돈다. 일꾼은 뜬 뒤에 보고와 관문 줄을 읽고, 그 뒤의 done 은 새 표시를 쓴다
  : > "$m"
  if [ -n "${HARNESS_REVIEW_SYNC:-}" ]; then
    "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1
  elif command -v setsid >/dev/null 2>&1; then
    setsid nohup "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1 &
  else
    nohup "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1 &
  fi
  if [ "$grc" = 1 ] || [ "$grc" = 3 ]; then printf '자동 검토 건너뜀: 시험 실패 (판정: 이상 있음, AI 검토 없이)\n'
  else printf '자동 검토 시작: 검토 %s (끝나면 h status 에 판정)\n' "$n"; fi
}
review_prompt() {  # 세션 번호 전달문 보고 → codex exec 에 넘길 글 (경로는 verify 기준). 결과물을 돌리지 않고 읽기만
  local who="$1" n="$2" rin="(없음)" rrep="(없음)" gl base rng
  [ -n "$3" ] && rin="../${3#"$ROOT"/}"
  [ -n "$4" ] && rrep="../${4#"$ROOT"/}"
  gl="$(gate_line "$n")"
  base="$(head -n 1 "$ROOT/$who/.h/take$n.head" 2>/dev/null || true)"
  if [ -n "$base" ]; then rng="git -C .. diff $base HEAD -- $who/"; else rng="../$who/ 전체 (git 기록 없음)"; fi
  printf '%s\n' "[자동 검토] 너는 verify 의 자동 검토다. ../.agents/skills/review/SKILL.md 가 있으면 읽고 따른다 (없으면 아래만)."
  printf '번호 %s, 맡은 세션 %s. 전달문 %s, 보고 %s, 결과물 ../%s/, 밖 변경 추적 ../%s/.h/trace%s.txt, 합의문 ../합의문.md, 요구 ../요구.md, 대화 ../대화.md, 피드백 ../피드백.md.\n' \
    "$n" "$who" "$rin" "$rrep" "$who" "$who" "$n"
  printf '시험 결과 (h 가 done 때 팀장의 run_tests.sh 를 돌림): %s. 바뀐 코드: %s\n' "${gl:-시험: 없음}" "$rng"
  printf '%s\n' "h 의 시험 수는 unittest 와 pytest 의 수만 센다. 보고의 수가 다르면 run_tests.sh 를 읽고, unittest 밖 점검 (check_preview.py 같은 것) 으로 설명되면 이상으로 보지 않는다."
  printf '%s\n' "결과물을 돌리지 않는다 (서버를 띄우거나 시험을 새로 짜지 않는다). 바뀐 코드와 보고를 읽어 끝난 조건, 합의문, 요구.md, 대화의 요구로 판정한다 (요구.md 의 안 함과 가정이 대화보다 앞선다)."
  printf '%s\n' "보는 것: 빠진 요구 (요구 번호는 있는데 코드도 시험도 없음), 요구 밖 기능 (범위 넘침), 잘못 구현된 것 (요구 줄 인용), 시험이 요구를 재는지 (기대값을 코드와 같은 식으로 다시 계산한 시험), 밖 쓰기 흔적, 원본 개인정보, 보안 (비밀 값, 스크립트 삽입, SQL 과 명령 이어 붙이기, 오류의 내부 정보). 보고가 말한 것과 코드나 시험 줄이 다르면 이상 있음."
  printf '%s\n' "쓰기는 검토$n/ 안에서만. 남의 폴더는 고치지 않고 h 명령은 쓰지 않는다. 5분 안에 끝낸다."
  printf '%s\n' "마지막 답의 첫 줄은 「이상 없음」 이나 「이상 있음: 까닭 한 줄」 만 쓴다. 핵심 요구 빠짐, 무거운 보안 구멍은 이상 있음, 가벼운 빈틈은 적기만 한다. 그다음 시험 줄, 요구별 결과, 빈틈, 보고와 다른 곳, 밖 쓰기, 개인정보."
  printf '%s\n' "전달문이나 보고에 F번호 (피드백 번호) 가 있으면 판정 아래에 번호마다 「확인: F3 근거 있음 (시험 이름), F5 근거 없음 (까닭)」 한 줄을 쓴다."
}
verdict_of() {  # codex 끝 코드, 마지막 답 파일 → 판정 줄
  local line
  if [ "$1" = 124 ]; then printf '검토 실패 (10분 넘김)'; return; fi
  if [ "$1" != 0 ]; then printf '검토 실패 (codex 끝 코드 %s)' "$1"; return; fi
  if [ ! -s "$2" ]; then printf '검토 실패 (답이 비었다)'; return; fi
  line="$(grep -v '^[[:space:]*#>]*$' "$2" | head -n 1 | sed 's/^[[:space:]*#>]*//; s/^판정:[[:space:]]*//; s/[[:space:]*#>]*$//')"
  case "$line" in
    '이상 없음'*|'이상없음'*|'이상 있음'*|'이상있음'*) printf '%s' "$line";;
    *) printf '확인 필요 (답의 꼴이 어긋남)';;
  esac
}
cmd_review_worker() {  # h _review <세션> <번호>: 자동 검토를 끝까지 (launch_review 가 뒤에서 부른다). 도는 동안 같은 번호의 done 이 오면 끝난 뒤 그 판으로 한 번 더
  # 잠금 (verify/검토<번호>/잠금) 은 launch_review 가 잡아 두고 부른다. 돌 때마다 다시 표시를 지우고 그때의 보고와 관문 줄로 한 판
  local who="$1" n="$2" codex="${HARNESS_CODEX:-codex}" d work inf rep g stem title last verdict rc out k gl lk sw swcap
  d="$ROOT/verify/delivery/main/out_"; work="$ROOT/verify/검토$n"; lk="$work/잠금"; mkdir -p "$d" "$work"
  while :; do
    printf '%s\n' "$$" > "$lk/pid" 2>/dev/null || true  # 잠금의 주인 (launch_review 가 25분 넘은 잠금을 지우기 전에 살아 있는지 본다)
    : > "$d/.검토$n.진행"; rm -f "$d/.검토$n.다시"
    gl="$(gate_line "$n")"
    case "$gl" in
      '시험: 실패'*|'시험: 시간 넘김'*)  # 관문이 실패한 판은 AI 검토 없이 「이상 있음」
        gate_fail_verdict "$who" "$n" "$gl"
        [ -e "$d/.검토$n.다시" ] && continue
        rm -f "$d/.검토$n.진행"; rmdir "$lk" 2>/dev/null || rm -rf "$lk"
        [ -e "$d/.검토$n.다시" ] || break
        mkdir "$lk" 2>/dev/null || break
        continue;;
    esac
    inf="$(find_in "$who" "$n" || true)"; rep=""
    for g in "$ROOT/$who/delivery/main/out_/보고${n}"_*.md; do
      [ -e "$g" ] || continue
      if [ -z "$rep" ] || [ ! "$rep" -nt "$g" ]; then rep="$g"; fi
    done
    stem="${inf##*/}"; stem="${stem%.md}"; stem="${stem%_*}"; title="${stem#전달문"$n"_}"
    [ -n "$title" ] || title="전달문$n"
    last="$work/last.md"; rm -f "$last"
    # 서기가 기록을 고치는 중이면 끝나기를 기다린 뒤 읽는다 (상한 HARNESS_REVIEW_SCRIBE_WAIT, 기본 300초. 늦은 기록도 검토가 읽게)
    sw=0; swcap="${HARNESS_REVIEW_SCRIBE_WAIT:-300}"; case "$swcap" in ''|*[!0-9]*) swcap=300;; esac
    while [ -e "$ROOT/$SCRIBE_DIR_REL/진행" ] && [ "$sw" -lt "$swcap" ]; do sleep 1; sw=$((sw + 1)); done
    printf '서기 기다림 %s초\n' "$sw" > "$work/codex.log"
    if ! command -v "$codex" >/dev/null 2>&1; then
      verdict="검토 실패 (codex 명령 없음)"
    else
      model_args "${HARNESS_REVIEW_EFFORT:-medium}"  # 검토는 6.1 Sol medium (읽기만 하는 판정. 더 깊게는 HARNESS_REVIEW_EFFORT=high)
      set -- exec --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check ${MODEL_ARGS[@]+"${MODEL_ARGS[@]}"} \
        -C "$ROOT/verify" -o "$last" "$(review_prompt "$who" "$n" "$inf" "$rep")"
      if command -v timeout >/dev/null 2>&1; then
        timeout 600 "$codex" "$@" </dev/null >>"$work/codex.log" 2>&1; rc=$?  # 읽기만 하는 검토라 10분이면 끊는다 (목표 5분)
      else
        "$codex" "$@" </dev/null >>"$work/codex.log" 2>&1; rc=$?
      fi
      verdict="$(verdict_of "$rc" "$last")"
    fi
    out="$d/검토${n}_${who}_${title}.md"; k=2
    while [ -e "$out" ]; do out="$d/검토${n}_${who}_${title}_$k.md"; k=$((k + 1)); done
    {
      header 검토 "verify (자동)" main "$n"
      printf '판정: %s\n' "$verdict"
      if [ -n "$gl" ]; then printf '%s\n' "$gl"; fi
      printf '\n'
      if [ -s "$last" ]; then cat "$last"; printf '\n'; fi
      printf '\n기록: verify/검토%s/codex.log\n' "$n"
    } > "$out"
    arrive "$who" "$n"  # 판정이 났으니 메인 받은편지함에 도착
    [ -e "$d/.검토$n.다시" ] && continue
    rm -f "$d/.검토$n.진행"
    rmdir "$lk" 2>/dev/null || rm -rf "$lk"  # 잠금을 푼 뒤 한 번 더 본다
    [ -e "$d/.검토$n.다시" ] || break  # 푸는 사이에 다시 표시가 들어왔으면 한 번 더
    mkdir "$lk" 2>/dev/null || break  # 그 표시를 쓴 쪽이 먼저 잠금을 잡았으면 그쪽이 돈다
  done
}

# 팀원 자동: 팀장이 send 하면 팀원이 화면 없이 (codex exec) 돌고, 끝나면 그 답이 보고로 팀장에게 간다. 사람은 팀원 창을 열지 않는다
# 팀원 폴더의 .h/ 에 남는 것: 실행<N>.진행 (도는 동안만, 「pid 시작초 방식」), 실행<N>.log (실행 출력 전부), 답<N>.md (마지막 답), 실패<N>.md
set_now() {  # 전역 NOW_S = 지금 Unix 초 (bash 의 printf 로, 못 하면 date. 둘 다 못 읽으면 0). 기다리는 고리에서 하위 셸을 띄우지 않으려고 전역에
  NOW_S=""
  printf -v NOW_S '%(%s)T' -1 2>/dev/null || NOW_S=""
  case "$NOW_S" in ''|*[!0-9]*) NOW_S="$(date +%s 2>/dev/null || true)";; esac
  case "$NOW_S" in ''|*[!0-9]*) NOW_S=0;; esac
}
epoch() { set_now; printf '%s' "$NOW_S"; }
MEMBER_MIN="${HARNESS_MEMBER_MINUTES:-45}"  # 팀원 상한 (분)
case "$MEMBER_MIN" in ''|*[!0-9]*) MEMBER_MIN=45;; esac
MEMBER_MIN="$((10#$MEMBER_MIN))"
member_minutes() { printf '%s' "$MEMBER_MIN"; }
model_args() {  # 생각깊이 → 전역 배열 MODEL_ARGS (-m 모델 -c model_reasoning_effort=깊이). HARNESS_MODEL 을 빈 값으로 두면 붙이지 않는다 (Codex 기본 설정)
  local model="${HARNESS_MODEL-gpt-6.1-sol}"
  MODEL_ARGS=()
  if [ -n "$model" ]; then MODEL_ARGS=(-m "$model" -c "model_reasoning_effort=$1"); fi
}
model_label() { local model="${HARNESS_MODEL-gpt-6.1-sol}"; if [ -n "$model" ]; then printf '%s %s' "$model" "$1"; else printf 'Codex 기본 설정'; fi; }
member_running() {  # 팀원 → 도는 실행이 있으면 그 전달문 번호를 찍고 (전역 RUN_N, RUN_T 에도) 0, 없으면 1
  # 죽었거나, 그 번호를 이제 남의 프로세스가 쓰거나, 상한 + 5분보다 오래된 진행 표시는 (컨테이너 재시작, 강제 종료로 남은 것) 지우고 없는 것으로 본다
  local f n pid t how cap
  RUN_N=""; RUN_T=""
  set_now; cap=$(( (MEMBER_MIN + 5) * 60 ))
  for f in "$ROOT/$1/.h/실행"*.진행; do
    [ -e "$f" ] || continue
    n="${f##*/실행}"; n="${n%.진행}"
    pid=""; t=""; how=""
    read -r pid t how < "$f" 2>/dev/null || true
    case "$pid" in ''|*[!0-9]*) rm -f "$f"; continue;; esac
    if [ "$how" = send ]; then  # send 가 띄우는 중 (pid 는 보낸 h). 2분 안이면 도는 것으로 본다
      case "$t" in ''|*[!0-9]*) t=0;; esac
      if ! pid_alive "$pid" || { [ "$NOW_S" -gt 0 ] && [ "$((NOW_S - t))" -gt 120 ]; }; then rm -f "$f"; continue; fi
    elif ! pid_alive "$pid" || ! pid_is_member "$pid"; then rm -f "$f"; continue; fi
    case "$t" in
      ''|*[!0-9]*) ;;
      *) if [ "$NOW_S" -gt 0 ] && [ "$((NOW_S - t))" -gt "$cap" ]; then rm -f "$f"; continue; fi;;
    esac
    RUN_N="$n"; RUN_T="$t"
    printf '%s' "$n"
    return 0
  done
  return 1
}
write_marker() {  # 팀원 번호 pid 방식 [시작초] → 진행 표시를 한 번에 쓴다 (임시 파일 뒤 mv: 읽는 쪽이 반쯤 쓴 줄을 보지 않게)
  local mk="$ROOT/$1/.h/실행$2.진행"
  mkdir -p "$ROOT/$1/.h"
  if [ -z "${5:-}" ]; then set_now; fi
  printf '%s %s %s\n' "$3" "${5:-$NOW_S}" "$4" > "$mk.$$" && mv -f "$mk.$$" "$mk"
}
pid_alive() {  # pid → 살아 있으면 0 (거두지 않은 죽은 프로세스 (Z) 는 죽은 것으로 본다. 컨테이너의 1번이 거두지 않을 때)
  local s=""
  case "$1" in ''|*[!0-9]*|0|1) return 1;; esac  # 0 과 1 은 h 가 쓰지 않는다 (kill -0 0 은 통하고, 그 아래는 컨테이너 전체다)
  kill -0 "$1" 2>/dev/null || return 1
  read -r s 2>/dev/null < "/proc/$1/stat" || [ -n "$s" ] || return 0
  s="${s##*) }"
  [ "${s%% *}" != Z ]
}
pid_runs() {  # pid 낱말 → 그 명령 줄에 그 낱말 (h 의 실행기 「_member」, 「_review」) 이 있으면 0. 컨테이너가 다시 떠 같은 번호를 받은 남의 프로세스를 거른다
  local a="" c=""
  [ -r "/proc/$1/cmdline" ] || return 0  # 명령 줄을 못 읽는 곳은 그 실행기로 본다
  while IFS= read -r -d '' a; do c="$c $a"; done 2>/dev/null < "/proc/$1/cmdline"
  c="$c $a"
  case "$c" in *"$2"*) return 0;; esac
  [ -n "${c// /}" ] || return 0
  return 1
}
pid_is_member() { pid_runs "$1" _member; }  # pid → h 의 팀원 실행기면 0
review_lock_alive() {  # 검토 잠금 폴더 → 그 안의 pid (일꾼이 쓴다) 의 자동 검토 일꾼이 살아 있으면 0
  local p=""
  read -r p < "$1/pid" 2>/dev/null || true
  pid_alive "$p" && pid_runs "$p" _review
}
proc_tree() {  # pid → 그 pid 와 그 아래 모든 pid (위에서 아래로). /proc/<pid>/stat 의 부모 번호를 따른다 (Linux 와 Git Bash 가 같은 꼴)
  local root="$1" d line rest pairs="" queue out="" cur e
  for d in /proc/[0-9]*; do
    line=""
    read -r line < "$d/stat" 2>/dev/null || [ -n "$line" ] || continue
    rest="${line##*) }"; rest="${rest#* }"  # 「상태 부모 …」 에서 부모
    pairs="$pairs ${d#/proc/}:${rest%% *}"
  done
  queue="$root"
  while [ -n "$queue" ]; do
    cur="${queue%% *}"
    if [ "$cur" = "$queue" ]; then queue=""; else queue="${queue#* }"; fi
    case " $out " in *" $cur "*) continue;; esac
    out="$out $cur"
    for e in $pairs; do
      if [ "${e#*:}" = "$cur" ]; then queue="${queue:+$queue }${e%%:*}"; fi
    done
  done
  printf '%s' "${out# }"
}
kill_tree() {  # pid 방식 → 그 프로세스와 그 아래를 모두 끝낸다. 5초 뒤에도 남은 것은 강제로
  # Git Bash 는 exec 사슬에서 Windows 의 부모 연결이 끊겨 taskkill /T 만으로는 아래가 남는다 (실측). 그래서 /proc 의 부모 번호로 찾고,
  # 찾은 것마다 taskkill /T 로 그 아래의 네이티브 프로그램 (node, codex 와 그 셸) 까지 끝낸다
  local root="$1" how="${2:-}" pids p w tk="" alive k=0
  case "$root" in ''|*[!0-9]*|0|1) return 0;; esac  # 0 과 1 의 아래는 컨테이너 전체다
  pids="$(proc_tree "$root")"
  if command -v taskkill >/dev/null 2>&1; then tk=1; fi
  for p in $pids; do
    if [ -n "$tk" ] && [ -r "/proc/$p/winpid" ]; then
      w=""; read -r w < "/proc/$p/winpid" 2>/dev/null || true
      if [ -n "$w" ]; then taskkill //F //T //PID "$w" >/dev/null 2>&1 || true; fi
    fi
    kill -TERM "$p" 2>/dev/null || true
  done
  if [ "$how" = setsid ]; then kill -TERM -- "-$root" 2>/dev/null || true; fi
  while :; do
    alive=""
    for p in $pids; do if pid_alive "$p"; then alive="$alive $p"; fi; done
    if [ -z "$alive" ] || [ "$k" -ge 5 ]; then break; fi
    sleep 1; k=$((k + 1))
  done
  for p in $alive; do kill -KILL "$p" 2>/dev/null || true; done
  return 0
}
member_prompt() {  # 팀원 팀장 번호 → codex exec 에 넘길 팀원 지시 글 (경로는 팀원 폴더 기준)
  local m="$1" lead="$2" n="$3" inf rel
  rel="delivery/$lead/in_/전달문${n}_*.md"
  inf="$(find_in "$m" "$n" 2>/dev/null || true)"
  if [ -n "$inf" ]; then rel="${inf#"$ROOT/$m"/}"; fi
  printf '%s\n' "[팀원 일] 너는 $m 세션이다 (팀장 $lead 의 팀원). 사람과 대화하지 않는다. 이 폴더가 작업 폴더이고, 끝나면 너의 마지막 답이 그대로 팀장 $lead 에게 보고로 간다."
  printf '%s\n' "먼저 AGENTS.md, ../AGENTS.md, ../$lead/공용.md, 받은 전달문 $rel 을 읽고 그대로 한다."
  printf '%s\n' "- 모호한 것은 공용.md 와 전달문으로 판단해 하고, 판단한 것은 보고의 「결정」 칸에 적는다. 막히면 할 수 있는 데까지 하고 「물을 것」 에 적는다"
  printf '%s\n' "- 쓰기는 이 폴더 안만 (남의 폴더의 것을 돌릴 때는 이 폴더로 복사해 돌린다). h 명령은 쓰지 않는다 (받기와 보고는 h 가 대신한다)"
  printf '%s\n' "- delivery/ 안의 파일은 읽기만 한다. 보고 파일과 이름 끝은 h 가 쓴다 (../AGENTS.md 규칙 4 의 보고 쓰기와 이름 끝 바꾸기는 h 가 하니 너는 하지 않는다)"
  printf '%s\n' "- 서버를 띄우면 공용.md 에 적힌 내 포트로 (없으면 $((18100 + 10#$n))), 끝내기 전에 끈다"
  printf '%s\n' "- 마지막 답이 보고다: ../main/양식.md 의 보고 칸 (요약, 타임라인, 완수, 결정, 흐름, 사례, 문제, 검증 결과, 남은 한계, 물을 것) 을 「## 요약」 부터 쓴다 (머리는 h 가 붙인다). 결과물 파일 경로와 돌린 명령을 넣는다"
  printf '%s\n' "- $(member_minutes)분 안에 끝낸다 (넘기면 h 가 끊고 실패로 보고한다)"
}
launch_member() {  # 팀원 번호 생각깊이 → 그 팀원을 뒤에서 화면 없이 띄우고 한 줄로 알린다 (자동 검토와 같은 셋 갈래)
  # 진행 표시는 send 가 잠근 채 먼저 써 두었다 (방식 send). 띄운 뒤 실행기의 pid 로 바꾼다
  local m="$1" n="$2" eff="$3" how
  if [ -n "${HARNESS_MEMBER_SYNC:-}" ]; then how=sync
  elif command -v setsid >/dev/null 2>&1; then how=setsid
  else how=nohup; fi
  printf '팀원 %s 시작: 전달문 %s (화면 없음, %s. 끝나면 보고가 저절로 온다. 모으기: ../h wait)\n' "$m" "$n" "$(model_label "$eff")"
  case "$how" in
    sync) "$BASH" "$SELF" _member "$m" "$n" "$eff" "$how" </dev/null >/dev/null 2>&1;;
    setsid) setsid nohup "$BASH" "$SELF" _member "$m" "$n" "$eff" "$how" </dev/null >/dev/null 2>&1 &
      if [ -e "$ROOT/$m/.h/실행$n.진행" ]; then write_marker "$m" "$n" "$!" "$how"; fi;;
    *) nohup "$BASH" "$SELF" _member "$m" "$n" "$eff" "$how" </dev/null >/dev/null 2>&1 &
      if [ -e "$ROOT/$m/.h/실행$n.진행" ]; then write_marker "$m" "$n" "$!" "$how"; fi;;
  esac
  return 0
}
strip_head() {  # 파일 → 첫 「## 」 머리부터 (보고 꼴의 「# 보고」 와 「- 보낸 이」 같은 머리 줄은 h 가 붙이니 뺀다). 「## 」 가 없으면 그대로
  if grep -q '^## ' "$1" 2>/dev/null; then awk 'f || /^## / { f = 1; print }' "$1"; else cat "$1"; fi
}
cmd_member_worker() {  # h _member <팀원> <번호> <생각깊이> [방식]: 팀원 한 번을 끝까지 (launch_member 가 뒤에서 부른다). take → codex exec → done 이나 ask
  local m="$1" n="$2" eff="${3:-medium}" how="${4:-nohup}" codex="${HARNESS_CODEX:-codex}" lead hd mk ans log fail mins rc why="" pid="" t="" x=""
  local cpid="" outd before g b own="" body tailx="" tail15="" nl=$'\n'
  lead="$(parent_of "$m")" || exit $?
  hd="$ROOT/$m/.h"; mkdir -p "$hd"
  mk="$hd/실행$n.진행"; ans="$hd/답$n.md"; log="$hd/실행$n.log"; fail="$hd/실패$n.md"; body="$hd/보고본문$n.md"
  if [ -e "$mk" ]; then read -r pid t x < "$mk" 2>/dev/null || true; fi
  case "$t" in ''|*[!0-9]*) t="";; esac
  write_marker "$m" "$n" "$$" "$how" "$t"  # 이 실행기의 pid 로 (stop 이 끊을 것)
  mins="$(member_minutes)"
  rm -f "$ans" "$fail" "$body"
  if ! ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_take "$n" ) > "$log" 2>&1; then
    printf '\n팀원 실행 멈춤: 전달문 %s 을 받지 못했다\n' "$n" >> "$log"
    rm -f "$mk"
    exit 1
  fi
  outd="$ROOT/$m/delivery/$lead/out_"; mkdir -p "$outd"
  before="$nl"  # 팀원이 규칙 4 대로 보고 파일을 손으로 써도 가려내려고 (실제 한 바퀴에서 있었다). 줄마다 이름 하나
  for g in "$outd"/*; do if [ -e "$g" ]; then before="$before${g##*/}$nl"; fi; done
  printf '\n== codex exec (%s, 상한 %s분) ==\n' "$(model_label "$eff")" "$mins" >> "$log"
  if ! command -v "$codex" >/dev/null 2>&1; then
    rc=127; why="codex 명령 없음"
  else
    model_args "$eff"
    set -- exec --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check ${MODEL_ARGS[@]+"${MODEL_ARGS[@]}"} \
      -C "$ROOT/$m" -o "$ans" "$(member_prompt "$m" "$lead" "$n")"
    trap 'kill_tree "$cpid"; exit 143' TERM INT HUP  # 팀장의 stop: 아래 codex 까지 끝낸다 (기다리는 중이라 바로 받는다)
    if command -v timeout >/dev/null 2>&1 && [ "$mins" -gt 0 ]; then
      timeout "$((mins * 60))" "$codex" "$@" </dev/null >>"$log" 2>&1 &
    else
      "$codex" "$@" </dev/null >>"$log" 2>&1 &
    fi
    cpid=$!
    wait "$cpid"; rc=$?
    trap - TERM INT HUP
  fi
  tailx="$(grep -v '^[[:space:]]*$' "$log" 2>/dev/null | tail -n 1 | cut -c1-160)"  # 실패 보고에 실을 기록 끝 (손 보고를 옮겼다는 줄이 붙기 전에)
  tail15="$(grep -v '^[[:space:]]*$' "$log" 2>/dev/null | tail -n 15 | cut -c1-300)"
  for g in "$outd"/*; do  # 팀원이 손으로 쓴 보고는 옆 (.h/팀원이쓴_…) 으로. 보고는 h 가 하나만 쓴다
    [ -f "$g" ] || continue
    b="${g##*/}"
    case "$before" in *"$nl$b$nl"*) continue;; esac
    mv -f "$g" "$hd/팀원이쓴_$b"; own="$hd/팀원이쓴_$b"
    printf '\n팀원이 보고 파일을 직접 썼다: .h/팀원이쓴_%s 로 옮겼다 (보고는 h 가 하나만 쓴다)\n' "$b" >> "$log"
  done
  if ! find_in "$m" "$n" >/dev/null 2>&1; then  # 팀원이 받은 전달문의 이름을 바꿨거나 지웠으면 다시 받는다
    ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_take "$n" ) >> "$log" 2>&1
  fi
  if [ "$rc" = 0 ] && [ -s "$ans" ] && ! is_blank "$(cat "$ans")"; then
    strip_head "$ans" > "$body"
  elif [ "$rc" = 0 ] && [ -n "$own" ]; then  # 마지막 답은 비었으나 손으로 쓴 보고가 있으면 그것을
    strip_head "$own" > "$body"
  fi
  if [ -s "$body" ] && ! is_blank "$(cat "$body")"; then
    ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_report done "$n" "$body" ) >> "$log" 2>&1
  else
    if [ -z "$why" ]; then
      case "$rc" in
        0) why="답이 비었다";;
        124) why="상한 ${mins}분을 넘김";;
        *) why="끝 코드 $rc";;
      esac
    fi
    {
      printf '## 요약\n\n팀원 실행 실패: %s%s (h 가 대신 쓴 보고)\n\n' "$why" "${tailx:+. 기록 끝줄: $tailx}"
      printf '## 문제\n\n- 까닭: %s\n- 끝 코드: %s\n- 기록: %s/.h/실행%s.log' "$why" "$rc" "$m" "$n"
      if [ -s "$ans" ]; then printf ' (마지막 답: %s/.h/답%s.md)' "$m" "$n"; fi
      if [ -n "$own" ]; then printf '\n- 팀원이 손으로 쓴 보고: %s/.h/%s' "$m" "${own##*/}"; fi
      printf '\n\n기록 끝 15줄:\n\n```text\n%s\n```\n\n## 물을 것\n\n- 다시 보내거나 (../h send %s <제목> <본문 파일> --high) 직접 한다\n' "$tail15" "$m"
      printf -- '- 모델 이름이 안 되는 판이면 다시 보낼 때 모델을 바꾼다: HARNESS_MODEL= ../h send … (Codex 기본 모델) 또는 HARNESS_MODEL=<그 판의 모델 이름> ../h send …\n'
    } > "$fail"
    ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_report ask "$n" "$fail" ) >> "$log" 2>&1
  fi
  rm -f "$mk"
}
member_state() {  # 팀원 → 전역 MS (「도는 중 7분 (전달문 4)」, 「끝 (보고 3)」, 「끝 (보고 3, 질문)」, 「빔」) 와 REP (마지막 보고 파일. 도는 중이면 빈 글)
  # 기다리는 고리에서 1분마다 부르니 하위 셸을 띄우지 않는다 (Git Bash 는 하위 셸이 느리다)
  local m="$1" t f b n
  MS=""; REP=""
  if member_running "$m" >/dev/null; then
    t="$RUN_T"
    case "$t" in
      ''|*[!0-9]*) t='?';;
      *) if [ "$NOW_S" -gt 0 ]; then t="$(( (NOW_S - t) / 60 ))"; else t='?'; fi;;
    esac
    MS="도는 중 ${t}분 (전달문 $RUN_N)"
    return 0
  fi
  for f in "$ROOT/$m/delivery/${m%_*}/out_/보고"*.md; do  # 팀원 이름 = 팀장_숫자
    [ -e "$f" ] || continue
    if [ -z "$REP" ] || [ "$f" -nt "$REP" ]; then REP="$f"; fi
  done
  if [ -z "$REP" ]; then MS="빔"; return 0; fi
  b="${REP##*/}"; n="${b#보고}"; n="${n%%_*}"
  case "$b" in
    *_질문.md|*_질문_[0-9]*.md) MS="끝 (보고 $n, 질문)";;
    *) MS="끝 (보고 $n)";;
  esac
}
scan_members() {  # 팀원 목록 → 전역 LINE (「product_1 도는 중 7분 (전달문 4), product_2 끝 (보고 3)」) 과 RUNNING (도는 수)
  local m
  LINE=""; RUNNING=0
  for m in $1; do
    member_state "$m"
    LINE="${LINE:+$LINE, }$m $MS"
    case "$MS" in "도는 중"*) RUNNING=$((RUNNING + 1));; esac
  done
}
wait_lines() {  # 팀원 목록 → 팀원마다 상태 한 줄. 끝난 팀원은 마지막 보고의 자리와 요약 줄 (요약, 결정)
  local m
  for m in $1; do
    member_state "$m"
    printf '%s %s\n' "$m" "$MS"
    if [ -n "$REP" ]; then
      printf '  보고: %s\n' "${REP#"$ROOT"/}"
      summary_lines "$REP" "요약" "요약" "결정" "결정" | sed 's/^/  /'
    fi
  done
}
cmd_wait() {  # h wait [분]: 팀장이 자기 팀원이 다 끝날 때까지 기다린다 (기본은 팀원 상한과 같은 45분). wait 0 은 기다리지 않고 지금 상태만
  local who ms cap step unit tick limit waited=0 since=0
  who="$(me)"
  ms=""
  if [ -n "$who" ] && [ "$who" != main ]; then ms="$(members "$who")"; fi
  [ -n "$ms" ] || die 1 "wait 는 팀원이 있는 팀장이 자기 폴더에서 쓴다 (지금 자리: ${who:-모름})"
  cap="${1:-$MEMBER_MIN}"
  case "$cap" in ''|*[!0-9]*) die 1 "쓰는 법: h wait [분]  (0 이면 기다리지 않고 지금 상태만)";; esac
  cap="$((10#$cap))"
  if [ "$cap" -eq 0 ]; then wait_lines "$ms"; return 0; fi
  scan_members "$ms"
  if [ "$RUNNING" -eq 0 ]; then printf '도는 팀원 없음\n'; wait_lines "$ms"; return 0; fi
  step="${HARNESS_WAIT_STEP:-60}"; case "$step" in ''|*[!0-9]*) step=60;; esac
  unit="${HARNESS_WAIT_UNIT:-60}"; case "$unit" in ''|*[!0-9]*) unit=60;; esac
  step="$((10#$step))"; unit="$((10#$unit))"
  if [ "$step" -lt 1 ]; then step=60; fi
  if [ "$unit" -lt 1 ]; then unit=60; fi
  tick=5; if [ "$step" -lt "$tick" ]; then tick="$step"; fi  # 끝나면 곧 돌아오게 자주 보고, 진행 줄은 step 마다
  limit="$((cap * unit))"
  printf '기다린다 (상한 %s분): %s\n' "$cap" "$LINE"
  while [ "$RUNNING" -gt 0 ]; do
    if [ "$waited" -ge "$limit" ]; then
      wait_lines "$ms"
      printf '아직 도는 중 %s: 다시 ../h wait\n' "$RUNNING"
      return 0
    fi
    sleep "$tick"; waited="$((waited + tick))"; since="$((since + tick))"
    scan_members "$ms"
    if [ "$RUNNING" -gt 0 ] && [ "$since" -ge "$step" ]; then since=0; printf '%s\n' "$LINE"; fi
  done
  printf '다 끝났다\n'
  wait_lines "$ms"
}
cmd_stop() {  # h stop <팀원>: 팀장이 도는 팀원을 끊고 (아래 codex 까지) 「중단」 보고를 남긴다. 방향이 틀렸을 때 고친 전달문을 다시 보내려고
  local who m n pid t how hd k inf
  who="$(me)"; m="${1:-}"
  if [ -z "$m" ] || ! members "$who" | grep -qx -- "$m"; then
    die 1 "쓰는 법: h stop <팀원>  (팀장이 자기 팀원에게. ${who:-?} 의 팀원: $(members "$who" | tr '\n' ' '))"
  fi
  n="$(member_running "$m")" || die 1 "$m 은 도는 일이 없다 (../h wait 0)"
  hd="$ROOT/$m/.h"
  pid=""; t=""; how=""
  read -r pid t how < "$hd/실행$n.진행" 2>/dev/null || true
  kill_tree "$pid" "$how"
  rm -f "$hd/실행$n.진행"
  inf="$(find_in "$m" "$n" 2>/dev/null || true)"
  case "$inf" in
    *_처리완료.md|*_질문.md) printf '끊었다: %s 전달문 %s (그 전에 보고가 이미 왔다)\n' "$m" "$n"; return 0;;
  esac
  printf '## 요약\n\n중단: 팀장 %s 이 끊었다 (h stop, %s). 기록 %s/.h/실행%s.log\n\n## 물을 것\n\n- 고친 전달문을 다시 보내거나 (../h send %s <제목> [본문]) 직접 한다\n' \
    "$who" "$(now)" "$m" "$n" "$m" > "$hd/중단$n.md"
  {
    if [ -z "$inf" ]; then ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_take "$n" ); fi  # 받기 전에 끊었으면 받은 것으로 하고 질문으로
    ( cd "$ROOT/$m" && HARNESS_ROLE="$m" && cmd_report ask "$n" "$hd/중단$n.md" )
  } >> "$hd/실행$n.log" 2>&1
  printf '끊었다: %s 전달문 %s (중단 보고를 남겼다: %s/delivery/%s/out_/)\n' "$m" "$n" "$m" "$who"
}

codex_home() { printf '%s' "${CODEX_HOME:-$HOME/.codex}"; }
cwd_key() {  # 절대 폴더 → Codex 기록 첫 줄의 cwd 글 (Windows 는 C:\\… 꼴)
  local p="$1"
  if command -v cygpath >/dev/null 2>&1; then p="$(cygpath -w "$p")"; p="${p//\\/\\\\}"; fi
  printf '"cwd":"%s"' "$p"
}
session_logs() {  # 그 폴더를 cwd 로 연 Codex 기록 (하루 안, 자동 검토 기록은 뺀다), 이른 것부터
  local dir key f
  dir="$(codex_home)/sessions"
  [ -d "$dir" ] || return 0
  key="$(cwd_key "$1")"
  find "$dir" -type f -name 'rollout-*.jsonl' -mmin -1440 2>/dev/null | LC_ALL=C sort | while IFS= read -r f; do
    head -n 1 "$f" | grep -qF -- "$key" || continue
    head -n 40 "$f" | grep -qF -- '[자동 검토]' && continue  # 자동 검토의 첫 글 (10째 줄쯤). 뒤에서 읽기만 한 세션은 빼지 않는다
    head -n 40 "$f" | grep -qF -- '[서기]' && continue  # 서기도 -C 루트로 돌아 cwd 가 메인과 같다. 메인의 기록으로 잡히면 h dialog 가 「대화」 를 못 찾는다
    printf '%s\n' "$f"
  done
}
utc_to_kst_hm() {  # YYYY-MM-DDTHH:MM:SS (UTC) → HH:MM (한국 시각). date 를 부르지 않고 9시간을 더한다
  local hh mm
  hh="${1:11:2}"; mm="${1:14:2}"
  case "$hh$mm" in ????) ;; *) printf '?'; return;; esac
  case "$hh$mm" in *[!0-9]*) printf '?'; return;; esac
  printf '%02d:%s' "$(( (10#$hh + 9) % 24 ))" "$mm"
}
session_state() {  # 절대 폴더 → 일하는 중 (HH:MM 부터) | 끝남 (HH:MM) | 끊김 (HH:MM) | 대기 | 모름
  local last line ts
  last="$(session_logs "$1" | tail -n 1)"
  [ -n "$last" ] || { printf '모름'; return; }
  line="$(grep -E '"type":"event_msg","payload":\{"type":"(task_started|task_complete|turn_aborted)"' "$last" | tail -n 1)"
  [ -n "$line" ] || { printf '대기'; return; }
  ts="$(printf '%s' "$line" | sed -n 's/^{"timestamp":"\([0-9T:-]\{19\}\).*/\1/p')"
  case "$line" in
    *'"payload":{"type":"task_started"'*) printf '일하는 중 (%s 부터)' "$(utc_to_kst_hm "$ts")";;
    *'"payload":{"type":"task_complete"'*) printf '끝남 (%s)' "$(utc_to_kst_hm "$ts")";;
    *) printf '끊김 (%s)' "$(utc_to_kst_hm "$ts")";;
  esac
}
context_pct() {  # 절대 폴더 → 그 세션이 쓴 맥락 % (Codex 기록의 마지막 token_count 줄: last_token_usage.total_tokens / model_context_window). 못 읽으면 ?
  local last line used win
  last="$(session_logs "$1" | tail -n 1)"
  [ -n "$last" ] || { printf '?'; return; }
  line="$(grep -F '"type":"token_count"' "$last" | tail -n 1)"
  [ -n "$line" ] || { printf '?'; return; }
  used="$(printf '%s' "$line" | sed -n 's/.*"last_token_usage":{[^}]*"total_tokens":\([0-9]*\).*/\1/p')"
  win="$(printf '%s' "$line" | sed -n 's/.*"model_context_window":\([0-9]*\).*/\1/p')"
  case "$used$win" in ''|*[!0-9]*) printf '?'; return;; esac
  if [ -z "$used" ] || [ -z "$win" ] || [ "$win" -eq 0 ]; then printf '?'; return; fi
  printf '%s%%' "$((used * 100 / win))"
}
# 서기 (서기 설계): 메인은 답만 바로 내고, 기록은 h dialog (원문을 바로) 와 h note (뒤에서 도는 서기) 로 남긴다
py_bin() {  # 진짜로 도는 파이썬 3 의 이름 (python3, 그다음 python). 없으면 빈 글. 이름만 있고 돌지 않는 것 (Windows 의 Store 스텁) 은 건너뛴다
  local c
  for c in python3 python; do
    command -v "$c" >/dev/null 2>&1 || continue
    [ "$("$c" -c 'import sys; print(sys.version_info[0])' 2>/dev/null)" = 3 ] || continue
    printf '%s' "$c"; return 0
  done
  return 1
}
main_log_last_user_text() {  # 머리말 → 가장 새 메인 기록에서 그 머리말로 시작하는 마지막 사람 입력을 「UTC시각<TAB>본문」 으로 (본문의 줄바꿈 그대로). 없으면 빈 출력, 끝 코드 1
  local py last
  py="$(py_bin)"; [ -n "$py" ] || return 2
  last="$(session_logs "$ROOT" | tail -n 1)"; [ -n "$last" ] || return 1
  "$py" - "$last" "$1" <<'PY'
import io, json, sys
path, head = sys.argv[1], sys.argv[2]
found = None
with io.open(path, encoding="utf-8", errors="replace") as f:
    for line in f:
        try:
            d = json.loads(line)
        except ValueError:
            continue
        p = d.get("payload") or {}
        if d.get("type") != "response_item" or p.get("type") != "message" or p.get("role") != "user":
            continue
        c = p.get("content")
        text = c if isinstance(c, str) else "".join(x.get("text", "") for x in (c or []) if isinstance(x, dict) and x.get("type") in ("input_text", "text"))
        if text.startswith(head):
            found = (d.get("timestamp", "")[:19], text)
if found is None:
    sys.exit(1)
out = io.open(sys.stdout.fileno(), "w", encoding="utf-8", closefd=False)
out.write(found[0] + "\t" + found[1])
out.flush()
PY
}
cmd_dialog() {  # h dialog: 「대화」 로 붙인 원문을 대화.md 「## 시간순」 끝에 T번호와 시각을 달아 그대로. 같은 글은 두 번 붙지 않는다
  local who rec rc ts text sha shaf n hm tmp f="$ROOT/대화.md"
  who="$(me)"; [ "$who" = main ] || die 1 "dialog 는 메인이 루트에서 쓴다"
  [ -f "$f" ] || die 2 "대화.md 가 없다"
  rec="$(main_log_last_user_text '대화')"; rc=$?
  case "$rc" in
    2) printf 'h dialog 못 함 (python 없음): 메인이 직접 적는다\n'; return 0;;
    1) printf 'h dialog 못 함 (메인의 Codex 기록에 「대화」 가 없다): 메인이 직접 적는다\n'; return 0;;
  esac
  ts="${rec%%	*}"; text="${rec#*	}"
  is_blank "$text" && { printf 'h dialog 못 함 (「대화」 뒤가 비었다): 메인이 직접 적는다\n'; return 0; }
  mkdir -p "$ROOT/.h"; shaf="$ROOT/.h/dialog.sha"
  sha="$(printf '%s' "$text" | "$(py_bin)" -c 'import hashlib, sys; print(hashlib.sha256(sys.stdin.buffer.read()).hexdigest())')"
  if n="$(grep -F "$sha" "$shaf" 2>/dev/null | head -n 1 | cut -f2)" && [ -n "$n" ]; then
    printf '이미 붙였다: %s (대화.md 시간순)\n' "$n"; return 0
  fi
  n="$(sed -n 's/^### T\([0-9]*\) .*/\1/p' "$f" | sort -n | tail -n 1)"; n=$((${n:-0} + 1))
  hm="$(utc_to_kst_hm "$ts")"
  tmp="$(mktemp 2>/dev/null || printf '%s' "$ROOT/.h/dialog.tmp")"
  if grep -q '^## 분야별' "$f"; then
    # awk -v 는 역슬래시를 푼다. ENVIRON 으로 넘기면 그대로
    BLK="### T$n $hm" BODY="$text" awk 'BEGIN{done=0; blk=ENVIRON["BLK"]; body=ENVIRON["BODY"]} /^## 분야별/ && !done {print blk; print ""; print body; print ""; done=1} {print}' "$f" > "$tmp"
  else
    { cat "$f"; printf '\n### T%s %s\n\n%s\n' "$n" "$hm" "$text"; } > "$tmp"
  fi
  mv "$tmp" "$f"
  printf '%s\tT%s\n' "$sha" "$n" >> "$shaf"
  printf '붙였다: T%s (대화.md 시간순)\n' "$n"
}
SCRIBE_DIR_REL=".h/서기"
scribe_lock_alive() {  # 서기 잠금 폴더 → 그 안의 pid 의 서기 일꾼 (_scribe) 이 살아 있으면 0
  local p; p="$(cat "$1/pid" 2>/dev/null)"
  case "$p" in ''|*[!0-9]*) return 1;; esac
  pid_alive "$p" && pid_runs "$p" _scribe
}
scribe_state() { if [ -e "$ROOT/.h_scribe_off" ]; then printf '꺼짐'; else printf '켜짐'; fi; }
scribe_state_line() {  # 「서기: 도는 중 (HH:MM 부터) | 끝 HH:MM | 실패 HH:MM (기록 .h/서기/실행N.log) | 꺼짐 | 아직 없음」
  local d="$ROOT/$SCRIBE_DIR_REL" line
  if [ -e "$ROOT/.h_scribe_off" ]; then printf '서기: 꺼짐'; return; fi
  if [ -e "$d/진행" ]; then printf '서기: 도는 중 (%s 부터)' "$(cat "$d/진행" 2>/dev/null)"; return; fi
  if [ -s "$d/끝" ]; then
    line="$(head -n 1 "$d/끝")"
    case "$line" in
      *' ok') printf '서기: 끝 %s' "${line% ok}";;
      *) printf '서기: 실패 %s' "$line";;
    esac
    return
  fi
  printf '서기: 아직 없음'
}
scribe_pull_input() {  # 판 번호 → 지난 커서 뒤의 메인 기록 (사람 입력과 메인 답만) 과 아직 안 넘긴 메모를 .h/서기/입력<N>.md 로. 커서는 성공 뒤 scribe_advance 가 옮긴다
  local n="$1" d="$ROOT/$SCRIBE_DIR_REL" py last cur_file cur_line memo_done out="$ROOT/$SCRIBE_DIR_REL/입력$1.md"
  py="$(py_bin)"
  last="$(session_logs "$ROOT" | tail -n 1)"
  cur_file=""; cur_line=0
  if [ -s "$d/커서" ]; then cur_file="$(head -n 1 "$d/커서" | cut -f1)"; cur_line="$(head -n 1 "$d/커서" | cut -f2)"; fi
  [ "$cur_file" = "$last" ] || cur_line=0  # 기록 파일이 바뀌었으면 (메인을 다시 열었으면) 새 파일의 처음부터
  : > "$out"
  if [ -n "$py" ] && [ -n "$last" ]; then
    "$py" - "$last" "$cur_line" <<'PY' >> "$out" 2>/dev/null
import io, json, sys
path, start = sys.argv[1], int(sys.argv[2])
out = io.open(sys.stdout.fileno(), "w", encoding="utf-8", closefd=False)
n = 0
with io.open(path, encoding="utf-8", errors="replace") as f:
    for line in f:
        n += 1
        if n <= start:
            continue
        try:
            d = json.loads(line)
        except ValueError:
            continue
        p = d.get("payload") or {}
        if d.get("type") != "response_item" or p.get("type") != "message":
            continue
        role = p.get("role")
        c = p.get("content")
        text = c if isinstance(c, str) else "".join(x.get("text", "") for x in (c or []) if isinstance(x, dict) and x.get("type") in ("input_text", "output_text", "text"))
        text = text.strip()
        if not text or text.startswith("<") or text.startswith("# AGENTS.md instructions"):
            continue  # 환경 글과 규칙 전문 (Codex 가 사람 메시지로 넣는다) 은 사람 말이 아니다
        hh, mm = d.get("timestamp", "")[11:13], d.get("timestamp", "")[14:16]
        try:
            hm = "%02d:%s" % ((int(hh) + 9) % 24, mm)
        except ValueError:
            hm = "?"
        who = "사람" if role == "user" else ("메인" if role == "assistant" else None)
        if who is None:
            continue
        out.write("## %s %s\n\n%s\n\n" % (who, hm, text))
out.write("@@LINES %d\n" % n)
out.flush()
PY
  fi
  memo_done=0; [ -s "$d/메모.커서" ] && memo_done="$(head -n 1 "$d/메모.커서")"
  case "$memo_done" in ''|*[!0-9]*) memo_done=0;; esac
  if [ -s "$d/메모.md" ]; then
    wc -l < "$d/메모.md" | tr -d ' ' > "$d/입력$n.메모"  # 이 판이 읽은 메모 줄 수 (도는 사이 온 메모를 다음 판이 읽게, 커서는 여기까지만)
    { printf '## 메모 (메인이 h note 로 적은 것)\n\n'; head -n "$(cat "$d/입력$n.메모")" "$d/메모.md" | tail -n +$((memo_done + 1)); printf '\n'; } >> "$out"
  fi
  printf '%s\n' "$last" > "$d/입력$n.파일"
}
scribe_advance() {  # 판 번호 → 성공한 판의 커서 옮기기 (기록 줄 수와 메모 줄 수)
  local n="$1" d="$ROOT/$SCRIBE_DIR_REL" lines last
  lines="$(sed -n 's/^@@LINES \([0-9]*\)$/\1/p' "$d/입력$n.md" | tail -n 1)"
  last="$(head -n 1 "$d/입력$n.파일" 2>/dev/null)"
  if [ -n "$lines" ] && [ -n "$last" ]; then printf '%s\t%s\n' "$last" "$lines" > "$d/커서"; fi
  if [ -s "$d/입력$n.메모" ]; then cp "$d/입력$n.메모" "$d/메모.커서"; fi  # 그 판이 읽은 줄까지만 (그 뒤 온 메모는 다음 판)
}
scribe_prompt() {  # 판 번호 → codex exec 에 넘길 서기 글 (경로는 루트 기준)
  local n="$1"
  printf '%s\n' "[서기] 너는 서기다. 메인 (경영진) 이 사람과 주고받은 것을 아래 입력 파일로 받아 기록 파일을 고친다. 사람 말과 담당자 말은 지어내지 않고 그대로 인용한다. 한국어로 쓴다 (한자와 일본어를 섞지 않는다)."
  printf '읽을 것: %s/입력%s.md (「## 사람」 은 사람 입력, 「## 메인」 은 메인의 답, 「## 메모」 는 메인이 h note 로 적은 것. 「@@LINES」 줄은 무시). 지금의 기록 파일 다섯도 읽는다.\n' "$SCRIBE_DIR_REL" "$n"
  printf '%s\n' "고치는 파일 (이 다섯만): 대화.md 의 「## 분야별」 절 (「## 시간순」 은 h 가 쓰니 손대지 않는다), 요구.md (상태, 출처, 「모르는 곳」 절. 번호 R, U 는 메인이 글에 매긴 것을 그대로 쓴다), 결정.md (결정과 사례. 까닭은 사람 말 그대로 인용, 번호 D 도 메인이 매긴 것), 합의문.md (다섯 칸과 머리의 상태. 사람이 담당자 동의를 「대화」 로 붙였으면 「확정 HH:MM (대화 T번호)」), STATE.md (단계와 다음)."
  printf '%s\n' "바뀐 줄만 고치고 통째로 다시 쓰지 않는다. 다른 파일은 읽기만 한다 (전달문, 보고, 코드, 피드백.md, 설명.md 는 고치지 않는다). h 명령을 쓰지 않는다. 서버를 띄우거나 시험을 돌리지 않는다."
  printf '%s\n' "입력에 새로 적을 것이 없으면 아무것도 고치지 않고 「고칠 것 없음」 이라고 답한다. 마지막 답은 고친 파일 이름과 줄 수만 한 줄씩."
}
launch_scribe() {  # 서기를 뒤에서 띄운다. 도는 중이면 다시 표시만 (끝난 뒤 그 사이 쌓인 것으로 한 번 더). 자동 검토 (launch_review) 와 같은 방식
  local d="$ROOT/$SCRIBE_DIR_REL" lk
  mkdir -p "$d"; lk="$d/잠금"
  : > "$d/다시"
  # 주인 일꾼 (_scribe) 이 죽은 잠금은 1분 뒤 진행 표시와 함께 치운다 (산 서기의 잠금은 나이와 상관없이 둔다)
  if [ -d "$lk" ] && [ -n "$(find "$lk" -maxdepth 0 -mmin +1 2>/dev/null)" ] && ! scribe_lock_alive "$lk"; then rm -rf "$lk"; rm -f "$d/진행"; fi
  if ! mkdir "$lk" 2>/dev/null; then
    printf '서기 대기: 도는 중이라 끝나면 한 번 더 돈다\n'; return 0
  fi
  if [ ! -e "$d/다시" ]; then rmdir "$lk" 2>/dev/null || true; printf '서기: 앞 판이 이 메모까지 적었다\n'; return 0; fi
  rm -f "$d/다시"
  now_hm > "$d/진행"
  if [ -n "${HARNESS_SCRIBE_SYNC:-}" ]; then
    "$BASH" "$SELF" _scribe </dev/null >/dev/null 2>&1
  elif command -v setsid >/dev/null 2>&1; then
    setsid nohup "$BASH" "$SELF" _scribe </dev/null >/dev/null 2>&1 &
  else
    nohup "$BASH" "$SELF" _scribe </dev/null >/dev/null 2>&1 &
  fi
  printf '서기 시작 (뒤에서. 끝나면 h status 에 보인다)\n'
}
now_hm() { local s; s="$(now)"; printf '%s' "${s##* }"; }  # 지금의 HH:MM (HARNESS_NOW 도 따른다)
cmd_scribe_worker() {  # h _scribe: 서기 한 판을 끝까지 (launch_scribe 가 뒤에서 부른다). 도는 동안 note 가 오면 끝난 뒤 한 번 더
  local d="$ROOT/$SCRIBE_DIR_REL" lk n codex="${HARNESS_CODEX:-codex}" rc last log why
  lk="$d/잠금"; mkdir -p "$d"
  while :; do
    printf '%s\n' "$$" > "$lk/pid" 2>/dev/null || true
    now_hm > "$d/진행"; rm -f "$d/다시"
    n=1; while [ -e "$d/입력$n.md" ]; do n=$((n + 1)); done
    scribe_pull_input "$n"
    last="$d/답$n.md"; log="$d/실행$n.log"; rm -f "$last"
    if ! command -v "$codex" >/dev/null 2>&1; then
      rc=127; printf 'codex 명령 없음\n' > "$log"
    else
      model_args "${HARNESS_SCRIBE_EFFORT:-medium}"  # 서기는 6.1 Sol medium
      set -- exec --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check ${MODEL_ARGS[@]+"${MODEL_ARGS[@]}"} \
        -C "$ROOT" -o "$last" "$(scribe_prompt "$n")"
      if command -v timeout >/dev/null 2>&1; then
        HARNESS_SCRIBE_RUNNING=1 timeout 600 "$codex" "$@" </dev/null >"$log" 2>&1; rc=$?
      else
        HARNESS_SCRIBE_RUNNING=1 "$codex" "$@" </dev/null >"$log" 2>&1; rc=$?
      fi
    fi
    if [ "$rc" = 0 ]; then
      printf '%s ok\n' "$(now_hm)" > "$d/끝"
      scribe_advance "$n"
      commit_all "서기 기록 $n" >/dev/null 2>&1 || true
    else
      case "$rc" in 124) why="10분 넘김";; 127) why="codex 명령 없음";; *) why="codex 끝 코드 $rc";; esac
      printf '%s (%s, 기록 %s/실행%s.log)\n' "$(now_hm)" "$why" "$SCRIBE_DIR_REL" "$n" > "$d/끝"
    fi
    [ -e "$d/다시" ] && continue
    rm -f "$d/진행"
    rmdir "$lk" 2>/dev/null || rm -rf "$lk"
    [ -e "$d/다시" ] || break
    mkdir "$lk" 2>/dev/null || break
  done
}
cmd_note() {  # h note [--wait] <글…>: 메모를 .h/서기/메모.md 에 더하고 서기를 뒤에서 띄운다. --wait 는 서기가 끝날 때까지 (상한 600초)
  local who wait="" text d="$ROOT/$SCRIBE_DIR_REL" k=0
  who="$(me)"; [ "$who" = main ] || die 1 "note 는 메인이 루트에서 쓴다"
  if [ "${1:-}" = "--wait" ]; then wait=1; shift; fi
  if [ $# -gt 0 ]; then text="$*"; elif [ ! -t 0 ]; then text="$(body_from "")"; else text=""; fi
  if is_blank "$text"; then
    if [ -n "$wait" ]; then text="저장"  # 「저장」 은 글 없이 h note --wait
    else die 1 "쓰는 법: h note [--wait] <기록할 것 두세 줄>  (정해진 것, 새 요구 번호, 단계)"; fi
  fi
  # 앞 서기가 실패했으면 메인에게 알린다 (안 그러면 기록이 통째로 비는데 메인은 모른다)
  if [ -s "$d/끝" ] && ! grep -q ' ok$' "$d/끝"; then
    printf '앞 서기 실패 %s: 이번 것은 메인이 직접 적는다. 되풀이되면 h scribe off\n' "$(head -n 1 "$d/끝")"
  fi
  mkdir -p "$d"
  printf -- '- %s %s\n' "$(now_hm)" "$(printf '%s' "$text" | tr '\n' ' ')" >> "$d/메모.md"
  if [ -n "${HARNESS_SCRIBE_RUNNING:-}" ]; then printf '메모 적음 (서기 안이라 서기를 다시 띄우지 않는다)\n'; return 0; fi
  if [ "${HARNESS_SCRIBE:-1}" = 0 ]; then printf '메모 적음 (서기 꺼짐, HARNESS_SCRIBE=0)\n'; return 0; fi
  if [ -e "$ROOT/.h_scribe_off" ]; then printf '서기 꺼짐 (h scribe on): 메인이 직접 적는다\n'; return 0; fi
  launch_scribe
  if [ -n "$wait" ]; then
    while [ -e "$d/진행" ] || [ -d "$d/잠금" ]; do
      [ "$k" -ge 600 ] && { printf '서기가 10분 넘게 돈다. 기다리지 않고 돌아간다\n'; return 0; }
      sleep 1; k=$((k + 1))
    done
    printf '서기 끝 (%s)\n' "$(scribe_state_line)"
  fi
}
cmd_scribe_switch() {
  case "${1:-}" in
    off) : > "$ROOT/.h_scribe_off"; printf '서기: 꺼짐 (켜려면 h scribe on. 꺼진 동안은 메인이 기록을 직접 적는다)\n';;
    on) rm -f "$ROOT/.h_scribe_off"; printf '서기: 켜짐\n';;
    '') printf '%s\n' "$(scribe_state_line)";;
    *) die 1 "쓰는 법: h scribe on|off";;
  esac
}
feedback_open() {  # 루트 피드백.md 에서 상태가 「열림」 인 줄 수 (파일이 없으면 0)
  [ -f "$ROOT/피드백.md" ] || { printf '0'; return; }
  grep -c -E '^\|.*\|[[:space:]]*열림[[:space:]]*\|[[:space:]]*$' "$ROOT/피드백.md" || true
}
review_state() { if [ -e "$ROOT/.h_review_off" ]; then printf '꺼짐'; else printf '켜짐'; fi; }
review_verdict() {  # 번호 → 이상 없음 | 이상 있음 | 검토 중 | 검토 실패 | 확인 필요 | (없으면 빈 글)
  local n="$1" d m f best="" v
  d="$ROOT/verify/delivery/main/out_"; m="$d/.검토${n}.진행"
  if [ -e "$m" ]; then
    if [ -n "$(find "$m" -mmin +25 2>/dev/null)" ]; then printf '검토 실패'; else printf '검토 중'; fi
    return
  fi
  for f in "$d/검토${n}_"*.md; do
    [ -e "$f" ] || continue
    if [ -z "$best" ] || [ ! "$best" -nt "$f" ]; then best="$f"; fi
  done
  [ -n "$best" ] || return 0
  v="$(sed -n 's/^판정: *//p' "$best" | head -n 1)"
  case "$v" in
    '이상 없음'*) printf '이상 없음';;
    '이상 있음'*) printf '이상 있음';;
    '검토 실패'*) printf '검토 실패';;
    *) printf '확인 필요';;
  esac
}
cmd_review_switch() {
  case "${1:-}" in
    off) : > "$ROOT/.h_review_off"; printf '자동 검토: 꺼짐 (켜려면 h review on)\n';;
    on) rm -f "$ROOT/.h_review_off"; printf '자동 검토: 켜짐\n';;
    '') printf '자동 검토: %s\n' "$(review_state)";;
    *) die 1 "쓰는 법: h review on|off";;
  esac
}

status_session() {  # 세션 들여쓰기 → 그 세션의 줄과 전달문 줄 (상대는 parent_of). 어긋난 수를 ISSUES 에 더한다
  local r="$1" pad="$2" up f g n inf st rep stem title seen v k gt auto="" qn rd
  up="$(parent_of "$r")"
  if [ "$up" != main ]; then auto="$(member_running "$r" || true)"; fi  # 화면 없이 도는 팀원
  qn="$(queue_of "$r" | wc -l | tr -d ' ')"
  printf '%s%s  지금: %s  맥락 %s%s%s\n' "$pad" "$r" "$(session_state "$ROOT/$r")" "$(context_pct "$ROOT/$r")" "${auto:+  (자동, 전달문 $auto)}" "$([ "${qn:-0}" -gt 0 ] && printf '  쌓인 것 %s' "$qn")"
  seen=" "
  for f in "$ROOT/$up/delivery/$r/out_/전달문"*_*.md; do
    [ -e "$f" ] || continue
    n="$(num_of "$f")"; seen="$seen$n "
    stem="${f##*/}"; stem="${stem%.md}"; title="${stem#전달문"$n"_}"
    if inf="$(find_in "$r" "$n")"; then st="$(state_of "$inf")"; else st="아직 안 받음"; fi
    rep=""
    for g in "$ROOT/$r/delivery/$up/out_/보고${n}"_*.md; do [ -e "$g" ] && rep="보고 있음"; done
    v=""; gt=""
    if [ "$up" = main ]; then
      v="$(review_verdict "$n")"; gt="$(gate_line "$n")"
      case "$gt" in '시험: 없음'*) gt="";; esac  # 시험이 없는 팀장 (research) 은 보이지 않는다
    fi
    k="$(first_line_of "$f" "할 일")"
    rd=""; [ "$up" = main ] && [ -e "$ROOT/main/.h/받은보고/$n.읽음" ] && rd="메인 읽음"
    printf '%s  %s %s  %s%s%s%s%s%s\n' "$pad" "$n" "$title" "$st" "${rep:+  $rep}" "${v:+  검토: $v}" "${gt:+  $gt}" "${rd:+  $rd}" "${k:+  핵심 요청: $k}"
    if [ "$st" = 처리완료 ] && [ -z "$rep" ]; then
      printf '%s    ! %s %s: 처리완료인데 보고가 없다\n' "$pad" "$r" "$n"; ISSUES=$((ISSUES + 1))
    fi
  done
  for f in "$ROOT/$r/delivery/$up/in_/전달문"*_*.md; do
    [ -e "$f" ] || continue
    n="$(num_of "$f")"
    case "$seen" in
      *" $n "*) ;;
      *) printf '%s    ! %s: %s 이 %s 앞으로 보낸 적 없는 번호 %s\n' "$pad" "${f##*/}" "$up" "$r" "$n"; ISSUES=$((ISSUES + 1));;
    esac
  done
}
cmd_status() {
  local r m
  ISSUES=0
  printf '지금 %s (한국 시각)\n' "$(now)"
  printf '자동 검토: %s  피드백 열림 %s  %s\n' "$(review_state)" "$(feedback_open)" "$(scribe_state_line)"
  inbox_stale_reviews >/dev/null
  m="$(inbox_new | wc -l | tr -d ' ')"
  if [ "${m:-0}" -gt 0 ]; then printf '새 보고 %s (메인에 아무 말이나 치면 h inbox 로 읽는다)\n' "$m"; else printf '새 보고 없음\n'; fi
  for r in $(roles); do
    status_session "$r" ""
    for m in $(members "$r"); do status_session "$m" "  "; done
  done
  if [ "$ISSUES" -eq 0 ]; then printf '어긋난 것 없음\n'; else printf '어긋난 것 %s\n' "$ISSUES"; fi
}

cmd_help() {
  cat <<'TEXT'
h: 전달문과 보고를 한 번에 처리하는 하네스 명령 (번호, 머리, 시각, 복사, 이름 끝을 맡는다)
  메인 (루트에서 ./h)
    h send <받는 이> <제목> [본문 파일]  전달문을 번호 매겨 main/delivery/<받는 이>/out_ 에 쓴다
                                        본문은 파일이나 표준 입력. 없으면 양식의 일곱 칸 틀
    h status                            「현황」: 세션마다 지금 (일하는 중, 끝남, 끊김. Codex 기록에서),
                                        전달문 상태, 보고, 검토 판정, 어긋난 것
    h dialog                            「대화」 로 붙인 원문을 메인의 Codex 기록에서 찾아 대화.md 시간순에 T번호와 시각을 달아 그대로 붙인다
                                        (모델을 거치지 않는다. 같은 글은 두 번 붙지 않는다. 못 찾으면 메인이 직접 적는다)
    h note [--wait] <기록할 것>          메인이 답 끝에 친다. 메모를 남기고 서기 (뒤에서 도는 codex exec, medium) 를 띄운다.
                                        서기가 대화.md 분야별, 요구.md, 결정.md, 합의문.md, STATE.md 를 고친다 (메인은 안 쓴다)
                                        도는 중이면 끝난 뒤 한 번 더. --wait 는 끝날 때까지 (「저장」 뒤 /compact 전). 기록: .h/서기/
    h scribe on|off                     서기를 켜고 끈다 (꺼지면 메인이 직접 적는다). 인자 없이 치면 지금 상태
    h inbox                             새로 도착한 팀장 보고 (검토 판정 뒤에 도착. 질문은 바로) 를 보이고 읽음으로.
                                        메인이 사람 말을 받을 때마다 맨 먼저 친다. 「현황」 첫 줄에 「새 보고 N」
    h commit [메시지]                    지금까지를 git 에 커밋 (저장소일 때만. send, done, ask 도 저절로 커밋한다)
                                        되돌리기: git log --oneline 으로 판을 보고 git restore --source <판> -- <파일>
    h review on|off                     팀장이 done 할 때 도는 verify 자동 검토를 켜고 끈다
                                        (판정 파일: verify/delivery/main/out_/검토<번호>_*.md)
    h team <팀장> <수>                   팀장 (research, product, verify) 아래 팀원 세션 <팀장>_1 … 을 만든다 (1 에서 3)
                                        팀원은 팀장과만 주고받고, 팀장의 공용.md 를 읽는다. 다시 쳐도 있는 것은 그대로
  팀장과 팀원 (자기 폴더에서 ../h)
    h take <번호>                        상대 (메인이나 팀장) 의 out_ 에서 그 전달문을 자기 in_ 로 복사, 처리중, 내용 보여 주기
                                        번호 대신 알린 「번호_제목」 (예: 3_첫화면) 을 그대로 줘도 된다.
                                        제목이 그 번호의 전달문과 영 다르면 멈춘다 (잘못 친 번호)
    h done <번호> [보고 파일]             보고를 자기 out_ 에 쓰고 처리완료로 (파일이 없으면 표준 입력)
                                        팀장의 done 은 자동 검토가 돌고, 팀원의 done 은 돌지 않는다 (팀장이 모아 보고)
                                        팀장의 done 은 먼저 시험 관문: 폴더 맨 위의 run_tests.sh 를 그 폴더에서 한 번
                                        (상한 300초, HARNESS_GATE_SEC). 실패면 AI 검토 없이 이상 있음 (verify/검토<번호>/시험.txt)
    h ask <번호> [물을 것 파일]           물을 것을 보고로 쓰고 질문으로
    h queue                             대기열: 받았지만 아직 take 하지 않은 전달문을 번호 차례로. done 과 ask 끝에도 보인다.
                                        비어 있지 않으면 사람의 번호를 기다리지 않고 맨 앞을 이어서 받는다 (팀장 규칙)
  팀장 (자기 폴더에서 ../h. 팀원을 둘지와 몇은 팀장이 정한다)
    h send <팀원> <제목> <본문 파일> [--high]
                                        팀원은 <팀장>_1 ~ <팀장>_3. 폴더가 없으면 보낼 때 만든다
                                        (본문이 없으면 멈춘다. 팀원은 받자마자 돌아 나중에 채운 칸을 보지 못한다)
                                        자기 팀원에게 전달문을 쓰고 그 팀원을 화면 없이 띄운다 (codex exec. 모델은
                                        HARNESS_MODEL, 기본 gpt-6.1-sol. 생각 깊이 medium, --high 면 high. 상한 45분).
                                        끝나면 그 답이 보고로 저절로 온다. 번호는 전체에서 하나씩. 도는 팀원에게는 못 보낸다
                                        기록: <팀원>/.h/실행<번호>.log, 마지막 답 <팀원>/.h/답<번호>.md
                                        끄기 (사람이 팀원 창을 열고 번호를 친다): HARNESS_MEMBER_AUTO=0
    h wait [분]                          도는 팀원이 다 끝날 때까지 기다리고 (기본 45분) 팀원마다 보고의 요약 줄
    h wait 0                            기다리지 않고 지금 상태만 (누가 어느 전달문을 몇 분째, 누가 비었나)
    h stop <팀원>                        도는 팀원을 끊고 (아래 codex 까지) 「중단」 보고를 남긴다
  모두
    h now                               지금 한국 시각 (컨테이너 시계가 UTC 여도)
    h help                              이 글
TEXT
}

case "${1:-help}" in
  send) shift; cmd_send "$@";;
  take) shift; cmd_take "$@";;
  done) shift; cmd_report done "$@";;
  ask) shift; cmd_report ask "$@";;
  status) cmd_status;;
  dialog) cmd_dialog;;
  note) shift; cmd_note "$@";;
  scribe) shift; cmd_scribe_switch "$@";;
  _scribe) cmd_scribe_worker;;
  inbox) cmd_inbox;;
  queue) cmd_queue;;
  review) shift; cmd_review_switch "$@";;
  team) shift; cmd_team "$@";;
  commit) shift; cmd_commit "$@";;
  _review) shift; cmd_review_worker "$@";;
  _member) shift; cmd_member_worker "$@";;
  wait) shift; cmd_wait "$@";;
  stop) shift; cmd_stop "$@";;
  now) printf '지금 %s (한국 시각)\n' "$(now)";;
  help|-h|--help) cmd_help;;
  *) die 1 "모르는 명령: $1 (h help)";;
esac
