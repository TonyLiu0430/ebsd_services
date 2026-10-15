#include "LocalServer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

namespace ebsd {
namespace {
void error(httplib::Response& response, int status, const char* message) {
    response.status = status;
    response.set_content(nlohmann::json{{"error", message}}.dump(), "application/json");
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool hop_header(const std::string& name) {
    static const std::set<std::string> headers = {"connection", "keep-alive", "proxy-authenticate",
        "proxy-authorization", "te", "trailer", "transfer-encoding", "upgrade", "host", "content-length"};
    return headers.contains(lower(name));
}
bool is_api(const std::string& path) {
    return path.starts_with("/api/") || path.starts_with("/cppapi/");
}
}

LocalServer::LocalServer(ServerOptions options) : options_(std::move(options)) {
    if (options_.session_token.size() < 32 || options_.user_id.empty())
        throw std::invalid_argument("A local user and a random session token are required");
    gateway_.set_payload_max_length(options_.max_upload_bytes);
    auth_.set_payload_max_length(4096);
    gateway_.set_read_timeout(60);
    gateway_.set_write_timeout(60);
    gateway_.new_task_queue = [] { return new httplib::ThreadPool(4); };
    auth_.new_task_queue = [] { return new httplib::ThreadPool(2); };
    gateway_.set_pre_routing_handler([this](const auto& request, auto& response) {
        if (!trusted_request(request, gateway_port_)) {
            error(response, 403, "Unexpected host or origin");
        } else if (!authorized(request)) {
            error(response, 401, "Desktop session required");
        } else {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        return httplib::Server::HandlerResponse::Handled;
    });
    auth_.set_pre_routing_handler([this](const auto& request, auto& response) {
        if (!trusted_request(request, auth_port_) || !authorized(request)) {
            error(response, 401, "Desktop session required");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    const auto headers = [](const auto&, auto& response) {
        response.set_header("Cache-Control", "no-store");
        response.set_header("X-Content-Type-Options", "nosniff");
        response.set_header("X-Frame-Options", "DENY");
        response.set_header("Referrer-Policy", "no-referrer");
    };
    gateway_.set_post_routing_handler(headers);
    auth_.set_post_routing_handler(headers);
    gateway_.Get("/goapi/user/check", [this](const auto&, auto& response) { user(response); });
    auth_.Get("/user/check", [this](const auto&, auto& response) { user(response); });
    // Single-user mode: login/register do not create accounts or store passwords.
    gateway_.Post("/goapi/user/(login|register)", [this](const auto&, auto& response) { user(response); });
    gateway_.Post("/goapi/user/logout", [](const auto&, auto& response) {
        response.set_content(R"({"msg":"ok"})", "application/json");
    });
    const auto proxy = [this](const auto& request, auto& response) { forward(request, response); };
    gateway_.Get("/(api|cppapi)/.*", proxy);
    gateway_.Post("/(api|cppapi)/.*", proxy);
    gateway_.Put("/(api|cppapi)/.*", proxy);
    gateway_.Patch("/(api|cppapi)/.*", proxy);
    gateway_.Delete("/(api|cppapi)/.*", proxy);
    gateway_.Get("/.*", [this](const auto& request, auto& response) { page(request, response); });
}

LocalServer::~LocalServer() { stop(); }
void LocalServer::start() {
    if (gateway_thread_.joinable()) throw std::logic_error("Server is already running");
    if (!std::filesystem::is_regular_file(options_.web_root / "index.html"))
        throw std::runtime_error("Missing web/index.html; generate and stage the Vue frontend first");
    gateway_port_ = gateway_.bind_to_any_port("127.0.0.1");
    auth_port_ = auth_.bind_to_any_port("127.0.0.1");
    if (gateway_port_ <= 0 || auth_port_ <= 0) {
        gateway_.stop(); auth_.stop();
        throw std::runtime_error("Cannot bind local desktop services");
    }
    auth_thread_ = std::thread([this] { auth_.listen_after_bind(); });
    gateway_thread_ = std::thread([this] { gateway_.listen_after_bind(); });
    gateway_.wait_until_ready();
    auth_.wait_until_ready();
}
void LocalServer::stop() {
    gateway_.stop();
    auth_.stop();
    if (gateway_thread_.joinable()) gateway_thread_.join();
    if (auth_thread_.joinable()) auth_thread_.join();
}
std::string LocalServer::origin() const { return "http://127.0.0.1:" + std::to_string(gateway_port_); }
std::string LocalServer::auth_origin() const { return "http://127.0.0.1:" + std::to_string(auth_port_); }
std::string LocalServer::cookie() const { return std::string(cookie_name) + "=" + options_.session_token; }

bool LocalServer::authorized(const httplib::Request& request) const {
    // Match complete cookie names and values, not substrings such as foo=<token>.
    std::istringstream stream(request.get_header_value("Cookie"));
    std::string item;
    while (std::getline(stream, item, ';')) {
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string::npos && item.substr(first, last - first + 1) == cookie()) return true;
    }
    return false;
}
bool LocalServer::trusted_request(const httplib::Request& request, int port) const {
    const auto host = "127.0.0.1:" + std::to_string(port);
    if (request.get_header_value("Host") != host) return false;
    if (request.has_header("Origin") && request.get_header_value("Origin") != "http://" + host) return false;
    if (request.get_header_value("Sec-Fetch-Site") == "cross-site") return false;
    return true;
}
void LocalServer::user(httplib::Response& response) const {
    response.set_content(nlohmann::json{{"user", {{"id", options_.user_id}, {"username", "Local user"}}}}.dump(), "application/json");
}

void LocalServer::forward(const httplib::Request& request, httplib::Response& response) {
    const bool cpp = request.path.starts_with("/cppapi/");
    httplib::Client client("127.0.0.1", cpp ? options_.cpp_port : options_.ml_port);
    client.set_connection_timeout(3);
    client.set_read_timeout(600);
    client.set_write_timeout(60);
    client.set_follow_location(false);
    // Keep the already-escaped request target (including its query string).
    const auto target = request.target.substr(cpp ? 7 : 4);
    httplib::Headers outgoing;
    for (const auto& [name, value] : request.headers) {
        const auto key = lower(name);
        if (!hop_header(name) && key != "accept-encoding" && key != "origin" &&
            key != "referer" && !key.starts_with("sec-fetch-") && key != "content-type")
            outgoing.emplace(name, value);
    }
    // cpp-httplib parses incoming multipart uploads. Rebuild the multipart body
    // through its public client API, keeping binary bytes and duplicate fields.
    httplib::Result result;
    if (request.is_multipart_form_data() && request.method == "POST") {
        httplib::MultipartFormDataItems items;
        for (const auto& [name, part] : request.files)
            items.push_back({name, part.content, part.filename, part.content_type});
        result = client.Post(target, outgoing, items);
    } else {
        httplib::Request upstream;
        upstream.method = request.method;
        upstream.path = target;
        upstream.headers = std::move(outgoing);
        upstream.body = request.body;
        if (request.has_header("Content-Type")) upstream.set_header("Content-Type", request.get_header_value("Content-Type"));
        result = client.send(upstream);
    }
    if (!result) { error(response, 502, "Local analysis service is unavailable"); return; }
    response.status = result->status;
    for (const auto& [name, value] : result->headers) {
        if (!hop_header(name) && lower(name) != "set-cookie" && !lower(name).starts_with("access-control-"))
            response.set_header(name, value);
    }
    response.body = std::move(result->body);
}

void LocalServer::page(const httplib::Request& request, httplib::Response& response) {
    if (request.path.starts_with("/goapi/") || is_api(request.path)) {
        error(response, 404, "Unknown API route"); return;
    }
    const auto root = std::filesystem::weakly_canonical(options_.web_root);
    const auto relative_text = request.path.substr(1);
    const auto relative = std::filesystem::path(std::u8string(relative_text.begin(), relative_text.end()));
    // Reject Windows drive paths, alternate streams and backslash traversal on
    // every platform, including Linux contract tests.
    if (request.path.find_first_of("\\:") != std::string::npos || request.path.find('\0') != std::string::npos) {
        error(response, 400, "Invalid asset path"); return;
    }
    auto path = std::filesystem::weakly_canonical(root / relative);
    const auto within = path.lexically_relative(root);
    if (within.is_absolute() || (!within.empty() && *within.begin() == "..")) {
        error(response, 403, "Invalid asset path"); return;
    }
    if (std::filesystem::is_directory(path)) path /= "index.html";
    if (!std::filesystem::is_regular_file(path)) {
        // Only application routes get the SPA fallback; missing assets stay 404.
        if (request.path != "/" && request.path != "/report" && request.path != "/login" && request.path != "/ipf-viewer") {
            error(response, 404, "Asset not found"); return;
        }
        path = root / "index.html";
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) { error(response, 404, "Asset not found"); return; }
    std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto extension = lower(path.extension().string());
    static const std::map<std::string, std::string> types = {{".html", "text/html; charset=utf-8"},
        {".js", "text/javascript"}, {".mjs", "text/javascript"}, {".css", "text/css"},
        {".json", "application/json"}, {".svg", "image/svg+xml"}, {".png", "image/png"},
        {".jpg", "image/jpeg"}, {".ico", "image/x-icon"}, {".woff2", "font/woff2"}, {".woff", "font/woff"}};
    const auto type = types.find(extension);
    response.set_content(std::move(body), type == types.end() ? "application/octet-stream" : type->second);
}
}
