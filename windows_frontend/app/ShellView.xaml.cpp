#include "pch.h"
#include "ShellView.xaml.h"
#include "ShellView.g.cpp"
#include <shellapi.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::Web::WebView2::Core;
using namespace Windows::Foundation;

namespace winrt::EBSDDesktop::implementation {
ShellView::ShellView() {
    InitializeComponent();
}
void ShellView::require_ui_thread() const {
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    check_hresult(CoGetApartmentType(&type, &qualifier));
    if ((type != APTTYPE_STA && type != APTTYPE_MAINSTA) || !DispatcherQueue().HasThreadAccess())
        throw hresult_wrong_thread();
}
void ShellView::Root_Loaded(IInspectable const&, RoutedEventArgs const&) {
    if (started_ || closing_) return;
    started_ = true;
    startup_ = start_async();
}
IAsyncAction ShellView::start_async() {
    auto lifetime = get_strong();
    require_ui_thread();
    apartment_context ui;
    hstring failure;
    try {
        runtime_ = std::make_shared<ebsd::DesktopRuntime>();
        auto runtime = runtime_;
        co_await resume_background();
        runtime->start();
        co_await ui;
        require_ui_thread();
        if (closing_) co_return;
        auto environment = co_await CoreWebView2Environment::CreateWithOptionsAsync(
            runtime->webview_runtime().wstring(),
            (runtime->data_directory() / "webview").wstring(), nullptr);
        if (closing_) co_return;
        co_await Browser().EnsureCoreWebView2Async(environment);
        if (closing_) co_return;
        require_ui_thread();
        auto core = Browser().CoreWebView2();
        auto cookies = core.CookieManager();
        auto cookie = cookies.CreateCookie(to_hstring(ebsd::LocalServer::cookie_name),
            to_hstring(runtime->token()), L"127.0.0.1", L"/");
        cookie.IsHttpOnly(true);
        cookie.IsSecure(false); // HTTP is loopback-only; no TLS certificate required.
        cookie.SameSite(CoreWebView2CookieSameSiteKind::Strict);
        cookies.AddOrUpdateCookie(cookie);
        const auto origin = to_hstring(runtime->origin());
        core.NavigationStarting([origin](auto const&, CoreWebView2NavigationStartingEventArgs const& args) {
            const std::wstring uri(args.Uri());
            const std::wstring prefix = std::wstring(origin) + L"/";
            if (!uri.starts_with(prefix) && !uri.starts_with(L"blob:" + prefix)) args.Cancel(true);
        });
        core.NewWindowRequested([this, origin](auto const&, CoreWebView2NewWindowRequestedEventArgs const& args) {
            args.Handled(true);
            const std::wstring uri(args.Uri());
            if (uri.starts_with(std::wstring(origin) + L"/")) Browser().CoreWebView2().Navigate(args.Uri());
        });
        core.PermissionRequested([](auto const&, CoreWebView2PermissionRequestedEventArgs const& args) {
            args.State(CoreWebView2PermissionState::Deny);
        });
        core.Settings().AreDevToolsEnabled(false);
        core.Settings().AreDefaultContextMenusEnabled(false);
        core.NavigationCompleted([this](auto const&, CoreWebView2NavigationCompletedEventArgs const& args) {
            if (!args.IsSuccess() && !closing_) show_error(L"頁面載入失敗，請關閉後重新開啟應用程式。");
        });
        core.ProcessFailed([this](auto const&, auto const&) {
            if (!closing_) show_error(L"網頁顯示程序已停止，請關閉後重新開啟應用程式。");
        });
        // WebView2's default download UI handles the existing frontend's Blob
        // links and Content-Disposition downloads, including PPTX exports.
        Browser().Visibility(Visibility::Visible);
        StartupPanel().Visibility(Visibility::Collapsed);
        BusyRing().IsActive(false);
        ReloadButton().IsEnabled(true);
        StatusText().Text(L"本機工作區");
        core.Navigate(origin + L"/");
        monitor_.Interval(std::chrono::seconds(3));
        monitor_.Tick([this](auto const&, auto const&) {
            const auto failed = runtime_->failed_backend();
            if (!failed.empty()) {
                monitor_.Stop();
                show_error(to_hstring(failed + " service stopped. Open the data folder and inspect logs; restart the app to retry."));
            }
        });
        monitor_.Start();
    } catch (hresult_error const& exception) {
        failure = exception.message();
    } catch (std::exception const& exception) {
        failure = to_hstring(exception.what());
    }
    if (!failure.empty()) {
        co_await ui;
        if (!closing_) show_error(failure);
        auto runtime = runtime_;
        co_await resume_background();
        if (runtime) runtime->stop();
        // Release the coroutine's strong reference on the owning STA, too.
        co_await ui;
    }
}
void ShellView::show_error(hstring const& message) {
    Browser().Visibility(Visibility::Collapsed);
    StartupPanel().Visibility(Visibility::Visible);
    BusyRing().IsActive(false);
    ReloadButton().IsEnabled(false);
    StatusText().Text(L"無法繼續");
    StartupText().Text(L"請檢查執行環境後重新開啟");
    ErrorText().Text(message);
}
void ShellView::Reload_Click(IInspectable const&, RoutedEventArgs const&) {
    if (auto core = Browser().CoreWebView2()) core.Reload();
}
void ShellView::Data_Click(IInspectable const&, RoutedEventArgs const&) {
    if (runtime_) ShellExecuteW(nullptr, L"open", runtime_->data_directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
fire_and_forget ShellView::shutdown() {
    auto lifetime = get_strong();
    closing_ = true;
    monitor_.Stop();
    ReloadButton().IsEnabled(false);
    StatusText().Text(L"正在關閉…");
    apartment_context ui;
    auto runtime = runtime_;
    if (runtime) runtime->cancel();
    // Keep the island and dispatcher alive until startup has returned to the
    // STA. Closing WebView2 during EnsureCoreWebView2Async races its continuation.
    if (startup_) {
        try { co_await startup_; } catch (...) {}
        startup_ = nullptr;
    }
    require_ui_thread();
    Browser().Close();
    co_await resume_background();
    if (runtime) runtime->stop();
    co_await ui;
    PostMessageW(host_, WM_APP + 1, 0, 0);
}
}
