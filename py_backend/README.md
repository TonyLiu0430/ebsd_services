# Python 後端

此 FastAPI 服務負責使用者驗證、EBSD 檔案與樣品資料管理、分析，以及 PowerPoint 報告產生。服務會直接沿用 PostgreSQL 既有的 `users` 資料表，包括 UUID、使用者名稱及 bcrypt 密碼雜湊，不需要轉換帳號資料。

## 目錄結構

```text
app/
├── main.py              # FastAPI 組裝、啟動與停止流程
├── schemas.py           # API 請求資料格式
├── api/                 # HTTP 路由：auth、ebsd、reports、analysis
├── db/store.py          # 資料庫連線、資料表模型與既有結構升級
├── services/            # 登入驗證、EBSD 流程、報告工作與內容組裝
├── integrations/cpp.py  # C++ EBSD 後端的 HTTP 用戶端
├── analysis/            # 統計計算與 EBSD 圖形產生
└── reports/             # PowerPoint 版面與圖表輸出
assets/                  # 報告範本
tests/                   # 單元與整合測試
tools/                   # 範本與視覺比對維護工具
typings/                 # 第三方套件型別補充
```

API 路由只處理 HTTP 輸入與輸出；資料庫、外部 C++ 服務、報告工作和分析程式分別放在對應模組，避免功能互相循環引用。

## 本機啟動

在 `py_backend` 目錄執行：

```bash
uv sync --locked
JWT_SECRET='請改成安全的隨機金鑰' \
DATABASE_URL='postgresql+psycopg2://使用者:密碼@localhost:5432/資料庫' \
uv run uvicorn app.main:app --host 0.0.0.0 --port 8000
```

`JWT_SECRET` 為必要設定。登入工作階段有效 48 小時，Cookie 使用 HttpOnly 及 SameSite=Lax。HTTPS 部署請設定 `COOKIE_SECURE=true`；一般 HTTP 區域網路環境則使用 `false`。

正式環境通常由專案根目錄的 Docker Compose 啟動，不需要單獨執行上述指令。

## 執行測試

```bash
JWT_SECRET='test-only-secret' \
DATABASE_URL='sqlite:///:memory:' \
MPLCONFIGDIR='/tmp/ebsd-matplotlib' \
uv run python -m unittest discover -s tests -v
```

測試涵蓋既有 Go bcrypt 帳號登入與 UUID 保留、Cookie 工作階段、不同使用者的資料隔離、資料表升級、報告工作、分析結果和 PowerPoint 版面。

## 維護工具

- `tools/build_report_template.py`：維護 PowerPoint 報告範本。
- `tools/generate_ppt_visual_fixtures.py`：產生簡報視覺測試資料。
這些工具應從 `py_backend` 目錄執行，讓 `app` 套件與 `assets` 路徑都能正確解析。
