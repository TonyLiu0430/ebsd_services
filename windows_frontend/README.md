# EBSD Desktop（Windows x64）

獨立的 Windows 發行入口。C++／WinRT、Win32 HWND 與訊息迴圈承載 **WinUI 3 XAML Islands**；UI 寫在 `app/ShellView.xaml`，WebView2 顯示現有 Vue 網頁。`App.xaml` 只提供 XAML 資源與型別資訊，不建立 WinUI 頂層 Window，也不呼叫 `Application::Start`。

程式使用 `asInvoker`、`WindowsPackageType=None`、非 AppContainer，沒有 MSIX 註冊或管理員權限需求。Windows App SDK 隨建置輸出部署；WebView2 Runtime、VC++ Runtime 與分析依賴仍需備妥。XAML Islands 不會繞過 Windows 檔案權限。

## 執行架構

```text
Win32 host → DesktopWindowXamlSource → ShellView.xaml → WebView2
                                                     │ session Cookie
                                                     ▼
                         本機 HTTP gateway（127.0.0.1，動態 port）
                         ├─ /、/report… → Vue 靜態檔
                         ├─ /goapi/user/* → 本機使用者
                         ├─ /api/* → Python FastAPI → SQLite
                         └─ /cppapi/* → C++／EBSDLib

Python → GO_BACKEND_URL/user/check → 獨立本機驗證 listener
Python → 隨附 node.exe → 原有共用圖表 renderer → CairoSVG → PPTX
```

不啟動 Go、PostgreSQL、Caddy 或 Nuxt server。現有 Vue 和 Python 原始碼不需改動，Docker 的部署流程維持原樣。C++ backend 額外支援 `EBSD_BIND_HOST`、`EBSD_PORT` 環境變數；沒設定時仍使用 `0.0.0.0:8080`。

WebView2 啟動時由宿主放入 HttpOnly／SameSite=Strict 的隨機 session Cookie；Python 會將同一個 Cookie 轉送驗證服務。沒有公開的 token 發放端點。驗證 listener 使用獨立 worker pool，避免分析請求等待 Python 時耗盡回呼的工作執行緒。

`/goapi/user/login`、`register` 是單機相容路由，不儲存密碼或建立新帳號。`logout` 回傳成功，但本機工作區身分仍有效；原本登入頁會顯示已登入，可按「回分析頁」。這不是多帳號模式。

## 建置

開發工具：Windows x64、Visual Studio 2022（v143 C++、Windows SDK 10.0.19041 或更新、WinUI／C++/WinRT 建置元件）、CMake 3.24+、可連線的 NuGet。建議從 Windows 本機磁碟的 checkout 建置。

```powershell
cd windows_frontend
cmake --preset windows-x64
cmake --build --preset windows-release
ctest --preset windows-release
```

輸出位於 `build/windows-x64/app/Release/`。CMake 產生 `winui/EBSDDesktop.vcxproj`，呼叫 MSBuild `/restore`；NuGet 固定 Windows App SDK 與 C++/WinRT 版本。XAML／MIDL／PRI 使用 Microsoft 的正式 MSBuild targets，而非手寫 XAML 編譯流程。HTTP 與 JSON 的 header-only 依賴由 CMake 下載固定版本並驗證 SHA-256。

產生共用前端靜態檔：

```powershell
cd ../frontend
bun install --frozen-lockfile
node node_modules/nuxt/bin/nuxt.mjs generate
```

如果 CMake 找得到 Bun，也可使用 `cmake --build build/windows-x64 --target web_assets`。這只產生 `.nuxt`／`.output`；不改 Vue 原始檔。

## 組合執行環境

外殼編譯成功不等於分析 runtime 已齊全。先準備：

- Windows x64 的 `ebsd_cpp_backend.exe`，包含 EbsdLib／HDF5／TBB 等實際需要的 DLL，並使用本次新增的 host／port 設定。
- 可搬移的 Python 3.9 發行目錄，已依 `ML_backend/uv.lock` 安裝依賴；不要直接搬一般 venv。若使用 Python embeddable distribution，需配置 `._pth` 的 site-packages／`import site`。
- Node 24 的 `node.exe`，與 Docker 報表 runtime 的 major 版本一致。
- Cairo 及其依賴 DLL，可放在 Python 搜尋路徑或 `--native` 指定目錄。
- WebView2 Evergreen Runtime；離線部署可傳入完整的 Fixed Version runtime 目錄。

```powershell
py windows_frontend/tools/stage_runtime.py `
  --app windows_frontend/build/windows-x64/app/Release `
  --python C:/ebsd-runtime/python `
  --node C:/ebsd-runtime/node/node.exe `
  --cpp C:/ebsd-runtime/cpp `
  --native C:/ebsd-runtime/cairo `
  --output windows_frontend/dist/EBSDDesktop
```

可加 `--webview C:/ebsd-runtime/webview2`。輸出目錄須尚未存在；腳本不覆蓋既有發行版。腳本複製 runtime，不替你安裝或編譯科學運算套件。

```text
EBSDDesktop/
  EBSDDesktop.exe、resources.pri、Windows App SDK 的建置輸出
  desktop.json
  web/                       Nuxt 靜態輸出
  runtime/python/            python.exe、已安裝的套件
  runtime/node/node.exe
  runtime/cpp/               backend exe 與 DLL
  runtime/native/            Cairo 等 DLL
  backend/ML_backend/        Python 原始碼與 PPT 模板
  backend/frontend/          原本的 JS renderer、chartScale 與 package.json
```

保留 `backend/ML_backend` 和 `backend/frontend` 的相鄰目錄關係，因為現有 Python 報表程式依此定位 JS renderer。宿主將隨附 Node 加入 **子程序** PATH，不修改系統 PATH，也不需要使用者自行安裝 Node。

`desktop.json` 的相對路徑以 exe 所在目錄為基準，亦接受絕對路徑供開發。讀寫資料位於 `%LOCALAPPDATA%/EBSDDesktop`：

- `user.json`：持久化本機使用者 UUID；與 `ebsd_store.db` 一起備份。
- `ebsd_store.db`：SQLite 資料庫。
- `logs/python.log`、`logs/cpp.log`：後端輸出。
- `logs/host.log`：原生宿主啟動失敗的階段與 HRESULT。
- `ppt-jobs`、`matplotlib`、`numba`、`webview`：使用者可寫的工作／快取目錄。

同一份資料只允許一個 app instance。後端放入 Windows Job Object，宿主結束時清除整個程序樹；關閉程式會中斷未完成的分析與匯出。啟動會等後端就緒，程序意外退出會在 XAML 顯示錯誤。程式不自動重啟失敗工作，避免重複執行分析。

## 測試與限制

STA 初始化順序為 COM STA → DispatcherQueue → App（只建立 metadata provider）→ WindowsXamlManager → App.xaml 資源 → XAML Island。`App` 隱藏公開的 `InitializeComponent`，避免 C++/WinRT 的 `make_self` 在 XAML core 初始化前自動呼叫它。WebView2 與 Cookie 操作在 UI STA；關閉時先等候啟動協程完成，再釋放 island 和 dispatcher。

在 Windows 互動桌面，用尚未組包的建置輸出驗證 XAML 顯示與正常關閉：

```powershell
powershell -NoProfile -File windows_frontend/tests/smoke_islands.ps1 `
  -Executable windows_frontend/build/windows-x64/app/Release/EBSDDesktop.exe
```

此測試預期看到缺少分析 runtime 的 XAML 提示，不會執行完整分析。

已在 Windows 11／Visual Studio 2022 實際通過 Release 編譯、上述 XAML Island 啟動／關閉測試，以及 Windows gateway contract 測試。Linux gateway 測試、組包後的 Node renderer 測試也通過；這些不代表完整 Windows 分析 runtime 已驗證。

跨平台 gateway contract 測試：

```bash
cmake -S windows_frontend -B /tmp/ebsd-desktop-tests -G Ninja -DEBSD_BUILD_WINUI=OFF
cmake --build /tmp/ebsd-desktop-tests
ctest --test-dir /tmp/ebsd-desktop-tests --output-on-failure
python3 -m unittest discover -s windows_frontend/tests -p 'test_*.py' -v
```

離線環境可以指定 `EBSD_HTTPLIB_INCLUDE_DIR`、`EBSD_JSON_INCLUDE_DIR`，分別指向含 `httplib.h` 與 `nlohmann/json.hpp` 的目錄。

測試涵蓋 Cookie、Python 回呼、並行驗證、JSON／multipart／二進位資料、下載標頭、錯誤狀態、靜態頁、路徑、Host／Origin 與上傳上限；staging 測試會實際執行打包後的共用 JS renderer。

上傳和回應目前在 gateway 記憶體中緩衝；`max_upload_mib` 預設 1024，四個 gateway workers。大量大型資料並行時需要足夠 RAM，後續可改成串流轉送。不能把此單機 gateway 當作 LAN 伺服器使用。

正式交付仍需在乾淨 Windows 機器驗證完整 runtime：CPR/CRC、CTF、ANG、圖表與 Docker 的一致性、PPTX 下載、中文路徑、離線啟動、DPI／Tab／輸入法、重新開啟後資料保存、關閉後無殘留子程序。此目錄目前提供外殼和組包流程，不包含安裝器、自動更新或已驗證的完整 Windows 科學運算發行包。

參考：[Microsoft XAML Islands](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/host-controls-existing-desktop-apps)、[Windows App SDK C++ 部署範例](https://github.com/microsoft/WindowsAppSDK-Samples/tree/main/Samples/SelfContainedDeployment)、[WebView2 部署](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)。
