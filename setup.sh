#!/usr/bin/env bash
# Codex 세션 넷 하네스를 현재 폴더에 깐다. 있는 파일은 덮어쓰지 않는다.
# AGENTS.md 가 이미 있으면 (플랫폼의 지침) 끝에 열세 줄을 더한다. README.md 는 만들지 않는다
# (작업 폴더의 README.md 는 과제 안내일 수 있다. 제출 문서는 규칙 6 의 보고서.md).
# 세션 폴더마다 AGENTS.md (역할), 스킬 아홉은 묶음의 skills/ 에서 .agents/skills/ 로 (웹 올리기는 점으로 시작하는 폴더를 빼서 묶음에는 skills/), 명령 h (전달문과 보고, 현황, 자동 검토) 와 go (세션 열기, 모두 yolo) 는 루트에.
# 작업 폴더가 git 저장소면 push 를 막는 설정을 넣는다 (push 는 쓰지 않는다. .git 은 Codex 가 고칠 수 없다).
# 쓰는 법: git clone <이 저장소> _h && bash _h/setup.sh && rm -rf _h
set -e
here="$(cd "$(dirname "$0")" && pwd)"
roles="research product verify"
platform="${HARNESS_PLATFORM:-}"
if [ -z "$platform" ]; then
  case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) platform=windows;; *) platform=linux;; esac
fi

if [ -e AGENTS.md ]; then
  grep -q '^1 메인(main)은' AGENTS.md || { printf '\n' >> AGENTS.md; cat "$here/AGENTS.md" >> AGENTS.md; }
else
  cp "$here/AGENTS.md" AGENTS.md
fi
for f in 합의문.md STATE.md 대화.md; do
  [ -e "$f" ] || cp "$here/$f" "$f"
done
mkdir -p main
[ -e main/양식.md ] || cp "$here/main/양식.md" main/양식.md
for p in $roles; do
  mkdir -p "main/delivery/$p/in_" "main/delivery/$p/out_" "$p/delivery/main/in_" "$p/delivery/main/out_"
  [ -e "$p/AGENTS.md" ] || sed "s/{역할}/$p/g" "$here/역할_AGENTS.md" > "$p/AGENTS.md"
done
mkdir -p .agents/skills
for s in "$here"/skills/*/ "$here"/.agents/skills/*/; do
  [ -f "$s/SKILL.md" ] || continue
  n="$(basename "$s")"
  [ -e ".agents/skills/$n/SKILL.md" ] || { mkdir -p ".agents/skills/$n"; cp "$s/SKILL.md" ".agents/skills/$n/SKILL.md"; }
done
nsk=0
for f in .agents/skills/*/SKILL.md; do if [ -f "$f" ]; then nsk=$((nsk + 1)); fi; done
case "$nsk" in 9) skl="스킬 아홉 (.agents/skills)";; 0) skl="스킬 없음 (묶음에 skills/ 가 없다)";; *) skl="스킬 ${nsk}개 (.agents/skills)";; esac

# 명령: Linux 는 루트에 h 와 go. Windows 는 Codex 가 PowerShell 로 돌리니 h.cmd 가 Git Bash 로 h 를 부른다
if [ "$platform" = windows ]; then
  mkdir -p .harness
  [ -e .harness/h ] || cp "$here/h" .harness/h
  [ -e h.cmd ] || printf '@echo off\r\n"%%ProgramFiles%%\\Git\\bin\\bash.exe" "%%~dp0.harness\\h" %%*\r\n' > h.cmd
  next="다음: 터미널마다 Codex 를 yolo 로 연다 (codex --yolo 를 열고 main, 외주는 codex --yolo -C research research)"
else
  [ -e h ] || { cp "$here/h" h; chmod +x h; }
  [ -e go ] || { cp "$here/go" go; chmod +x go; }
  next="다음: ./go main. 다른 터미널은 ./go research, ./go product, ./go verify (모두 yolo 로 연다. go 가 없으면 codex --yolo -C research research)"
fi

# git: 작업 폴더가 git 저장소가 아니면 만든다 (변경 추적과 되돌리기). 커밋에 쓸 이름이 없으면 이 저장소에만 둔다. 받아 온 묶음 폴더는 커밋에서 뺀다
in_git() { command -v git >/dev/null 2>&1 && git rev-parse --is-inside-work-tree >/dev/null 2>&1; }
if command -v git >/dev/null 2>&1 && ! in_git; then git init -q . >/dev/null 2>&1 || true; fi
if in_git; then
  git config user.name >/dev/null 2>&1 || git config --local user.name harness
  git config user.email >/dev/null 2>&1 || git config --local user.email harness@localhost
  ex="$(git rev-parse --git-path info/exclude 2>/dev/null)"
  pre="$(git rev-parse --show-prefix 2>/dev/null)"
  case "$here" in
    "$PWD"/*) rel="${here#"$PWD"/}"; mkdir -p "$(dirname "$ex")"
      grep -qxF "/$pre$rel/" "$ex" 2>/dev/null || printf '/%s%s/\n' "$pre" "$rel" >> "$ex";;
  esac
fi

# push 막기: git 저장소면 push 주소를 없는 방식(nopush:)으로 바꿔 push 가 시작 전에 멈추게 한다. clone 과 fetch 는 그대로
if in_git; then
  for u in https:// http:// ssh:// git@ git://; do
    git config --local --get-all url.nopush:.pushinsteadof 2>/dev/null | grep -qxF "$u" || git config --local --add url.nopush:.pushInsteadOf "$u"
  done
  guard="push 막기 설정을 넣었다 (이 폴더의 git)"
else
  guard="git 이 없어 push 막기와 커밋은 건너뜀"
fi

# 첫 커밋 (h 가 10MB 넘는 파일과 .h/ 를 뺀다). 그 뒤로는 h 의 send, done, ask 가 저절로 커밋한다
hcmd=./h
if [ "$platform" = windows ]; then hcmd=.harness/h; fi
first="$(bash "$hcmd" commit 하네스 깔기 2>/dev/null || true)"

echo "하네스 준비 끝: AGENTS.md (열세 줄), 합의문.md, STATE.md, 대화.md, main/양식.md, 세션 폴더 셋의 AGENTS.md, 폴더 스물여섯, $skl, 명령 h. $guard. ${first:+$first. }있던 파일은 그대로 (AGENTS.md 는 끝에 더함)"
echo "$next"
