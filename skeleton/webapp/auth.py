"""토큰 로그인. 쿠키를 쓰지 않는다 (미리보기 창이 다른 도메인 안에 뜨면 쿠키가 안 보내진다). 요청 머리 Authorization: Bearer <토큰>."""
import hashlib
import hmac
import time

from fastapi import Depends, HTTPException, Request


def make_token(secret, username, role, issued=None):
    issued = str(int(issued if issued is not None else time.time()))
    msg = "%s|%s|%s" % (username, role, issued)
    sig = hmac.new(secret.encode("utf-8"), msg.encode("utf-8"), hashlib.sha256).hexdigest()
    return "%s|%s" % (msg, sig)


def parse_token(secret, token):
    """토큰 → {"username", "role"}. 서명이 다르면 None."""
    try:
        username, role, issued, sig = token.split("|")
    except ValueError:
        return None
    msg = "%s|%s|%s" % (username, role, issued)
    want = hmac.new(secret.encode("utf-8"), msg.encode("utf-8"), hashlib.sha256).hexdigest()
    if not hmac.compare_digest(want, sig):
        return None
    return {"username": username, "role": role}


def current_user(request: Request):
    auth = request.headers.get("authorization", "")
    if not auth.lower().startswith("bearer "):
        raise HTTPException(status_code=401, detail="로그인이 필요하다 (Authorization: Bearer <토큰>)")
    user = parse_token(request.app.state.secret, auth[7:].strip())
    if not user:
        raise HTTPException(status_code=401, detail="토큰이 맞지 않는다")
    return user


def require(role):
    """역할 의존성: require("operator") 는 운영자만, require("member") 는 누구나 (로그인만)."""
    def dep(user=Depends(current_user)):
        if role == "operator" and user["role"] != "operator":
            raise HTTPException(status_code=403, detail="운영자만 할 수 있다")
        return user
    return dep
