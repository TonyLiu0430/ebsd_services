#pragma once
#include "ShellView.g.h"
#include "DesktopRuntime.h"

namespace winrt::EBSDDesktop::implementation {
struct ShellView : ShellViewT<ShellView> {
    ShellView();
    void set_host(HWND host) { host_ = host; }
    void begin_close() { if (!closing_) shutdown(); }
    void Root_Loaded(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Reload_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void Data_Click(Windows::Foundation::IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
private:
    Windows::Foundation::IAsyncAction start_async();
    void require_ui_thread() const;
    winrt::fire_and_forget shutdown();
    void show_error(winrt::hstring const& message);
    std::shared_ptr<ebsd::DesktopRuntime> runtime_;
    Microsoft::UI::Xaml::DispatcherTimer monitor_;
    Windows::Foundation::IAsyncAction startup_{nullptr};
    bool started_ = false;
    bool closing_ = false;
    HWND host_ = nullptr;
};
}
namespace winrt::EBSDDesktop::factory_implementation {
struct ShellView : ShellViewT<ShellView, implementation::ShellView> {};
}
