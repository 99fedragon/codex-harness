"""미리보기 점검: 서버를 띄우고 로그인 → 토큰으로 요청 → 다시 요청 (새로고침 흉내) 이 200 인지 본다. 끝나면 서버를 끈다.
돌리기: python check_preview.py [포트]   (기본 8765). 통과하면 「미리보기 점검: 통과」, 아니면 「실패: 까닭」 과 끝 코드 1
브라우저의 미리보기 창에서는 사람이 한 번 더 본다: 로그인 → 새로고침 → 그대로 들어가 있나."""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))


def call(url, data=None, token=None):
    req = urllib.request.Request(url, data=json.dumps(data).encode("utf-8") if data is not None else None,
                                 headers={"Content-Type": "application/json"}, method="POST" if data is not None else "GET")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(req, timeout=5) as r:
        return r.status, json.loads(r.read().decode("utf-8") or "null")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    env = dict(os.environ)
    env.setdefault("PORT", str(port))
    env.setdefault("ADMIN_PASSWORD", "operator")
    env.setdefault("DB_PATH", os.path.join(HERE, "preview_check.db"))
    proc = subprocess.Popen([sys.executable, "-m", "uvicorn", "app:app", "--port", str(port), "--host", "127.0.0.1"],
                            cwd=HERE, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = "http://127.0.0.1:%d" % port
    try:
        for _ in range(50):
            try:
                if call(base + "/health")[0] == 200:
                    break
            except (urllib.error.URLError, ConnectionError, OSError):
                time.sleep(0.2)
        else:
            print("실패: 서버가 5초 안에 뜨지 않았다")
            return 1
        _, login = call(base + "/login", {"username": "operator", "password": env["ADMIN_PASSWORD"]})
        token = login["token"]
        s1, _ = call(base + "/items", token=token)
        s2, _ = call(base + "/items", token=token)  # 새로고침 흉내: 같은 토큰으로 다시
        try:
            call(base + "/items")
            print("실패: 토큰 없이도 들어간다")
            return 1
        except urllib.error.HTTPError as e:
            if e.code != 401:
                print("실패: 토큰 없는 요청이 %d (401 이어야 한다)" % e.code)
                return 1
        if s1 == 200 and s2 == 200:
            print("미리보기 점검: 통과 (로그인 → 요청 → 다시 요청 200, 토큰 없으면 401)")
            return 0
        print("실패: 요청이 %d, %d" % (s1, s2))
        return 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        try:
            os.remove(env["DB_PATH"])
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
