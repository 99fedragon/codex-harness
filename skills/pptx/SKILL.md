---
name: pptx
description: 산출물이 발표 자료(pptx, 슬라이드)일 때 product 나 메인이 읽는다. 문서 보고서는 report, 소개 페이지는 page 를 쓴다
---

# 발표 자료

- python-pptx 가 있는지 먼저 본다 (python -c "import pptx"). 있으면 product/deck/make_deck.py 로 만든다 (글과 표는 코드에 두어 다시 만들 수 있게). 없으면 HTML 슬라이드 (product/deck/index.html, 절 하나가 한 장, 인쇄하면 쪽마다 한 장)
- 차례 (여덟 장 안): 제목 → 문제와 담당자 요구 → 데이터 → 방법 → 결과 숫자 → 시연 (화면 그림이나 실행 한 줄) → 한계 → 다음
- 장마다 제목 한 줄과 항목 셋에서 다섯. 숫자는 보고서.md 와 같아야 한다
- 그림은 파일로 두고 코드가 넣는다. 없으면 표
- 검증: 만든 파일을 다시 열어 장 수와 제목을 찍는 시험 (product/deck/tests/test_deck.py). pptx 는 python-pptx 로 열어 센다
- 「피드백」 은 장 번호로 받고 그 장만 고친다
