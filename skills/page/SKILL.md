---
name: page
description: 산출물이 프로젝트 소개 페이지(홍보, 결과 소개, 정적 HTML 한 장)일 때 product 가 읽는다. 입력을 받아 처리하는 화면은 webapp, 발표 자료는 pptx 를 쓴다
---

# 프로젝트 소개 페이지

- 정적 HTML 한 장 product/site/index.html. 바깥 라이브러리 없이, CSS 는 파일 안에. 파일로 열어도 보여야 한다
- 차례: 제목과 한 줄 요약 → 문제 → 해결 (그림이나 표) → 결과 숫자 → 써 보는 법 → 한계. 담당자 말은 대화.md 에서 인용
- 그림은 product/site/img/ 에 PNG. 데이터에서 만든 그림은 만든 스크립트를 같이 둔다. 그림이 없으면 표로
- 휴대폰 폭(400px)에서도 가로 넘침이 없게 (표는 overflow-x: auto)
- 검증: python 으로 파일을 읽어 절 제목 여섯이 있는지, 이미지 경로가 실제 파일인지 확인하는 시험 (product/site/tests/test_site.py)
- 「피드백」 전달문을 받으면 그 절만 고친다. 글은 짧게, 숫자는 보고서.md 와 같아야 한다
