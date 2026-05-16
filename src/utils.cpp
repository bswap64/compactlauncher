#include "launcher.h"
#include "json_parser.h"

#include <QCryptographicHash>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <wininet.h>
#else
  #include <curl/curl.h>
#endif

#include <fstream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <mutex>
#include <vector>

#ifdef _WIN32

static HINTERNET openSession() {
    HINTERNET h = InternetOpenA("Compact Launcher/2.0",
                                INTERNET_OPEN_TYPE_DIRECT,
                                nullptr, nullptr, 0);
    if (h) {

        DWORD timeout = 10000;
        InternetSetOptionA(h, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(h, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(h, INTERNET_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));
        DWORD maxConn = 1;
        InternetSetOptionA(h, INTERNET_OPTION_MAX_CONNS_PER_SERVER,     &maxConn, sizeof(maxConn));
        InternetSetOptionA(h, INTERNET_OPTION_MAX_CONNS_PER_1_0_SERVER, &maxConn, sizeof(maxConn));
    }
    return h;
}

static HINTERNET openUrl(HINTERNET hSession, const std::string& url) {
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
    HINTERNET h = InternetOpenUrlA(hSession, url.c_str(), nullptr, 0, flags, 0);
    if (!h) {
        flags &= ~INTERNET_FLAG_SECURE;
        h = InternetOpenUrlA(hSession, url.c_str(), nullptr, 0, flags, 0);
    }
    return h;
}

std::string httpGetString(const std::string& url) {
    LOG("GET " + url);
    HINTERNET hSess = openSession();
    if (!hSess) { LOG("GET failed (no session): " + url); return ""; }
    HINTERNET hUrl = openUrl(hSess, url);
    if (!hUrl) {
        InternetCloseHandle(hSess);
        LOG("GET failed (open url): " + url);
        return "";
    }
    std::string result;
    std::vector<char> buf(8192);
    DWORD read = 0;
    while (InternetReadFile(hUrl, buf.data(), (DWORD)buf.size(), &read) && read > 0)
        result.append(buf.data(), read);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hSess);
    LOG("GET ok " + url + " (" + std::to_string(result.size()) + " bytes)");
    return result;
}


bool httpDownloadToFile(const std::string& url, const std::string& dest,
                        const std::atomic<bool>& cancelled) {
    HINTERNET hSess = openSession();
    if (!hSess) { LOG("Download failed (no session): " + url); return false; }
    HINTERNET hUrl = openUrl(hSess, url);
    if (!hUrl) {
        InternetCloseHandle(hSess);
        LOG("Download failed (open url): " + url);
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hSess);
        LOG("Download failed (cannot write): " + dest);
        return false;
    }
    std::vector<char> buf(32768);
    DWORD read = 0;
    bool ok = true;
    while (true) {

        if (cancelled) { ok = false; break; }
        if (!InternetReadFile(hUrl, buf.data(), (DWORD)buf.size(), &read)) { ok = false; break; }
        if (read == 0) break;
        out.write(buf.data(), read);
        if (!out) { ok = false; break; }
    }
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hSess);
    out.close();
    if (!ok) {
        std::filesystem::remove(dest);
        if (!cancelled)
            LOG("Download failed (read/write error): " + dest);
        return false;
    }
    return std::filesystem::exists(dest);
}

#else

static size_t curlWriteStr(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* s = static_cast<std::string*>(userdata);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}
[[maybe_unused]] static size_t curlWriteFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* f = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, f);
}


struct CurlCancelCtx {
    FILE* file;
    const std::atomic<bool>& cancelled;
};

static size_t curlWriteFileCancellable(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<CurlCancelCtx*>(userdata);
    if (ctx->cancelled) return 0; 
    return fwrite(ptr, size, nmemb, ctx->file);
}

std::string httpGetString(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    std::string result;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteStr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return result;
}


bool httpDownloadToFile(const std::string& url, const std::string& dest,
                        const std::atomic<bool>& cancelled) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    FILE* f = fopen(dest.c_str(), "wb");
    if (!f) { curl_easy_cleanup(curl); return false; }
    CurlCancelCtx ctx{f, cancelled};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteFileCancellable);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode res = curl_easy_perform(curl);
    fclose(f);
    curl_easy_cleanup(curl);
    if (cancelled || res != CURLE_OK) {
        std::filesystem::remove(dest);
        return false;
    }
    return true;
}
#endif

void mkdirRecursive(const std::string& path) {
    fs::create_directories(path);
}

std::string readFileText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFileText(const std::string& path, const std::string& data) {
    std::ofstream f(path);
    if (!f) return false;
    f << data;
    return true;
}

long long fileSize(const std::string& path) {
    std::error_code ec;
    auto s = fs::file_size(path, ec);
    return ec ? -1LL : (long long)s;
}

static std::string md5hex(const std::string& input) {
    QByteArray data(input.data(), (int)input.size());
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    return hash.toHex().toStdString();
}

std::string offlineUUID(const std::string& name) {
    std::string h = md5hex("OfflinePlayer:" + name);
    h[12] = '3';
    char v = h[16];
    int nibble = (v >= '0' && v <= '9') ? (v - '0') : (v - 'a' + 10);
    nibble = (nibble & 0x3) | 0x8;
    h[16] = nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10);
    return h.substr(0,8) + "-" + h.substr(8,4) + "-" + h.substr(12,4) + "-" + h.substr(16,4) + "-" + h.substr(20,12);
}

std::vector<std::string> listInstalledVersions(const std::string& workDir) {
    std::vector<std::string> result;
    std::string versDir = workDir + "/versions";
    if (!fs::exists(versDir)) return result;
    for (auto& entry : fs::directory_iterator(versDir)) {
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        std::string jsonPath = versDir + "/" + name + "/" + name + ".json";
        if (fs::exists(jsonPath))
            result.push_back(name);
    }
    auto parseVer = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream ss(s);
        std::string token;
        while (std::getline(ss, token, '.')) {
            try { parts.push_back(std::stoi(token)); }
            catch (...) { parts.push_back(0); }
        }
        return parts;
    };
    std::sort(result.begin(), result.end(), [&](const std::string& a, const std::string& b) {
        return parseVer(a) > parseVer(b);
    });
    return result;
}

std::vector<std::string> findJavaInstalls() {
    std::vector<std::string> result;
#ifdef _WIN32
    std::vector<std::string> javaRoots;
    auto addJavaRoot = [&](const char* envVar) {
        const char* p = getenv(envVar);
        if (p) javaRoots.push_back(std::string(p));
    };
    addJavaRoot("ProgramW6432");
    addJavaRoot("PROGRAMFILES");
    addJavaRoot("PROGRAMFILES(X86)");
    std::vector<std::string> javaVendors = {"Java", "Eclipse Adoptium", "Microsoft", "BellSoft", "Azul", "Amazon Corretto"};
    for (auto& root : javaRoots) {
        for (auto& vendor : javaVendors) {
            std::string dir = root + "\\" + vendor;
            if (!fs::exists(dir)) continue;
            for (auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_directory()) continue;
                std::string javaw = entry.path().string() + "\\bin\\javaw.exe";
                if (fs::exists(javaw))
                    result.push_back(javaw);
            }
        }
    }
    const char* jh = getenv("JAVA_HOME");
    if (jh) {
        std::string jw = std::string(jh) + "\\bin\\javaw.exe";
        if (fs::exists(jw)) result.push_back(jw);
    }
#else
    auto addIfExists = [&](const std::string& p) {
        if (fs::exists(p) && !fs::is_directory(p))
            result.push_back(p);
    };
    addIfExists("/usr/bin/java");
    addIfExists("/usr/local/bin/java");
    const char* javaHome = getenv("JAVA_HOME");
    if (javaHome) {
        std::string jh = std::string(javaHome) + "/bin/java";
        addIfExists(jh);
    }
    for (auto& jvmBase : std::vector<std::string>{"/usr/lib/jvm", "/usr/local/lib/jvm"}) {
        if (fs::exists(jvmBase)) {
            for (auto& e : fs::directory_iterator(jvmBase)) {
                std::string java = e.path().string() + "/bin/java";
                if (fs::exists(java)) result.push_back(java);
            }
        }
    }
    for (auto& optEntry : std::vector<std::string>{"/opt/java", "/opt/jdk", "/opt/jre"}) {
        if (!fs::exists(optEntry)) continue;
        for (auto& e : fs::directory_iterator(optEntry)) {
            std::string java = e.path().string() + "/bin/java";
            if (fs::exists(java)) result.push_back(java);
        }
    }
    const char* home = getenv("HOME");
    if (home) {
        std::string sdkman = std::string(home) + "/.sdkman/candidates/java";
        if (fs::exists(sdkman)) {
            for (auto& e : fs::directory_iterator(sdkman)) {
                if (!e.is_directory()) continue;
                std::string java = e.path().string() + "/bin/java";
                if (fs::exists(java)) result.push_back(java);
            }
        }
    }
#endif
    return result;
}
