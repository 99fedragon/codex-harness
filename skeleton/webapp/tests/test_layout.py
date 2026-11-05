"""화면 규칙 검사: 새 창과 팝업 없음, 단추 최소 크기와 간격, 좁은 창에서 탭 줄 바꿈. 브라우저 없이 파일을 읽어 본다."""
import io
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(name):
    with io.open(os.path.join(ROOT, "static", name), encoding="utf-8") as f:
        return f.read()


class Layout(unittest.TestCase):
    def test_no_popups_or_file_dialogs(self):
        text = read("index.html") + read("app.js")
        for s in ("window.open", "alert(", "confirm(", 'target="_blank"', 'type="file"', "download="):
            self.assertNotIn(s, text, s)

    def test_no_password_save_prompt(self):
        # type=password 칸이 있으면 로그인 뒤 브라우저가 비밀번호 저장 창을 띄워 전체 화면이 풀린다
        text = read("index.html") + read("app.js")
        for s in ('type="password"', "type='password'", "current-password", "new-password", 'name="username"', 'name="password"'):
            self.assertNotIn(s, text, s)  # username/password 라는 칸 이름도 브라우저가 로그인 칸으로 알아본다
        self.assertRegex(read("theme.css"), r"input\.secret\s*\{[^}]*-webkit-text-security:\s*disc")

    def test_buttons_have_min_size_and_gap(self):
        css = read("theme.css")
        self.assertRegex(css, r"button\s*\{[^}]*min-height:\s*36px")
        self.assertRegex(css, r"gap:\s*8px")

    def test_tabs_wrap_on_narrow_screens(self):
        css = read("theme.css")
        self.assertRegex(css, r"\.tabs\s*\{[^}]*flex-wrap:\s*wrap")
        self.assertIn("@media (max-width: 800px)", css)


if __name__ == "__main__":
    unittest.main()
