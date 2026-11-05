"""뼈대의 시험 (넘길 때 함께 둔다). 돌리기: python -m unittest tests.test_app  (뼈대 폴더에서)"""
import gc
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
os.environ["SKELETON_NO_APP"] = "1"  # 들여올 때 모듈 수준 app 을 만들지 않는다

from fastapi.testclient import TestClient  # noqa: E402

import app as appmod  # noqa: E402


class App(unittest.TestCase):
    def setUp(self):
        # 임시 자료와 DB 는 뼈대 폴더 안 .tmp_tests/ 에 (세션 폴더 밖 시스템 임시 폴더에 쓰면 자동 검토가 「밖 쓰기」 로 짚는다)
        base = os.path.join(ROOT, ".tmp_tests")
        os.makedirs(base, exist_ok=True)
        self.tmp = tempfile.mkdtemp(dir=base)
        self.data = os.path.join(self.tmp, "data")
        shutil.copytree(os.path.join(ROOT, "data"), self.data)
        os.environ.setdefault("ADMIN_PASSWORD", "operator")
        self.c = TestClient(appmod.create_app(db_path=os.path.join(self.tmp, "t.db"), data_dir=self.data))
        r = self.c.post("/login", json={"username": "operator", "password": os.environ["ADMIN_PASSWORD"]})
        self.op = {"Authorization": "Bearer " + r.json()["token"]}

    def tearDown(self):
        self.c.close()
        gc.collect()  # 오류 난 요청이 잡고 있던 DB 연결을 닫는다 (Windows 는 열린 파일을 지우지 못한다)
        shutil.rmtree(self.tmp, ignore_errors=True)
        try:
            os.rmdir(os.path.dirname(self.tmp))  # 비었으면 .tmp_tests 도 지운다
        except OSError:
            pass

    def test_health(self):
        self.assertEqual("ok", self.c.get("/health").json()["status"])

    def test_login_required(self):
        self.assertEqual(401, self.c.get("/items").status_code)

    def test_bad_dataset_keeps_old(self):
        d = os.path.join(self.data, "bad")
        os.makedirs(d)
        with open(os.path.join(d, "items.csv"), "w", encoding="utf-8") as f:
            f.write("id,name\n1,a\n")
        with open(os.path.join(d, "users.csv"), "w", encoding="utf-8") as f:
            f.write("id,username,role\n1,x,member\n")
        self.assertEqual(400, self.c.post("/datasets/bad/apply", headers=self.op).status_code)
        self.assertEqual("sample", self.c.get("/datasets", headers=self.op).json()["current"])

    def put(self, name, items, users):
        d = os.path.join(self.data, name)
        os.makedirs(d)
        for fname, text in (("items.csv", items), ("users.csv", users)):
            with open(os.path.join(d, fname), "w", encoding="utf-8") as f:
                f.write(text)

    def test_header_only_dataset_is_rejected(self):
        self.put("empty", "id,name,status,owner_id\n", "id,username,role\n")
        self.assertEqual(400, self.c.post("/datasets/empty/apply", headers=self.op).status_code)
        self.assertEqual("sample", self.c.get("/datasets", headers=self.op).json()["current"])

    def test_apply_keeps_login_accounts(self):
        self.put("people", "id,name,status,owner_id\n5,five,open,1\n", "id,username,role\n1,kim,member\n")
        self.assertEqual(200, self.c.post("/datasets/people/apply", headers=self.op).status_code)
        r = self.c.post("/login", json={"username": "operator", "password": os.environ["ADMIN_PASSWORD"]})
        self.assertEqual(200, r.status_code)

    def test_status_round_trip(self):
        first = self.c.get("/items", headers=self.op).json()[0]["id"]
        self.c.post("/items/%d/status" % first, json={"status": "closed"}, headers=self.op)
        self.c.post("/items/%d/status" % first, json={"status": "open"}, headers=self.op)
        self.assertEqual("open", [i for i in self.c.get("/items", headers=self.op).json() if i["id"] == first][0]["status"])


if __name__ == "__main__":
    unittest.main()
