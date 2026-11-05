"""데이터 서비스 뼈대 (FastAPI 와 SQLite). 과제 풀이 코드가 아니다. 요청을 받아 만드는 시제품의 공통 부분만 있다.
실행: uvicorn app:app --host 0.0.0.0 --port $PORT   (환경 변수는 env.example)
화면: /  API: /health /login /datasets /datasets/{name}/validate /datasets/{name}/apply /items /items/{id}/status /settings /recompute /export"""
import json
import os

from fastapi import Depends, FastAPI, HTTPException
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

import auth
import compute as compute_mod
import datasets as ds
import db as dbmod

HERE = os.path.dirname(os.path.abspath(__file__))


class Login(BaseModel):
    username: str
    password: str


class StatusChange(BaseModel):
    status: str


class Settings(BaseModel):
    capacity: int = None
    margin_minutes: int = None


class Params(BaseModel):
    capacity: int = None
    margin_minutes: int = None


def create_app(db_path=None, data_dir=None):
    """앱을 만든다. 시험은 임시 DB 와 임시 data 폴더로 만든다. DB 는 환경 변수 DB_PATH, 없으면 실행 폴더의 app.db (뼈대 폴더를 더럽히지 않는다)."""
    db_path = db_path or os.environ.get("DB_PATH", os.path.join(os.getcwd(), "app.db"))
    data_dir = data_dir or os.environ.get("DATA_DIR", os.path.join(HERE, "data"))
    dbmod.init_db(db_path, os.environ.get("ADMIN_PASSWORD", "operator"), os.environ.get("MEMBER_PASSWORD", "member"))
    with dbmod.connect(db_path) as c:  # 비밀값: 환경 변수 SECRET, 없으면 DB 에 둔 무작위 값 (공개된 기본값으로 토큰을 위조하지 못하게)
        secret = os.environ.get("SECRET") or dbmod.app_secret(c)
    app = FastAPI(title="data service skeleton", docs_url=None, redoc_url=None)
    app.state.secret = secret
    app.state.db_path = db_path
    app.state.data_dir = data_dir

    def conn():
        return dbmod.connect(db_path)

    with conn() as c:  # 처음 켤 때 샘플 묶음이 있고 데이터가 비어 있으면 적용해 둔다
        if dbmod.current_dataset(c) is None and "sample" in ds.list_datasets(data_dir):
            ds.apply(c, data_dir, "sample")

    @app.get("/health")
    def health():
        return {"status": "ok"}

    @app.post("/login")
    def login(body: Login):
        with conn() as c:
            user = dbmod.check_account(c, body.username, body.password)  # 로그인 계정 (accounts). 데이터셋의 users 가 아니다
        if not user:
            raise HTTPException(status_code=401, detail="아이디나 비밀번호가 맞지 않는다")
        return {"token": auth.make_token(secret, user["username"], user["role"]), "role": user["role"], "username": user["username"]}

    @app.get("/datasets")
    def datasets(user=Depends(auth.require("member"))):
        with conn() as c:
            return {"current": dbmod.current_dataset(c), "available": ds.list_datasets(data_dir)}

    @app.post("/datasets/{name}/validate")
    def validate(name: str, user=Depends(auth.require("operator"))):
        ok, errors, _ = ds.validate(data_dir, name)
        return {"ok": ok, "errors": errors}

    @app.post("/datasets/{name}/apply")
    def apply(name: str, user=Depends(auth.require("operator"))):
        with conn() as c:
            ok, errors = ds.apply(c, data_dir, name)
        if not ok:
            raise HTTPException(status_code=400, detail={"message": "검증에 실패해 적용하지 않았다. 기존 데이터와 결과를 유지한다", "errors": errors})
        return {"ok": True, "current": name}

    @app.get("/items")
    def items(user=Depends(auth.require("member"))):
        with conn() as c:
            rows = c.execute("SELECT id, name, status, owner_id FROM items ORDER BY id").fetchall()
        return [dict(r) for r in rows]

    @app.post("/items")
    def add_item(body: dict, user=Depends(auth.require("operator"))):
        name = (body.get("name") or "").strip()
        if not name:
            raise HTTPException(status_code=422, detail="name 이 비었다")
        with conn() as c:
            cur = c.execute("INSERT INTO items (name, status, owner_id) VALUES (?, 'open', ?)", (name, int(body.get("owner_id") or 1)))
            new_id = cur.lastrowid
        return {"id": new_id}

    @app.post("/items/{item_id}/status")
    def change_status(item_id: int, body: StatusChange, user=Depends(auth.require("member"))):
        if body.status not in ds.STATUSES:
            raise HTTPException(status_code=422, detail="status 는 %s 가운데 하나" % "/".join(ds.STATUSES))
        with conn() as c:
            n = c.execute("UPDATE items SET status = ? WHERE id = ?", (body.status, item_id)).rowcount
        if n == 0:
            raise HTTPException(status_code=404, detail="없는 항목")
        return {"id": item_id, "status": body.status}

    @app.get("/settings")
    def get_settings(user=Depends(auth.require("member"))):
        with conn() as c:
            return {k: int(dbmod.get_setting(c, k, v)) for k, v in dbmod.DEFAULT_SETTINGS.items()}

    @app.post("/settings")
    def set_settings(body: Settings, user=Depends(auth.require("operator"))):
        with conn() as c:
            for k, v in body.dict().items():
                if v is not None:
                    dbmod.set_setting(c, k, v)
            return {k: int(dbmod.get_setting(c, k, v)) for k, v in dbmod.DEFAULT_SETTINGS.items()}

    @app.post("/recompute")
    def recompute(body: Params, user=Depends(auth.require("operator"))):
        with conn() as c:
            params = {k: int(dbmod.get_setting(c, k, v)) for k, v in dbmod.DEFAULT_SETTINGS.items()}
            params.update({k: v for k, v in body.dict().items() if v is not None})
            dataset = dbmod.current_dataset(c) or ""
            result = compute_mod.compute(params, dataset, c)
            c.execute("INSERT INTO results (dataset, params, body) VALUES (?, ?, ?)", (dataset, json.dumps(params), json.dumps(result, ensure_ascii=False)))
        return result

    @app.get("/export")
    def export(user=Depends(auth.require("member"))):
        """운영 계획을 JSON 으로. 저장창을 열지 않는다 (화면이 글로 보이고 복사한다)."""
        with conn() as c:
            dataset = dbmod.current_dataset(c)
            last = c.execute("SELECT body FROM results ORDER BY id DESC LIMIT 1").fetchone()
            rows = [dict(r) for r in c.execute("SELECT id, name, status, owner_id FROM items ORDER BY id").fetchall()]
        body = {"dataset": dataset, "items": rows, "result": json.loads(last["body"]) if last else None}
        return JSONResponse(content=body, media_type="application/json")

    static = os.path.join(HERE, "static")
    if os.path.isdir(static):
        @app.get("/")
        def index():
            return FileResponse(os.path.join(static, "index.html"))
        app.mount("/static", StaticFiles(directory=static), name="static")
    return app


app = create_app() if os.environ.get("SKELETON_NO_APP") is None else None  # uvicorn app:app 이 쓴다. 시험은 SKELETON_NO_APP=1 로 들여와 create_app 만 쓴다
