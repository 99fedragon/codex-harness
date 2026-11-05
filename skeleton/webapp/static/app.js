/* 화면 한 벌. 규칙: 새 창, 팝업, alert, confirm 없음 (화면 안의 글로). 상태를 바꾼 뒤에는 서버에서 다시 읽어 그린다.
   토큰은 메모리에 두고 sessionStorage 에도 적는다 (다른 사이트 안의 미리보기 창처럼 저장소가 막혀도 메모리로 돈다).
   주소는 앞에 / 없는 상대 경로로 쓴다 (미리보기가 /proxy/8000/ 같은 경로 뒤에 떠도 같은 서버로 간다. api 가 앞의 / 를 떼어 준다) */
(function () {
  "use strict";
  var store = (function () {
    var mem = Object.create(null);
    function session() { try { return window.sessionStorage || null; } catch (e) { return null; } }
    return {
      get: function (k) {
        var v = null;
        try { var s = session(); if (s) v = s.getItem(k); } catch (e) { v = null; }
        return v !== null ? v : (k in mem ? mem[k] : null);
      },
      set: function (k, v) { mem[k] = v; try { var s = session(); if (s) s.setItem(k, v); } catch (e) { /* 메모리만 */ } },
      clear: function () { mem = Object.create(null); try { var s = session(); if (s) s.clear(); } catch (e) { /* 메모리만 */ } }
    };
  })();
  var token = store.get("token") || "";
  var role = store.get("role") || "";
  var $ = function (id) { return document.getElementById(id); };

  function show(el, on) { el.classList.toggle("hidden", !on); }
  function toast(msg, isError) {
    var t = $("toast"); t.textContent = msg; t.classList.toggle("error", !!isError); show(t, true);
    clearTimeout(toast.timer); toast.timer = setTimeout(function () { show(t, false); }, 3000);
  }
  function api(method, path, body) {
    show($("busy"), true);
    var opt = { method: method, headers: { "Content-Type": "application/json" } };
    if (token) opt.headers["Authorization"] = "Bearer " + token;
    if (body !== undefined) opt.body = JSON.stringify(body);
    return fetch(String(path).replace(/^\/+/, ""), opt).then(function (r) {
      return r.text().then(function (text) {
        var data = null; try { data = text ? JSON.parse(text) : null; } catch (e) { data = { detail: text }; }
        if (!r.ok) { var err = new Error(detailText(data)); err.status = r.status; err.data = data; throw err; }
        return data;
      });
    }).finally(function () { show($("busy"), false); });
  }
  function detailText(data) {
    if (!data) return "오류";
    var d = data.detail;
    if (typeof d === "string") return d;
    if (d && d.message) return d.message;
    return JSON.stringify(d || data);
  }

  // 로그인과 역할별 화면
  function render() {
    var logged = !!token;
    show($("login"), !logged);
    show($("operator"), logged && role === "operator");
    show($("member"), logged && role !== "operator");
    show($("logout"), logged);
    $("who").textContent = logged ? (store.get("username") + " (" + role + ")") : "";
    if (logged) { loadDatasets(); loadItems(); loadSettings(); }
  }
  $("login-form").addEventListener("submit", function (e) {
    e.preventDefault();
    var f = e.target;
    api("POST", "login", { username: f.account.value, password: f.secret.value }).then(function (d) {
      token = d.token; role = d.role;
      store.set("token", token); store.set("role", role); store.set("username", d.username);
      $("login-error").textContent = ""; render();
    }).catch(function (err) { $("login-error").textContent = err.message; });
  });
  $("logout").addEventListener("click", function () {
    token = ""; role = ""; store.clear(); render();
  });

  // 탭
  document.querySelectorAll(".tabs").forEach(function (nav) {
    nav.addEventListener("click", function (e) {
      var b = e.target.closest("button[data-tab]"); if (!b) return;
      nav.querySelectorAll("button").forEach(function (x) { x.classList.toggle("active", x === b); });
      nav.parentElement.querySelectorAll(".panel").forEach(function (p) { show(p, p.id === "tab-" + b.dataset.tab); });
    });
  });

  // 데이터셋
  function loadDatasets() {
    if (role !== "operator") return;
    api("GET", "datasets").then(function (d) {
      $("ds-current").textContent = d.current || "-";
      var sel = $("ds-select"); sel.innerHTML = "";
      d.available.forEach(function (n) { var o = document.createElement("option"); o.value = n; o.textContent = n; sel.appendChild(o); });
    }).catch(function (err) { toast(err.message, true); });
  }
  function showErrors(list) {
    var ul = $("ds-errors"); ul.innerHTML = "";
    (list || []).forEach(function (e) { var li = document.createElement("li"); li.textContent = e; ul.appendChild(li); });
  }
  $("ds-validate").addEventListener("click", function () {
    api("POST", "datasets/" + encodeURIComponent($("ds-select").value) + "/validate").then(function (d) {
      showErrors(d.errors); $("ds-msg").textContent = d.ok ? "검증 통과" : "검증 실패 (" + d.errors.length + "곳)";
    }).catch(function (err) { toast(err.message, true); });
  });
  $("ds-apply").addEventListener("click", function () {
    api("POST", "datasets/" + encodeURIComponent($("ds-select").value) + "/apply").then(function (d) {
      showErrors([]); $("ds-msg").textContent = "적용했다: " + d.current; loadDatasets(); loadItems();
      $("results-body").textContent = ""; show($("results-body"), false); show($("results-empty"), true);
    }).catch(function (err) {
      showErrors(err.data && err.data.detail && err.data.detail.errors);
      $("ds-msg").textContent = err.message; loadDatasets();
    });
  });

  // 조건과 재계산
  function loadSettings() {
    api("GET", "settings").then(function (s) {
      var f = $("params-form"); f.capacity.value = s.capacity; f.margin_minutes.value = s.margin_minutes;
    }).catch(function () {});
  }
  $("params-form").addEventListener("submit", function (e) {
    e.preventDefault();
    var f = e.target; var body = { capacity: Number(f.capacity.value), margin_minutes: Number(f.margin_minutes.value) };
    api("POST", "settings", body).then(function () { return api("POST", "recompute", body); }).then(function (r) {
      $("params-msg").textContent = "계산했다";
      $("results-body").textContent = JSON.stringify(r, null, 2); show($("results-body"), true); show($("results-empty"), false);
    }).catch(function (err) { $("params-msg").textContent = err.message; });
  });

  // 내보내기 (저장창 없이 글로)
  $("export-load").addEventListener("click", function () {
    api("GET", "export").then(function (d) { $("export-body").textContent = JSON.stringify(d, null, 2); }).catch(function (err) { toast(err.message, true); });
  });
  $("export-copy").addEventListener("click", function () {
    var text = $("export-body").textContent; if (!text) { toast("먼저 불러온다"); return; }
    if (navigator.clipboard) navigator.clipboard.writeText(text).then(function () { toast("복사했다"); }, function () { toast("복사 못 함. 글을 골라 복사한다", true); });
    else toast("글을 골라 복사한다");
  });

  // 항목과 상태 바꾸기 (누른 뒤 서버에서 다시 읽는다)
  function loadItems() {
    api("GET", "items").then(function (rows) {
      var tb = $("items-table").querySelector("tbody"); tb.innerHTML = "";
      show($("items-empty"), rows.length === 0);
      rows.forEach(function (r) {
        var tr = document.createElement("tr");
        tr.innerHTML = "<td></td><td></td><td></td><td></td>";
        tr.children[0].textContent = r.id; tr.children[1].textContent = r.name; tr.children[2].textContent = r.status;
        var b = document.createElement("button"); b.type = "button"; b.textContent = r.status === "open" ? "닫기" : "다시 열기";
        b.addEventListener("click", function () {
          api("POST", "items/" + r.id + "/status", { status: r.status === "open" ? "closed" : "open" })
            .then(loadItems).catch(function (err) { toast(err.message, true); });
        });
        tr.children[3].appendChild(b); tb.appendChild(tr);
      });
    }).catch(function (err) { if (err.status === 401) { token = ""; store.clear(); render(); } else toast(err.message, true); });
  }
  $("member-settings").addEventListener("submit", function (e) {
    e.preventDefault(); $("member-settings-msg").textContent = "저장했다 (화면 안 설정)";
  });

  render();
})();
