#include "pch.h"
#include "App.xaml.h"
#include "ShellView.xaml.h"
#include <winrt/Microsoft.UI.Interop.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Hosting;

namespace {
void report_failure(const char* stage, hstring const& detail) noexcept {
    try {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
            auto directory = std::filesystem::path(local) / "EBSDDesktop" / "logs";
            CoTaskMemFree(local);
            std::filesystem::create_directories(directory);
            std::ofstream(directory / "host.log", std::ios::app) << stage << ": " << to_string(detail) << '\n';
        }
    } catch (...) {}
    MessageBoxW(nullptr, detail.c_str(), L"EBSD Desktop", MB_OK | MB_ICONERROR);
}
struct Host {
    HWND window = nullptr;
    DesktopWindowXamlSource island{nullptr};
    com_ptr<EBSDDesktop::implementation::ShellView> view;
    guid focus_correlation{};
    bool navigating_focus = false;
    void resize() {
        if (!island) return;
        RECT bounds{};
        GetClientRect(window, &bounds);
        island.SiteBridge().MoveAndResize({0, 0, bounds.right, bounds.bottom});
    }
    void focus(XamlSourceFocusNavigationReason reason = XamlSourceFocusNavigationReason::First) {
        if (!island || navigating_focus) return;
        auto request = XamlSourceFocusNavigationRequest(reason);
        focus_correlation = request.CorrelationId();
        navigating_focus = true;
        try { island.NavigateFocus(request); }
        catch (...) { navigating_focus = false; throw; }
        navigating_focus = false;
    }
    void release() {
        if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
        view = nullptr;
    }
};
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto host = reinterpret_cast<Host*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        host = static_cast<Host*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        host->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(host));
    }
    if (host) {
        switch (message) {
        case WM_SIZE: host->resize(); return 0;
        case WM_DPICHANGED: {
            const auto rect = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            host->resize(); return 0;
        }
        case WM_SETFOCUS:
            if (host->island && !host->island.HasFocus()) host->focus();
            return 0;
        case WM_CLOSE:
            if (host->view) host->view->begin_close();
            else DestroyWindow(window);
            return 0;
        case WM_APP + 1:
            host->release();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const char* stage = "Initialize STA";
    try {
        init_apartment(apartment_type::single_threaded);
        stage = "Create UI dispatcher";
        auto dispatcher = Microsoft::UI::Dispatching::DispatcherQueueController::CreateOnCurrentThread();
        // App only supplies generated XAML metadata/resources. Win32 owns the
        // HWND and message loop; there is no Application::Start or Xaml::Window.
        stage = "Create XAML application";
        auto app = make_self<EBSDDesktop::implementation::App>();
        stage = "Initialize XAML manager";
        auto manager = WindowsXamlManager::InitializeForCurrentThread();
        stage = "Load XAML application resources";
        app->InitializeResources();
        WNDCLASSEXW window_class{sizeof(WNDCLASSEXW)};
        window_class.lpfnWndProc = window_proc;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = L"EBSDDesktopHost";
        check_bool(RegisterClassExW(&window_class));
        Host host;
        stage = "Create Win32 host";
        check_bool(CreateWindowExW(0, window_class.lpszClassName, L"EBSD Desktop", WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 1280, 900, nullptr, nullptr, instance, &host) != nullptr);
        stage = "Create XAML Island";
        host.island = DesktopWindowXamlSource();
        host.island.Initialize(Microsoft::UI::GetWindowIdFromWindow(host.window));
        stage = "Load ShellView XAML";
        host.view = make_self<EBSDDesktop::implementation::ShellView>();
        host.view->set_host(host.window);
        host.island.Content(*host.view);
        host.island.TakeFocusRequested([&host](auto const&, DesktopWindowXamlSourceTakeFocusRequestedEventArgs const& args) {
            // A failed NavigateFocus returns the same request. Retrying that
            // request would create an infinite UI-thread focus loop.
            if (args.Request().CorrelationId() == host.focus_correlation) return;
            // The island fills the entire client area. Wrap keyboard focus back
            // into it instead of losing Tab/Shift+Tab to the empty native host.
            const auto reason = args.Request().Reason() == XamlSourceFocusNavigationReason::Last
                ? XamlSourceFocusNavigationReason::Last : XamlSourceFocusNavigationReason::First;
            Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue(
                [&host, reason] { host.focus(reason); });
        });
        stage = "Show XAML Island";
        host.resize();
        ShowWindow(host.window, show);
        UpdateWindow(host.window);
        stage = "Run UI message loop";
        using PreTranslate = BOOL(WINAPI*)(MSG*);
        const auto module = GetModuleHandleW(L"Microsoft.UI.Windowing.Core.dll");
        const auto pre_translate = reinterpret_cast<PreTranslate>(GetProcAddress(module, "ContentPreTranslateMessage"));
        check_bool(pre_translate != nullptr);
        MSG message{};
        int result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (pre_translate(&message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        stage = "Shutdown XAML Island";
        host.release();
        manager.Close();
        app = nullptr;
        dispatcher.ShutdownQueue();
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (hresult_error const& error) {
        report_failure(stage, error.message() + L" (HRESULT " + to_hstring(static_cast<int32_t>(error.code())) + L")");
    } catch (std::exception const& error) {
        report_failure(stage, to_hstring(error.what()));
    }
    return 1;
}
