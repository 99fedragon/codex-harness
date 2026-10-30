---
name: webapp
description: 산출물이 웹 앱(화면에서 입력하고 결과를 보는 것)일 때 product 가 읽는다. 정적 소개 페이지는 page, 명령줄 도구는 app, 발표 자료는 pptx 를 쓴다
---

# 웹 앱

- 묻지 말고 고른다: streamlit 이 깔려 있으면 streamlit (한 파일 app.py). 없으면 fastapi 와 정적 index.html. 둘 다 없으면 python -m http.server 로 여는 정적 HTML 과 JS
- 핵심 기능 하나를 먼저 끝까지 (입력 → 처리 → 결과 표시). 꾸밈은 그다음
- 자리: product/app/ 에 app.py (또는 main.py), 화면 파일, requirements.txt, 실행 한 줄을 적은 README 한 단락
- 실행 한 줄과 포트를 보고서.md 실행법에 그대로
- 검증: 서버를 띄우고 requests 나 curl 로 주소 하나를 받아 200 과 핵심 결과 문자열을 확인하는 시험 (product/app/tests/test_app.py). 화면은 스크린샷 대신 응답 본문으로 본다
- 「피드백」 전달문을 받으면 바꾸기 전에 기존 시험이 도는지 보고, 바꾼 곳과 확인 방법을 보고에 적는다
- 데이터는 원본 경로를 읽는다. 복사하지 않는다 (규칙 3)
- 앱이 쓰는 자리 (로그, 기록 파일) 는 실행 폴더 기준 상대 경로나 환경 변수로 받는다. verify 가 자기 폴더로 복사해 돌리고, 외주는 자기 폴더 밖에 쓸 수 없다
