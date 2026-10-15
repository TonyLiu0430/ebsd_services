#include "LocalServer.h"
#include "ProcessArguments.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Backend {
    httplib::Server server;
    int port = server.bind_to_any_port("127.0.0.1");
    std::thread thread;
    void start() { thread = std::thread([this] { server.listen_after_bind(); }); server.wait_until_ready(); }
    ~Backend() { server.stop(); if (thread.joinable()) thread.join(); }
};
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("ebsd-gateway-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "_nuxt");
    std::ofstream(root / "index.html") << "<html>EBSD</html>";
    std::ofstream(root / "_nuxt/app.js") << "window.ebsd = true;";
    try {
        require(ebsd::quote_argument(L"") == L"\"\"", "empty argument quoting failed");
        require(ebsd::quote_argument(L"C:\\a b\\") == L"\"C:\\a b\\\\\"", "trailing slash quoting failed");
        require(ebsd::quote_argument(L"a\"b") == L"\"a\\\"b\"", "embedded quote escaping failed");
        Backend ml;
        Backend cpp;
        const std::string binary("abc\0\xff\r\n", 7);
        ebsd::ServerOptions options{root, "74de6ba4-0937-4e03-85b7-2f040b75802b",
            std::string(64, 'a'), ml.port, cpp.port, 4096};
        ebsd::LocalServer desktop(options);
        desktop.start();
        // Simulate exactly the callback used by current_user_from_go_session:
        // the incoming browser Cookie is forwarded to GO_BACKEND_URL/user/check.
        ml.server.Get("/private", [&](const auto& request, auto& response) {
            httplib::Client auth(desktop.auth_origin());
            auto result = auth.Get("/user/check", {{"Cookie", request.get_header_value("Cookie")}});
            if (!result) { response.status = 503; return; }
            response.status = result->status;
            response.set_content(result->body, "application/json");
        });
        ml.server.Post("/upload", [&](const auto& request, auto& response) {
            require(request.get_param_value("q") == "a b", "query encoding changed");
            require(request.has_file("crc"), "multipart file missing");
            require(request.get_file_value("crc").content == binary, "binary upload changed");
            require(request.get_file_value("crc").filename == "test.crc", "filename changed");
            require(request.get_file_value("sample").content == "Ni", "form field missing");
            response.set_content("uploaded", "text/plain");
        });
        ml.server.Post("/json", [](const auto& request, auto& response) {
            response.status = 202;
            response.set_content(request.body, "application/json");
        });
        ml.server.Delete("/pairs/id", [](const auto&, auto& response) { response.status = 204; });
        ml.server.Get("/report", [&](const auto&, auto& response) {
            response.set_header("Content-Disposition", "attachment; filename=report.pptx");
            response.set_content(binary, "application/vnd.openxmlformats-officedocument.presentationml.presentation");
        });
        ml.server.Get("/invalid", [](const auto&, auto& response) {
            response.status = 422;
            response.set_content(R"({"detail":"invalid scan"})", "application/json");
        });
        cpp.server.Get("/ipf_legend", [](const auto&, auto& response) { response.set_content("PNG", "image/png"); });
        ml.start(); cpp.start();
        httplib::Client browser(desktop.origin());
        browser.set_read_timeout(10);
        httplib::Headers session{{"Cookie", desktop.cookie()}};
        auto result = browser.Get("/goapi/user/check");
        require(result && result->status == 401, "unauthenticated access allowed");
        result = browser.Get("/goapi/user/check", {{"Cookie", "prefix_" + desktop.cookie()}});
        require(result && result->status == 401, "substring cookie accepted");
        result = browser.Get("/goapi/user/check", session);
        require(result && result->status == 200, "local session check failed");
        require(nlohmann::json::parse(result->body)["user"]["id"] == options.user_id, "user ID contract changed");
        result = browser.Get("/api/private", session);
        require(result && result->status == 200, "Python auth callback failed");
        // More in-flight requests than gateway workers must not starve auth.
        std::vector<std::future<bool>> requests;
        for (int i = 0; i < 8; ++i) requests.push_back(std::async(std::launch::async, [&] {
            httplib::Client client(desktop.origin()); client.set_read_timeout(10);
            auto reply = client.Get("/api/private", session);
            return reply && reply->status == 200;
        }));
        for (auto& future : requests) require(future.get(), "concurrent Python callback starved");
        result = browser.Post("/api/upload?q=a%20b", session,
            httplib::MultipartFormDataItems{{"crc", binary, "test.crc", "application/octet-stream"}, {"sample", "Ni", "", ""}});
        require(result && result->status == 200 && result->body == "uploaded", "multipart proxy failed");
        result = browser.Post("/api/json", session, R"({"name":"測試"})", "application/json");
        require(result && result->status == 202 && result->body == R"({"name":"測試"})", "JSON proxy failed");
        result = browser.Delete("/api/pairs/id", session);
        require(result && result->status == 204, "DELETE status changed");
        result = browser.Get("/api/report", session);
        require(result && result->body == binary && result->get_header_value("Content-Disposition").find("report.pptx") != std::string::npos, "download changed");
        result = browser.Get("/api/invalid", session);
        require(result && result->status == 422, "upstream error lost");
        result = browser.Get("/cppapi/ipf_legend", session);
        require(result && result->body == "PNG", "C++ route failed");
        for (const auto* path : {"/", "/report", "/login", "/ipf-viewer"}) {
            result = browser.Get(path, session);
            require(result && result->status == 200 && result->body.find("<html>") != std::string::npos, "SPA route failed");
        }
        result = browser.Get("/_nuxt/app.js", session);
        require(result && result->status == 200 && result->get_header_value("Content-Type") == "text/javascript", "asset MIME wrong");
        result = browser.Get("/_nuxt/missing.js", session);
        require(result && result->status == 404, "missing asset received SPA fallback");
        result = browser.Get("/goapi/missing", session);
        require(result && result->status == 404, "unknown auth API received SPA fallback");
        auto cross_origin = session; cross_origin.emplace("Origin", "https://example.com");
        result = browser.Post("/api/json", cross_origin, "{}", "application/json");
        require(result && result->status == 403, "cross-origin request accepted");
        auto foreign_host = session; foreign_host.emplace("Host", "evil.example");
        result = browser.Get("/", foreign_host);
        require(result && result->status == 403, "foreign Host accepted");
        result = browser.Get("/%2e%2e/secret", session);
        require(result && result->status >= 400, "path traversal accepted");
        result = browser.Get("/C:%5csecret", session);
        require(result && result->status >= 400, "Windows path accepted");
        result = browser.Post("/api/json", session, std::string(8192, 'x'), "application/json");
        require(result && result->status == 413, "oversized upload accepted");
        for (const auto* path : {"/goapi/user/login", "/goapi/user/register", "/goapi/user/logout"}) {
            result = browser.Post(path, session, "{}", "application/json");
            require(result && result->status == 200, "desktop auth route failed");
        }
        httplib::Client auth(desktop.auth_origin());
        result = auth.Get("/user/check");
        require(result && result->status == 401, "auth callback bypassed session");
        ml.server.stop();
        result = browser.Get("/api/private", session);
        require(result && result->status == 502, "unavailable backend not reported");
        std::cout << "Desktop gateway contracts passed\n";
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
}
