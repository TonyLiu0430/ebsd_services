#include "pch.h"
#include "App.xaml.h"

namespace winrt::EBSDDesktop::implementation {
App::App() {}
// Islands initialize the XAML framework after constructing Application. Loading
// XAML resources in the constructor is too early, even on a COM STA thread.
void App::InitializeResources() { InitializeComponent(); }
}
