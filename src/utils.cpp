#include "launcher.h"
#include "json_parser.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <wininet.h>
  #include <wincrypt.h>
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
static size_t curlWriteFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
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
#ifdef _WIN32
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    CryptAcquireContextA(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
    CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
    CryptHashData(hHash, (BYTE*)input.data(), (DWORD)input.size(), 0);
    BYTE hash[16]; DWORD hashLen = 16;
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    std::ostringstream ss;
    for (int i = 0; i < 16; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
#else
    auto F = [](uint32_t x, uint32_t y, uint32_t z){ return (x & y) | (~x & z); };
    auto G = [](uint32_t x, uint32_t y, uint32_t z){ return (x & z) | (y & ~z); };
    auto H = [](uint32_t x, uint32_t y, uint32_t z){ return x ^ y ^ z; };
    auto I = [](uint32_t x, uint32_t y, uint32_t z){ return y ^ (x | ~z); };
    auto RL = [](uint32_t x, uint32_t n) -> uint32_t { return (x << n) | (x >> (32 - n)); };

    static const uint32_t T[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const uint32_t S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
        5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
        6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };

    uint64_t bitLen = (uint64_t)input.size() * 8;
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 0; i < 8; i++) msg.push_back((uint8_t)(bitLen >> (8 * i)));

    uint32_t a0=0x67452301, b0=0xefcdab89, c0=0x98badcfe, d0=0x10325476;

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++)
            M[i] = (uint32_t)msg[off+i*4] | ((uint32_t)msg[off+i*4+1]<<8) |
                   ((uint32_t)msg[off+i*4+2]<<16) | ((uint32_t)msg[off+i*4+3]<<24);
        uint32_t A=a0, B=b0, C=c0, D=d0;
        for (int i = 0; i < 64; i++) {
            uint32_t f, g;
            if      (i < 16) { f = F(B,C,D); g = i; }
            else if (i < 32) { f = G(B,C,D); g = (5*i+1)%16; }
            else if (i < 48) { f = H(B,C,D); g = (3*i+5)%16; }
            else             { f = I(B,C,D); g = (7*i)%16;   }
            uint32_t tmp = D; D = C; C = B;
            B = B + RL(A + f + T[i] + M[g], S[i]);
            A = tmp;
        }
        a0+=A; b0+=B; c0+=C; d0+=D;
    }

    uint8_t digest[16];
    auto put = [](uint8_t* p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; };
    put(digest+0,a0); put(digest+4,b0); put(digest+8,c0); put(digest+12,d0);

    std::ostringstream ss;
    for (int i = 0; i < 16; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return ss.str();
#endif
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
    std::sort(result.begin(), result.end(), std::greater<>());
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
    std::vector<std::string> candidates = {
        "/usr/bin/java", "/usr/local/bin/java", "/usr/lib/jvm"
    };
    for (auto& c : candidates) {
        if (fs::exists(c) && !fs::is_directory(c))
            result.push_back(c);
    }
    if (fs::exists("/usr/lib/jvm")) {
        for (auto& e : fs::directory_iterator("/usr/lib/jvm")) {
            std::string java = e.path().string() + "/bin/java";
            if (fs::exists(java)) result.push_back(java);
        }
    }
#endif
    return result;
}
