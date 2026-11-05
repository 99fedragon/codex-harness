"""SQLite 저장소. 빈 DB 에서 시작해도 스키마와 로그인 계정이 준비된다. 서비스를 다시 켜도 남는다.
로그인 계정 (accounts) 과 데이터셋의 사용자 (users) 는 다른 표다. 묶음을 적용해도 계정은 그대로이고, 묶음의 users 로는 로그인하지 않는다."""
import hashlib
import hmac
import os
import secrets
import sqlite3

SCHEMA = """
CREATE TABLE IF NOT EXISTS accounts (username TEXT PRIMARY KEY, role TEXT NOT NULL, salt TEXT NOT NULL, hash TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE NOT NULL, role TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS items (
  id INTEGER PRIMARY KEY, name TEXT NOT NULL, status TEXT NOT NULL, owner_id INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS datasets (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS results (id INTEGER PRIMARY KEY AUTOINCREMENT, dataset TEXT NOT NULL, params TEXT NOT NULL, body TEXT NOT NULL,
  made_at TEXT NOT NULL DEFAULT (datetime('now')));
"""

DEFAULT_SETTINGS = {"capacity": "10", "margin_minutes": "5"}


def connect(db_path):
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init_db(db_path, admin_password, member_password):
    """스키마와 로그인 계정 둘 (operator, member). 계정의 비밀번호는 켤 때마다 환경 변수 값으로 맞춘다 (해시로 저장)."""
    d = os.path.dirname(os.path.abspath(db_path))
    os.makedirs(d, exist_ok=True)
    conn = connect(db_path)
    with conn:
        conn.executescript(SCHEMA)
        set_account(conn, "operator", "operator", admin_password)
        set_account(conn, "member", "member", member_password)
        for k, v in DEFAULT_SETTINGS.items():
            conn.execute("INSERT OR IGNORE INTO settings (key, value) VALUES (?, ?)", (k, v))
    conn.close()


def hash_password(password, salt):
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt.encode("utf-8"), 100000).hex()


def set_account(conn, username, role, password):
    salt = secrets.token_hex(8)
    conn.execute("INSERT INTO accounts (username, role, salt, hash) VALUES (?, ?, ?, ?) "
                 "ON CONFLICT(username) DO UPDATE SET role = excluded.role, salt = excluded.salt, hash = excluded.hash",
                 (username, role, salt, hash_password(password, salt)))


def check_account(conn, username, password):
    """아이디와 비밀번호가 맞으면 {"username", "role"}, 아니면 None."""
    row = conn.execute("SELECT username, role, salt, hash FROM accounts WHERE username = ?", (username,)).fetchone()
    if not row or not hmac.compare_digest(row["hash"], hash_password(password, row["salt"])):
        return None
    return {"username": row["username"], "role": row["role"]}


def app_secret(conn):
    """토큰 서명 비밀값. 처음 켤 때 무작위로 만들어 DB 에 두고, 다시 켜도 같은 값을 쓴다 (환경 변수 SECRET 이 있으면 그쪽)."""
    row = conn.execute("SELECT value FROM meta WHERE key = 'secret'").fetchone()
    if row:
        return row["value"]
    value = secrets.token_hex(32)
    conn.execute("INSERT INTO meta (key, value) VALUES ('secret', ?)", (value,))
    return value


def get_setting(conn, key, default=None):
    row = conn.execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
    return row["value"] if row else default


def set_setting(conn, key, value):
    conn.execute("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value", (key, str(value)))


def current_dataset(conn):
    row = conn.execute("SELECT value FROM datasets WHERE key = 'current'").fetchone()
    return row["value"] if row else None
