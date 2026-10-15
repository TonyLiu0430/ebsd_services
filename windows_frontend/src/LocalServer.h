#pragma once
#include <httplib.h>
#include <filesystem>
#include <string>
#include <thread>

namespace ebsd {
struct ServerOptions {
    std::filesystem::path web_root;
    std::string user_id;
    std::string session_token;
    int ml_port = 0;
    int cpp_port = 0;
    size_t max_upload_bytes = 1024ULL * 1024 * 1024;
};

// The auth listener has its own worker pool: Python calls it while the public
// gateway is waiting for Python's response. Sharing the pool can deadlock.
class LocalServer {
public:
    explicit LocalServer(ServerOptions options);
    ~LocalServer();
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;
    void start();
    void stop();
    std::string origin() const;
    std::string auth_origin() const;
    std::string cookie() const;
    static constexpr const char* cookie_name = "ebsd_desktop_session";
private:
    bool authorized(const httplib::Request& request) const;
    bool trusted_request(const httplib::Request& request, int port) const;
    void user(httplib::Response& response) const;
    void forward(const httplib::Request& request, httplib::Response& response);
    void page(const httplib::Request& request, httplib::Response& response);
    ServerOptions options_;
    httplib::Server gateway_;
    httplib::Server auth_;
    int gateway_port_ = 0;
    int auth_port_ = 0;
    std::thread gateway_thread_;
    std::thread auth_thread_;
};
}
