#pragma once
#include "App.xaml.g.h"

namespace winrt::EBSDDesktop::implementation {
struct App : AppT<App> {
    App();
    void InitializeResources();
private:
    // make/make_self automatically invoke a public InitializeComponent. Islands
    // must defer it until WindowsXamlManager has initialized this STA's XAML core.
    using AppT<App>::InitializeComponent;
};
}
