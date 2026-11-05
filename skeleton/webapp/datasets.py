"""데이터셋 묶음 (data/<이름>/ 아래 CSV 들) 의 목록, 검증, 적용.
검증은 실제 파일을 읽어 무엇이 잘못됐는지 줄 번호와 함께 알린다. 적용은 트랜잭션이라 실패하면 기존 데이터와 결과가 그대로다.
과제에 맞출 때는 FILES 의 파일과 열, check_refs 의 참조만 바꾼다."""
import csv
import io
import os

# 파일 이름 → 필수 열. 과제의 CSV 7종이면 여기를 늘린다
FILES = {
    "users.csv": ["id", "username", "role"],
    "items.csv": ["id", "name", "status", "owner_id"],
}
STATUSES = ("open", "closed")


def list_datasets(data_dir):
    if not os.path.isdir(data_dir):
        return []
    return sorted(d for d in os.listdir(data_dir) if os.path.isdir(os.path.join(data_dir, d)))


def read_csv(path):
    with io.open(path, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    return rows


def validate(data_dir, name):
    """(ok, errors, tables). errors 는 사람이 읽을 문장 (파일, 줄, 무엇이 잘못됐나)."""
    errors = []
    tables = {}
    if name not in list_datasets(data_dir):  # data 폴더 바로 아래의 묶음 이름만 (.. 같은 이름으로 다른 폴더를 읽지 않게)
        return False, ["묶음이 없다: %s" % name], {}
    folder = os.path.join(data_dir, name)
    for fname, cols in FILES.items():
        path = os.path.join(folder, fname)
        if not os.path.isfile(path):
            errors.append("%s: 파일이 없다" % fname)
            continue
        try:
            rows = read_csv(path)
        except (OSError, UnicodeDecodeError, csv.Error) as e:
            errors.append("%s: 읽지 못함 (%s)" % (fname, e))
            continue
        header = list(rows[0].keys()) if rows else []
        if not rows:
            with io.open(path, encoding="utf-8-sig", newline="") as f:
                header = next(csv.reader(f), [])
        missing = [c for c in cols if c not in header]
        if missing:
            errors.append("%s: 필수 열 누락 %s" % (fname, ", ".join(missing)))
            continue
        if not rows:  # 머리 줄만 있는 파일을 적용하면 기존 데이터가 모두 지워진다
            errors.append("%s: 데이터 줄이 없다 (머리 줄만 있다)" % fname)
            continue
        for i, row in enumerate(rows, start=2):  # 1행은 머리
            for c in cols:
                if row.get(c) is None or row[c].strip() == "":
                    errors.append("%s %d행: %s 가 비었다" % (fname, i, c))
            if "id" in cols and not (row.get("id") or "").strip().isdigit():
                errors.append("%s %d행: id 가 정수가 아니다 (%s)" % (fname, i, row.get("id")))
        tables[fname] = rows
    if not errors:
        errors.extend(check_refs(tables))
    return (not errors), errors, tables


def check_refs(tables):
    """참조 검사: users 의 id 와 username 은 겹치지 않고, items.owner_id 는 users.id 에 있어야 하고, items.status 는 STATUSES 안이어야 한다."""
    errors = []
    user_ids, names = set(), set()
    for i, row in enumerate(tables.get("users.csv", []), start=2):
        uid, uname = row["id"].strip(), row["username"].strip()
        if uid in user_ids:
            errors.append("users.csv %d행: id %s 가 중복이다" % (i, uid))
        if uname in names:
            errors.append("users.csv %d행: username %s 가 중복이다" % (i, uname))
        user_ids.add(uid)
        names.add(uname)
    seen = set()
    for i, row in enumerate(tables.get("items.csv", []), start=2):
        if row["owner_id"].strip() not in user_ids:
            errors.append("items.csv %d행: owner_id %s 가 users.csv 에 없다" % (i, row["owner_id"]))
        if row["status"].strip() not in STATUSES:
            errors.append("items.csv %d행: status %s 는 %s 가운데 하나여야 한다" % (i, row["status"], "/".join(STATUSES)))
        if row["id"].strip() in seen:
            errors.append("items.csv %d행: id %s 가 중복이다" % (i, row["id"]))
        seen.add(row["id"].strip())
    return errors


def apply(conn, data_dir, name):
    """검증을 통과하면 users 와 items 를 이 묶음으로 바꾸고 현재 묶음을 적는다. 실패하면 아무것도 바꾸지 않는다.
    users 는 자료다. 로그인 계정 (db.accounts) 은 건드리지 않는다."""
    ok, errors, tables = validate(data_dir, name)
    if not ok:
        return False, errors
    with conn:  # 트랜잭션. 중간에 오류가 나면 되돌아간다
        conn.execute("DELETE FROM items")
        conn.execute("DELETE FROM users")
        for r in tables["users.csv"]:
            conn.execute("INSERT INTO users (id, username, role) VALUES (?, ?, ?)",
                         (int(r["id"]), r["username"].strip(), r["role"].strip()))
        for r in tables["items.csv"]:
            conn.execute("INSERT INTO items (id, name, status, owner_id) VALUES (?, ?, ?, ?)",
                         (int(r["id"]), r["name"].strip(), r["status"].strip(), int(r["owner_id"])))
        conn.execute("INSERT INTO datasets (key, value) VALUES ('current', ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value", (name,))
        conn.execute("DELETE FROM results")  # 적용 뒤 재계산 전의 옛 결과를 새 데이터의 결과로 보이지 않게
    return True, []
