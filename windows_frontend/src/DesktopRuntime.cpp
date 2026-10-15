#include "DesktopRuntime.h"
#include "ProcessArguments.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ebsd {
namespace {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void check(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
std::string utf8(const std::filesystem::path& path) {
    auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    check(count > 0, "Decode UTF-8");
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
std::string random_token() {
    unsigned char bytes[32];
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw std::runtime_error("Cannot generate desktop session");
    std::ostringstream result;
    for (auto byte : bytes) result << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return result.str();
}
std::string local_user(const std::filesystem::path& data) {
    const auto file = data / "user.json";
    if (std::filesystem::exists(file)) {
        auto json = nlohmann::json::parse(std::ifstream(file));
        auto id = json.at("id").get<std::string>();
        GUID parsed{};
        const auto guid_text = L"{" + wide(id) + L"}";
        if (FAILED(CLSIDFromString(guid_text.c_str(), &parsed)))
            throw std::runtime_error("Invalid user.json; restore it with the matching database backup");
        return id;
    }
    // Never silently orphan an existing database by creating a different owner.
    if (std::filesystem::exists(data / "ebsd_store.db"))
        throw std::runtime_error("user.json is missing; restore it with the database backup");
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot create local user ID");
    wchar_t buffer[39];
    StringFromGUID2(guid, buffer, 39);
    std::string id;
    for (int i = 1; i < 37; ++i) id += static_cast<char>(buffer[i]);
    const auto temporary = data / "user.json.tmp";
    std::ofstream output(temporary, std::ios::trunc);
    output << nlohmann::json{{"id", id}}.dump(2);
    output.close();
    if (!output) throw std::runtime_error("Cannot save local user ID");
    std::filesystem::rename(temporary, file);
    return id;
}
int available_port() {
    httplib::Server reservation;
    const int port = reservation.bind_to_any_port("127.0.0.1");
    if (port <= 0) throw std::runtime_error("Cannot allocate backend port");
    return port;
}
struct EnvironmentLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};
}

DesktopRuntime::DesktopRuntime() {
    wchar_t executable[32768];
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    check(length > 0 && length < 32768, "Locate application");
    root_ = std::filesystem::path(std::wstring(executable, length)).parent_path();
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
        throw std::runtime_error("Cannot locate LocalAppData");
    data_ = std::filesystem::path(local) / "EBSDDesktop";
    CoTaskMemFree(local);
    std::filesystem::create_directories(data_ / "logs");
    std::filesystem::create_directories(data_ / "ppt-jobs");
}
DesktopRuntime::~DesktopRuntime() { stop(); }

void DesktopRuntime::start() {
    std::lock_guard guard(mutex_);
    if (cancelled_) throw std::runtime_error("Startup cancelled");
    lock_ = CreateFileW((data_ / "instance.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock_ != INVALID_HANDLE_VALUE, "EBSD Desktop is already running or its data directory is unavailable");
    auto config = nlohmann::json::parse(std::ifstream(root_ / "desktop.json"));
    const auto resolve = [this, &config](const char* key) {
        const auto text = config.at(key).get<std::string>();
        auto path = std::filesystem::path(std::u8string(text.begin(), text.end()));
        return std::filesystem::absolute(root_ / path);
    };
    const auto python = resolve("python_executable");
    const auto node = resolve("node_executable");
    const auto cpp = resolve("cpp_executable");
    const auto ml_root = resolve("ml_root");
    for (const auto& path : {python, node, cpp, ml_root / "main.py"}) {
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Missing runtime file: " + utf8(path));
    }
    if (!config.value("webview_runtime", "").empty()) webview_runtime_ = resolve("webview_runtime");
    token_ = random_token();
    const int cpp_port = available_port();
    int ml_port = available_port();
    while (ml_port == cpp_port) ml_port = available_port();
    server_ = std::make_unique<LocalServer>(ServerOptions{resolve("web_root"), local_user(data_), token_, ml_port, cpp_port,
        config.value("max_upload_mib", size_t{1024}) * 1024ULL * 1024});
    server_->start();
    job_ = CreateJobObjectW(nullptr, nullptr);
    check(job_ != nullptr, "Create backend job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check(SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != 0, "Configure backend job");

    launch(cpp, {}, {{L"EBSD_BIND_HOST", L"127.0.0.1"}, {L"EBSD_PORT", std::to_wstring(cpp_port)}}, "cpp");
    wait_ready(cpp_port, "/", children_.back());
    std::wstring path = node.parent_path().wstring() + L";" + python.parent_path().wstring();
    if (const auto inherited = _wgetenv(L"PATH")) path += L";" + std::wstring(inherited);
    // Cairo DLLs may be supplied beside Python or in a dedicated runtime folder.
    if (!config.value("native_dll_directory", "").empty()) path = resolve("native_dll_directory").wstring() + L";" + path;
    std::map<std::wstring, std::wstring> environment{
        {L"PATH", path}, {L"PYTHONUTF8", L"1"}, {L"PYTHONDONTWRITEBYTECODE", L"1"},
        {L"PYTHONUNBUFFERED", L"1"}, {L"GO_BACKEND_URL", wide(server_->auth_origin())},
        {L"CPP_BACKEND_URL", L"http://127.0.0.1:" + std::to_wstring(cpp_port)},
        {L"DATABASE_URL", wide("sqlite:///" + utf8(data_ / "ebsd_store.db"))},
        {L"PPT_JOB_DIR", (data_ / "ppt-jobs").wstring()},
        {L"MPLCONFIGDIR", (data_ / "matplotlib").wstring()},
        {L"NUMBA_CACHE_DIR", (data_ / "numba").wstring()}
    };
    launch(python, {L"-B", L"-m", L"uvicorn", L"main:app", L"--app-dir", ml_root.wstring(),
        L"--host", L"127.0.0.1", L"--port", std::to_wstring(ml_port), L"--workers", L"1"}, environment, "python");
    wait_ready(ml_port, "/ebsd_features", children_.back());
}

void DesktopRuntime::launch(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
    const std::map<std::wstring, std::wstring>& overrides, const std::string& name) {
    if (cancelled_) throw std::runtime_error("Startup cancelled");
    std::map<std::wstring, std::wstring, EnvironmentLess> environment;
    auto block = GetEnvironmentStringsW();
    check(block != nullptr, "Read environment");
    for (auto entry = block; *entry; entry += wcslen(entry) + 1) {
        const std::wstring value(entry);
        const auto split = value.find(L'=', 1);
        if (split != std::wstring::npos) environment[value.substr(0, split)] = value.substr(split + 1);
    }
    FreeEnvironmentStringsW(block);
    for (const auto& [key, value] : overrides) environment[key] = value;
    std::vector<wchar_t> child_environment;
    for (const auto& [key, value] : environment) {
        const auto entry = key + L"=" + value;
        child_environment.insert(child_environment.end(), entry.begin(), entry.end());
        child_environment.push_back(0);
    }
    child_environment.push_back(0);
    std::wstring command = quote_argument(executable.wstring());
    for (const auto& argument : arguments) command += L" " + quote_argument(argument);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle output{CreateFileW((data_ / "logs" / (name + ".log")).c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    check(output.value != INVALID_HANDLE_VALUE, "Open backend log");
    Handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr)};
    check(input.value != INVALID_HANDLE_VALUE, "Open backend stdin");
    SIZE_T attribute_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<unsigned char> attributes(attribute_size);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    check(InitializeProcThreadAttributeList(list, 1, 0, &attribute_size) != 0, "Initialize process attributes");
    HANDLE inherited[] = {output.value, input.value};
    const bool updated = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        inherited, sizeof(inherited), nullptr, nullptr) != 0;
    if (!updated) { DeleteProcThreadAttributeList(list); check(false, "Set inherited handles"); }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.value;
    startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = output.value;
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    const bool created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
        child_environment.data(), data_.c_str(), &startup.StartupInfo, &process) != 0;
    const auto create_error = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!created) { SetLastError(create_error); check(false, "Start backend"); }
    Handle thread{process.hThread};
    if (!AssignProcessToJobObject(job_, process.hProcess)) {
        TerminateProcess(process.hProcess, 1); CloseHandle(process.hProcess);
        throw std::runtime_error("Cannot attach backend to desktop lifetime");
    }
    children_.push_back({process.hProcess, name});
    check(ResumeThread(thread.value) != static_cast<DWORD>(-1), "Resume backend");
}

void DesktopRuntime::wait_ready(int port, const char* path, const Child& child) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled_) throw std::runtime_error("Startup cancelled");
        if (WaitForSingleObject(child.process, 0) != WAIT_TIMEOUT)
            throw std::runtime_error(child.name + " exited; see " + utf8(data_ / "logs" / (child.name + ".log")));
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(1);
        client.set_read_timeout(1);
        if (auto response = client.Get(path); response && (response->status == 200 || response->status == 404)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    throw std::runtime_error(child.name + " did not become ready within 120 seconds");
}
std::string DesktopRuntime::failed_backend() const {
    std::lock_guard guard(mutex_);
    for (const auto& child : children_)
        if (WaitForSingleObject(child.process, 0) != WAIT_TIMEOUT) return child.name;
    return {};
}
void DesktopRuntime::stop() {
    cancel();
    std::lock_guard guard(mutex_);
    // Kill the managed process tree first, releasing any pending proxy requests.
    if (job_) { TerminateJobObject(job_, 0); CloseHandle(job_); job_ = nullptr; }
    for (auto& child : children_) {
        WaitForSingleObject(child.process, 5000);
        CloseHandle(child.process);
    }
    children_.clear();
    if (server_) { server_->stop(); server_.reset(); }
    if (lock_ != INVALID_HANDLE_VALUE) { CloseHandle(lock_); lock_ = INVALID_HANDLE_VALUE; }
}
}
