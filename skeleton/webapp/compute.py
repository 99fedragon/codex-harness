"""재계산의 빈 자리. 여기에 과제의 엔진을 끼운다.
compute(params, dataset, conn) -> dict. 받은 조건과 현재 데이터로 결과를 만들어 돌려준다 (지금은 입력과 건수를 그대로)."""


def compute(params, dataset, conn):
    n_items = conn.execute("SELECT COUNT(*) AS n FROM items").fetchone()["n"]
    n_open = conn.execute("SELECT COUNT(*) AS n FROM items WHERE status = 'open'").fetchone()["n"]
    return {
        "dataset": dataset,
        "params": params,
        "summary": {"items": n_items, "open": n_open},
        "rows": [],  # 과제의 결과 줄 (예: 항목마다 배정) 을 여기에
    }
