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

roles() {  # 외주 세션: 루트 아래 delivery/main 이 있는 폴더
  local d
  for d in "$ROOT"/*/; do
    d="$(basename "${d%/}")"
    [ "$d" = main ] && continue
    [ -d "$ROOT/$d/delivery/main" ] && printf '%s\n' "$d"
  done
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

find_out() {  # 세션 번호 → 메인이 그 세션 앞으로 쓴 전달문
  local f
  for f in "$ROOT/main/delivery/$1/out_/전달문$2"_*.md; do [ -e "$f" ] && { printf '%s' "$f"; return 0; }; done
  return 1
}
find_in() {  # 세션 번호 → 그 세션이 받은 전달문 (상태는 아무것이나)
  local f
  for f in "$ROOT/$1/delivery/main/in_/전달문$2"_*.md; do [ -e "$f" ] && { printf '%s' "$f"; return 0; }; done
  return 1
}
list_nums() {  # 세션 → 메인이 그 세션 앞으로 보낸 것을 「번호_제목」 으로
  local f b out=""
  for f in "$ROOT/main/delivery/$1/out_/전달문"*_*.md; do
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
  die 2 "번호 $2 의 제목은 「$t」 이다 (받은 글: 「$3」). 메인이 알린 글을 다시 본다. 메인이 $1 앞으로 보낸 것:$(list_nums "$1")"
}
next_num() {
  local f n max=0
  for f in "$ROOT"/main/delivery/*/out_/전달문*_*.md; do
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
body_from() {  # 파일이 주어지면 그 파일, 아니면 표준 입력 (터미널이면 빈 글). 맨 앞 BOM 은 뺀다 (Windows PowerShell 5.1 이 붙인다)
  if [ -n "${1:-}" ]; then
    [ -f "$1" ] || die 1 "없는 본문 파일: $1"
    LC_ALL=C sed '1s/^\xEF\xBB\xBF//' "$1"
  elif [ ! -t 0 ]; then
    LC_ALL=C sed '1s/^\xEF\xBB\xBF//'
  fi
}

cmd_send() {
  local who to title n dir file body
  who="$(me)"
  [ "$who" = main ] || die 1 "send 는 메인이 루트에서 쓴다 (지금 자리: ${who:-모름})"
  [ $# -ge 2 ] && [ $# -le 3 ] || die 1 "쓰는 법: h send <받는 이> <제목> [본문 파일]  (제목은 한 낱말, 띄어쓰기는 _. 본문은 파일이나 표준 입력)"
  to="$1"; title="$(slug "$2")"
  roles | grep -qx -- "$to" || die 1 "없는 받는 이: $to. 있는 세션: $(roles | tr '\n' ' ')"
  body="$(body_from "${3:-}")"
  n="$(next_num)"
  dir="$ROOT/main/delivery/$to/out_"; mkdir -p "$dir"
  file="$dir/전달문${n}_${title}.md"
  {
    header 전달문 main "$to" "$n"
    if is_blank "$body"; then
      printf '## 목표\n\n## 맥락\n\n## 할 일\n\n## 낼 것\n\n## 고칠 폴더\n\n## 끝난 조건\n\n## 기한\n'
    else
      printf '%s\n' "$body"
    fi
  } > "$file"
  printf '썼다: 전달문 %s, main/delivery/%s/out_/%s\n' "$n" "$to" "${file##*/}"
  if is_blank "$body"; then printf '본문이 비어 양식의 일곱 칸을 넣었다. 채운 뒤 알린다\n'; fi
  printf '사람이 %s 창에 칠 글: %s_%s\n' "$to" "$n" "$title"
  commit_all "main 전달문 ${n}_${title}" || true
}

cmd_take() {
  local who n src dst stem
  who="$(me)"; split_ticket "${1:-}"; n="$TK_N"
  [ -n "$who" ] && [ "$who" != main ] || die 1 "take 는 외주 세션이 자기 폴더에서 쓴다"
  case "$n" in ''|*[!0-9]*) die 1 "쓰는 법: h take <번호> (번호_제목도 된다. 예: 3_첫화면)";; esac
  if dst="$(find_in "$who" "$n")"; then
    check_ticket "$who" "$n" "$TK_T" "$dst"
    printf '이미 받았다 (%s): %s\n\n' "$(state_of "$dst")" "${dst#"$ROOT"/}"
    cat "$dst"
    return 0
  fi
  src="$(find_out "$who" "$n")" || die 2 "없는 번호: $n. 메인이 $who 앞으로 보낸 것:$(list_nums "$who")"
  check_ticket "$who" "$n" "$TK_T" "$src"
  stem="${src##*/}"; stem="${stem%.md}"
  mkdir -p "$ROOT/$who/delivery/main/in_" "$ROOT/$who/.h"
  dst="$ROOT/$who/delivery/main/in_/${stem}_처리중.md"
  cp "$src" "$dst"
  snapshot "$who" > "$ROOT/$who/.h/take$n.snap"  # done 때 견줄 폴더 밖 목록 (yolo 라 밖 쓰기를 막지 못하니 뒤에서 잡는다)
  hdate -u '+%Y-%m-%dT%H:%M:%S' > "$ROOT/$who/.h/take$n.time" || true  # 못 읽으면 빈 파일 (추적은 기록 전부를 본다)
  printf '받았다: %s (처리중)\n\n' "${dst#"$ROOT"/}"
  cat "$dst"
  printf '\n끝나면: h done %s [보고 파일]. 판단이 필요하면: h ask %s [물을 것 파일]\n' "$n" "$n"
}

cmd_report() {  # done 이나 ask
  local kind="$1" who n inf body stem title rep st out k new
  who="$(me)"; split_ticket "${2:-}"; n="$TK_N"
  [ -n "$who" ] && [ "$who" != main ] || die 1 "h $kind: 외주 세션이 자기 폴더에서 쓴다"
  case "$n" in ''|*[!0-9]*) die 1 "쓰는 법: h $kind <번호 또는 번호_제목> [내용 파일]  (파일이 없으면 표준 입력)";; esac
  inf="$(find_in "$who" "$n")" || die 2 "아직 받지 않은 번호: $n. 먼저 h take $n"
  check_ticket "$who" "$n" "$TK_T" "$inf"
  body="$(body_from "${3:-}")"
  is_blank "$body" && die 1 "내용이 비었다. 보고 일곱 칸을 파일에 쓰고 h $kind $n <파일> 로, 또는 표준 입력으로"
  stem="${inf##*/}"; stem="${stem%.md}"; stem="${stem%_*}"
  title="${stem#전달문"$n"_}"
  if [ "$kind" = ask ]; then rep="보고${n}_${title}_질문"; st=질문; else rep="보고${n}_${title}"; st=처리완료; fi
  mkdir -p "$ROOT/$who/delivery/main/out_"
  out="$ROOT/$who/delivery/main/out_/$rep.md"; k=2
  while [ -e "$out" ]; do out="$ROOT/$who/delivery/main/out_/${rep}_$k.md"; k=$((k + 1)); done
  { header 보고 "$who" main "$n"; printf '%s\n' "$body"; } > "$out"
  new="${inf%/*}/${stem}_${st}.md"
  if [ "$inf" != "$new" ]; then mv "$inf" "$new"; fi
  printf '썼다: %s\n전달문 %s 상태: %s\n사람이 main 창에 알릴 말: %s 보고 %s_%s\n' "${out#"$ROOT"/}" "$n" "$st" "$who" "$n" "$title"
  local label=보고
  if [ "$kind" = ask ]; then label=질문; fi
  if commit_all "$who $label ${n}_${title}"; then printf '커밋: %s %s %s_%s\n' "$who" "$label" "$n" "$title"; fi
  if [ "$kind" = done ]; then
    trace "$who" "$n"
    launch_review "$who" "$n"
  fi
}

# 밖 변경 추적과 자동 검토 (v1.5. 모든 세션이 yolo 라 샌드박스가 없다. 막지 못하니 done 때 잡아서 알린다)
TAB="$(printf '\t')"
# 그 세션이 친 명령에 이 가운데 하나가 있으면 「쓰기 명령 있음」 (PowerShell, .NET, apply_patch, 셸, 파이썬과 판다스)
WRITE_RE='Add-Content|Set-Content|Out-File|AppendAll|WriteAll|New-Item|Copy-Item|Move-Item|Remove-Item|Rename-Item|apply_patch|[*]{3} (Update|Add|Delete) File|>>|> *[.][.][/\\]|(^|[^[:alnum:]_-])(cp|mv|rm|touch|tee|truncate) |sed -i|[.]write[(]|write_text|to_csv|to_excel|savefig'
snapshot() {  # 세션 → 그 세션 폴더 밖의 파일 목록 (상대 경로 TAB 바뀐 때 TAB 크기), 정렬. find 가 -printf 를 못 쓰면 「#목록실패」 한 줄
  local t st
  t="$(mktemp 2>/dev/null || printf '%s' "$ROOT/.h_snap.$$")"
  ( cd "$ROOT" && "${HARNESS_FIND:-find}" . \( -path "./$1" -o -name .git -o -name node_modules -o -name __pycache__ -o -name .venv \
      -o -name venv -o -name .h -o -path './verify/검토*' \) -prune -o -type f \
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
launch_review() {  # 세션 번호 → verify 자동 검토를 뒤에서 띄우고 한 줄로 알린다
  local who="$1" n="$2" d
  if [ "$who" = verify ]; then printf '자동 검토 건너뜀: verify 의 일\n'; return 0; fi
  if [ "${HARNESS_REVIEW:-1}" = 0 ]; then printf '자동 검토 꺼짐 (HARNESS_REVIEW=0)\n'; return 0; fi
  if [ -e "$ROOT/.h_review_off" ]; then printf '자동 검토 꺼짐 (h review on)\n'; return 0; fi
  if [ ! -d "$ROOT/verify" ]; then printf '자동 검토 건너뜀: verify 폴더가 없다\n'; return 0; fi
  d="$ROOT/verify/delivery/main/out_"; mkdir -p "$d"; : > "$d/.검토$n.진행"
  if [ -n "${HARNESS_REVIEW_SYNC:-}" ]; then
    "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1
  elif command -v setsid >/dev/null 2>&1; then
    setsid nohup "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1 &
  else
    nohup "$BASH" "$SELF" _review "$who" "$n" </dev/null >/dev/null 2>&1 &
  fi
  printf '자동 검토 시작: 검토 %s (끝나면 h status 에 판정)\n' "$n"
}
review_prompt() {  # 세션 번호 전달문 보고 → codex exec 에 넘길 글 (경로는 verify 기준)
  local who="$1" n="$2" rin="(없음)" rrep="(없음)"
  [ -n "$3" ] && rin="../${3#"$ROOT"/}"
  [ -n "$4" ] && rrep="../${4#"$ROOT"/}"
  printf '%s\n' "[자동 검토] 너는 verify 의 자동 검토다. ../.agents/skills/review/SKILL.md 가 있으면 읽고 따른다 (없으면 아래만)."
  printf '번호 %s, 맡은 세션 %s. 전달문 %s, 보고 %s, 결과물 ../%s/, 밖 변경 추적 ../%s/.h/trace%s.txt, 합의문 ../합의문.md, 대화 ../대화.md.\n' \
    "$n" "$who" "$rin" "$rrep" "$who" "$who" "$n"
  printf '%s\n' "판정은 끝난 조건과 합의문과 대화의 요구로 한다. 보고는 돌리는 법만 참고하고, 보고의 검증 결과는 판정을 낸 뒤에 견준다 (다르면 이상 있음)."
  printf '%s\n' "결과물을 검토$n/ 로 복사해 직접 돌려 조건마다 값을 코드로 재고, 보고가 말하지 않은 빈틈 (요구 가운데 빠진 기능, 보안: 입력 검사, 스크립트 삽입, 비밀 값, 오류에 드러난 내부 정보) 을 셋까지 짧게 훑는다. 밖 변경 추적에서 이 세션이 자기 폴더 밖에 쓴 흔적과, 결과물과 보고에 원본 개인정보가 남았는지도 본다."
  printf '%s\n' "쓰기는 검토$n/ 안에서만. 남의 폴더는 고치지 않고 h 명령은 쓰지 않는다. 웹 서버를 띄우면 포트는 $((18000 + 10#$n)), 끝내기 전에 끈다. 15분 안에 끝낸다."
  printf '%s\n' "마지막 답의 첫 줄은 「이상 없음」 이나 「이상 있음: 까닭 한 줄」 만 쓴다. 무거운 빈틈 (보안 구멍, 핵심 요구 빠짐) 은 이상 있음, 가벼운 빈틈은 적기만 한다. 그다음 조건별 결과, 빈틈, 보고와 다른 곳, 밖 쓰기, 개인정보, 돌린 명령."
}
verdict_of() {  # codex 끝 코드, 마지막 답 파일 → 판정 줄
  local line
  if [ "$1" = 124 ]; then printf '검토 실패 (20분 넘김)'; return; fi
  if [ "$1" != 0 ]; then printf '검토 실패 (codex 끝 코드 %s)' "$1"; return; fi
  if [ ! -s "$2" ]; then printf '검토 실패 (답이 비었다)'; return; fi
  line="$(grep -v '^[[:space:]*#>]*$' "$2" | head -n 1 | sed 's/^[[:space:]*#>]*//; s/^판정:[[:space:]]*//; s/[[:space:]*#>]*$//')"
  case "$line" in
    '이상 없음'*|'이상없음'*|'이상 있음'*|'이상있음'*) printf '%s' "$line";;
    *) printf '확인 필요 (답의 꼴이 어긋남)';;
  esac
}
cmd_review_worker() {  # h _review <세션> <번호>: 자동 검토 하나를 끝까지 (launch_review 가 뒤에서 부른다)
  local who="$1" n="$2" codex="${HARNESS_CODEX:-codex}" d work inf rep="" g stem title last verdict rc out k
  d="$ROOT/verify/delivery/main/out_"; work="$ROOT/verify/검토$n"; mkdir -p "$d" "$work"; : > "$d/.검토$n.진행"
  inf="$(find_in "$who" "$n" || true)"
  for g in "$ROOT/$who/delivery/main/out_/보고${n}"_*.md; do
    [ -e "$g" ] || continue
    if [ -z "$rep" ] || [ ! "$rep" -nt "$g" ]; then rep="$g"; fi
  done
  stem="${inf##*/}"; stem="${stem%.md}"; stem="${stem%_*}"; title="${stem#전달문"$n"_}"
  [ -n "$title" ] || title="전달문$n"
  last="$work/last.md"; rm -f "$last"
  if ! command -v "$codex" >/dev/null 2>&1; then
    verdict="검토 실패 (codex 명령 없음)"
  else
    set -- exec --dangerously-bypass-approvals-and-sandbox --skip-git-repo-check -C "$ROOT/verify" -o "$last" \
      "$(review_prompt "$who" "$n" "$inf" "$rep")"
    if command -v timeout >/dev/null 2>&1; then
      timeout 1200 "$codex" "$@" </dev/null >"$work/codex.log" 2>&1; rc=$?
    else
      "$codex" "$@" </dev/null >"$work/codex.log" 2>&1; rc=$?
    fi
    verdict="$(verdict_of "$rc" "$last")"
  fi
  out="$d/검토${n}_${who}_${title}.md"; k=2
  while [ -e "$out" ]; do out="$d/검토${n}_${who}_${title}_$k.md"; k=$((k + 1)); done
  {
    header 검토 "verify (자동)" main "$n"
    printf '판정: %s\n\n' "$verdict"
    if [ -s "$last" ]; then cat "$last"; printf '\n'; fi
    printf '\n기록: verify/검토%s/codex.log\n' "$n"
  } > "$out"
  rm -f "$d/.검토$n.진행"
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

cmd_status() {
  local r f g n inf st rep stem title seen v issues=0
  printf '지금 %s (한국 시각)\n' "$(now)"
  printf '자동 검토: %s\n' "$(review_state)"
  for r in $(roles); do
    printf '%s  지금: %s\n' "$r" "$(session_state "$ROOT/$r")"
    seen=" "
    for f in "$ROOT/main/delivery/$r/out_/전달문"*_*.md; do
      [ -e "$f" ] || continue
      n="$(num_of "$f")"; seen="$seen$n "
      stem="${f##*/}"; stem="${stem%.md}"; title="${stem#전달문"$n"_}"
      if inf="$(find_in "$r" "$n")"; then st="$(state_of "$inf")"; else st="아직 안 받음"; fi
      rep=""
      for g in "$ROOT/$r/delivery/main/out_/보고${n}"_*.md; do [ -e "$g" ] && rep="보고 있음"; done
      v="$(review_verdict "$n")"
      printf '  %s %s  %s%s%s\n' "$n" "$title" "$st" "${rep:+  $rep}" "${v:+  검토: $v}"
      if [ "$st" = 처리완료 ] && [ -z "$rep" ]; then
        printf '    ! %s %s: 처리완료인데 보고가 없다\n' "$r" "$n"; issues=$((issues + 1))
      fi
    done
    for f in "$ROOT/$r/delivery/main/in_/전달문"*_*.md; do
      [ -e "$f" ] || continue
      n="$(num_of "$f")"
      case "$seen" in
        *" $n "*) ;;
        *) printf '    ! %s: 메인이 %s 앞으로 보낸 적 없는 번호 %s\n' "${f##*/}" "$r" "$n"; issues=$((issues + 1));;
      esac
    done
  done
  if [ "$issues" -eq 0 ]; then printf '어긋난 것 없음\n'; else printf '어긋난 것 %s\n' "$issues"; fi
}

cmd_help() {
  cat <<'TEXT'
h: 전달문과 보고를 한 번에 처리하는 하네스 명령 (번호, 머리, 시각, 복사, 이름 끝을 맡는다)
  메인 (루트에서 ./h)
    h send <받는 이> <제목> [본문 파일]  전달문을 번호 매겨 main/delivery/<받는 이>/out_ 에 쓴다
                                        본문은 파일이나 표준 입력. 없으면 양식의 일곱 칸 틀
    h status                            「현황」: 세션마다 지금 (일하는 중, 끝남, 끊김. Codex 기록에서),
                                        전달문 상태, 보고, 검토 판정, 어긋난 것
    h commit [메시지]                    지금까지를 git 에 커밋 (저장소일 때만. send, done, ask 도 저절로 커밋한다)
                                        되돌리기: git log --oneline 으로 판을 보고 git restore --source <판> -- <파일>
    h review on|off                     외주가 done 할 때 도는 verify 자동 검토를 켜고 끈다
                                        (판정 파일: verify/delivery/main/out_/검토<번호>_*.md)
  외주 (자기 폴더에서 ../h)
    h take <번호>                        메인의 out_ 에서 그 전달문을 자기 in_ 로 복사, 처리중, 내용 보여 주기
                                        번호 대신 메인이 알린 「번호_제목」 (예: 3_첫화면) 을 그대로 줘도 된다.
                                        제목이 그 번호의 전달문과 영 다르면 멈춘다 (잘못 친 번호)
    h done <번호> [보고 파일]             보고를 자기 out_ 에 쓰고 처리완료로 (파일이 없으면 표준 입력)
    h ask <번호> [물을 것 파일]           물을 것을 보고로 쓰고 질문으로
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
  review) shift; cmd_review_switch "$@";;
  commit) shift; cmd_commit "$@";;
  _review) shift; cmd_review_worker "$@";;
  now) printf '지금 %s (한국 시각)\n' "$(now)";;
  help|-h|--help) cmd_help;;
  *) die 1 "모르는 명령: $1 (h help)";;
esac
