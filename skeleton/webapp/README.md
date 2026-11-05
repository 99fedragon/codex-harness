# 데이터 서비스 뼈대

요청을 받아 만드는 시제품의 공통 부분. 과제 풀이 코드가 아니다. 과제가 웹 서비스면 이 폴더를 `product/app/` 으로 복사해 시작하고, 아니면 그대로 둔다.

## 들어 있는 것

| 파일 | 하는 일 |
|---|---|
| `app.py` | FastAPI 앱과 API. `create_app(db_path, data_dir)` 로 만든다 |
| `db.py` | SQLite. 빈 DB 에서 스키마와 로그인 계정 둘 (operator, member. 비밀번호는 해시) 을 만든다. 다시 켜도 남는다. 데이터셋의 users 는 자료이고 로그인 계정이 아니다 |
| `auth.py` | 토큰 로그인 (요청 머리 `Authorization: Bearer`). 쿠키를 쓰지 않아 미리보기 창에서도 세션이 이어진다. 서명 비밀값은 `SECRET`, 없으면 DB 에 둔 무작위 값 |
| `datasets.py` | `data/<이름>/` 묶음의 목록, 검증 (필수 열, 빈 값, 데이터 줄 없음, 정수 id, 참조 ID, 상태 값, 중복), 적용 (실패하면 기존 유지, 로그인 계정은 그대로) |
| `compute.py` | 재계산의 빈 자리. `compute(params, dataset, conn)` 에 과제의 엔진을 끼운다 |
| `static/` | 로그인, 운영자 화면 (탭: 데이터셋, 조건과 재계산, 결과, 내보내기), 일반 화면 (탭: 내 항목, 설정). 색과 글꼴은 `theme.css` 변수 |
| `tests/` | `test_app.py` (헬스, 로그인, 묶음 거절, 머리 줄만 있는 묶음 거절, 적용 뒤 계정 유지, 상태 되풀이), `test_layout.py` (새 창 없음, 비밀번호 저장 창 없음, 단추 크기, 좁은 창) |
| `check_preview.py` | 서버를 띄워 로그인 → 요청 → 다시 요청이 되는지 본다 |
| `Dockerfile`, `env.example`, `requirements.txt` | 컨테이너, 환경 변수 보기, 의존성 둘 |

## 실행

```bash
pip install -r requirements.txt
cp env.example .env   # 값을 환경 변수로 넣는다 (export 나 docker --env-file)
uvicorn app:app --host 0.0.0.0 --port 8000
```

- 화면 `http://127.0.0.1:8000/`. 운영자는 `operator` / `ADMIN_PASSWORD`, 일반은 `member` / `MEMBER_PASSWORD`
- 시험: `python -m unittest tests.test_app tests.test_layout`. 미리보기 점검: `python check_preview.py`
- 컨테이너: `docker build -t svc . && docker run -p 8000:8000 -e ADMIN_PASSWORD=... svc`
- pip 가 막히면: `app.py` 의 FastAPI 를 표준 라이브러리 `http.server` 로 바꾸고 `db.py` 와 `datasets.py` 는 그대로 쓴다 (둘 다 표준 라이브러리만 쓴다)

## 기능 한 칸 더하는 법 (API 하나, 탭 하나, 시험 하나를 한 묶음으로)

1. `datasets.py` 의 `FILES` 에 CSV 와 필수 열을 더하고, `check_refs` 에 참조 검사를 더한다
2. `db.py` 의 `SCHEMA` 에 표를 더하고 `datasets.apply` 에서 그 표를 채운다
3. `app.py` 에 API 하나 (`@app.get("/things")`, 역할은 `auth.require("operator")` 나 `"member"`)
4. `static/index.html` 의 `.tabs` 에 `<button data-tab="things">` 와 `<div class="panel hidden" id="tab-things">` 를 더하고, `static/app.js` 에 읽어 그리는 함수 하나. 상태를 바꾸는 단추는 누른 뒤 서버에서 다시 읽는다
5. `tests/test_app.py` 에 시험 하나
6. 엔진은 `compute.py` 의 `compute` 안에서. 결과 줄은 `rows` 에 넣으면 결과 탭과 내보내기에 그대로 나온다

## 지키는 것

- 새 창, 팝업, `alert`, `confirm`, 파일 선택창과 저장창을 열지 않는다. 내보내기는 화면에 JSON 을 보이고 복사한다
- 비밀번호 칸은 `type="password"` 를 쓰지 않는다 (로그인 뒤 브라우저의 비밀번호 저장 창이 뜨면 전체 화면이 풀린다). `type="text"` 에 `class="secret"` (글자를 가림), `autocomplete="off"`
- 상태를 바꾼 뒤에는 서버에서 다시 읽어 그린다 (화면의 값을 믿지 않는다)
- 화면이 부르는 주소와 `static/` 경로는 앞에 `/` 를 붙이지 않는다 (미리보기가 `/proxy/8000/` 같은 경로 뒤에 떠도 돈다). 토큰은 `store` 로 읽고 쓴다 (저장소가 막힌 창에서도 메모리로 돈다)
- 비밀값은 환경 변수로만. 코드에 적지 않는다
- 「회사 스타일로」 는 `static/theme.css` 의 변수 (색, 글꼴, 로고 글자) 만 바꾼다. 로고 그림은 넣지 않는다
