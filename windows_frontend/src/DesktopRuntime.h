#pragma once
#include "LocalServer.h"
#include <windows.h>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ebsd {
class DesktopRuntime {
public:
    DesktopRuntime();
    ~DesktopRuntime();
    void start();
    void cancel() { cancelled_ = true; }
    void stop();
    std::string origin() const { return server_->origin(); }
    const std::string& token() const { return token_; }
    const std::filesystem::path& data_directory() const { return data_; }
    const std::filesystem::path& webview_runtime() const { return webview_runtime_; }
    std::string failed_backend() const;
private:
    struct Child { HANDLE process = nullptr; std::string name; };
    void launch(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
        const std::map<std::wstring, std::wstring>& environment, const std::string& name);
    void wait_ready(int port, const char* path, const Child& child);
    std::filesystem::path root_, data_, webview_runtime_;
    std::string token_;
    std::unique_ptr<LocalServer> server_;
    HANDLE job_ = nullptr;
    HANDLE lock_ = INVALID_HANDLE_VALUE;
    std::vector<Child> children_;
    std::atomic<bool> cancelled_ = false;
    mutable std::mutex mutex_;
};
}
