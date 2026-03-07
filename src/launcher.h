#pragma once

/*
    CLauncher [Compact Launcher]
    author: vulkansdk (also known as vu1k4n, or just vulkan.)
*/


#include <QString>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

constexpr const char* CFG_NAME             = "Player/Name";
constexpr const char* CFG_RAM              = "Player/Ram";
constexpr const char* CFG_CHOSEN_VER       = "Game/ChosenVersion";
constexpr const char* CFG_JAVA_PATH        = "Java/CustomPath";
constexpr const char* CFG_USE_CUSTOM_JAVA  = "Java/UseCustom";
constexpr const char* CFG_USE_CUSTOM_ARGS  = "Args/UseCustom";
constexpr const char* CFG_CUSTOM_ARGS      = "Args/Custom";
constexpr const char* CFG_DL_THREADS       = "Download/Threads";
constexpr const char* CFG_ASYNC_DL         = "Download/Async";
constexpr const char* CFG_DL_MISSING_LIBS  = "Download/MissingLibs";
constexpr const char* CFG_KEEP_OPEN        = "Launcher/KeepOpen";
constexpr const char* CFG_SAVE_LAUNCH_STR  = "Launcher/SaveLaunchString";
constexpr const char* CFG_SHOW_ALL_VER     = "Download/ShowAllVersions";
constexpr const char* CFG_REDOWNLOAD       = "Download/RedownloadAll";

constexpr const char* DEFAULT_NAME         = "Player";
constexpr const char* DEFAULT_RAM          = "2048";
constexpr const char* DEFAULT_JAVA_PATH    = "";
constexpr int         DEFAULT_DL_THREADS   = 8;
constexpr bool        DEFAULT_ASYNC_DL     = true;
constexpr bool        DEFAULT_DL_MISSING   = true;
constexpr bool        DEFAULT_KEEP_OPEN    = false;
constexpr bool        DEFAULT_SAVE_LAUNCH  = false;
constexpr bool        DEFAULT_SHOW_ALL     = false;
constexpr bool        DEFAULT_REDOWNLOAD   = false;
constexpr bool        DEFAULT_USE_CUSTOM_JAVA = false;
constexpr bool        DEFAULT_USE_CUSTOM_ARGS = false;

constexpr const char* DEFAULT_JVM_ARGS_NEW =
    "-Xss1M -XX:+UnlockExperimentalVMOptions -XX:+UseG1GC "
    "-XX:G1NewSizePercent=20 -XX:G1ReservePercent=20 "
    "-XX:MaxGCPauseMillis=50 -XX:G1HeapRegionSize=32M";

constexpr const char* DEFAULT_JVM_ARGS_OLD =
    "-XX:+UseConcMarkSweepGC -XX:+CMSIncrementalMode "
    "-XX:-UseAdaptiveSizePolicy -Xmn128M";

constexpr const char* VERSIONS_MANIFEST_URL =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

constexpr const char* LAUNCHER_VERSION = "1.1.0";

struct DownloadEntry {
    std::string url;
    std::string localPath;
    long long   expectedSize = 0;
};

struct LibraryInfo {
    std::string jarPath;
    std::string downloadUrl;
    long long   size = 0;
    bool        isNative = false;
    bool        skip = false;
};


inline const std::atomic<bool> g_notCancelled{false};
bool        httpDownloadToFile(const std::string& url, const std::string& dest,
                               const std::atomic<bool>& cancelled = g_notCancelled);
std::string httpGetString(const std::string& url);

void         mkdirRecursive(const std::string& path);
std::string  readFileText(const std::string& path);
bool         writeFileText(const std::string& path, const std::string& data);
long long    fileSize(const std::string& path);

std::string offlineUUID(const std::string& name);

std::vector<std::string> listInstalledVersions(const std::string& workDir);
std::vector<std::string> findJavaInstalls();

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

#include <iomanip>
#include <ctime>
class Logger {
public:
    static Logger& instance() { static Logger l; return l; }

    void open(const std::string& path) {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_file.open(path, std::ios::out | std::ios::app);
        write_unlocked("Compact Launcher " + std::string(LAUNCHER_VERSION) + " started");
    }

    void write(const std::string& msg) {
        std::lock_guard<std::mutex> lk(m_mtx);
        write_unlocked(msg);
    }

private:
    std::ofstream m_file;
    std::mutex    m_mtx;

    void write_unlocked(const std::string& msg) {
        std::time_t t = std::time(nullptr);
        char ts[32];
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);
        std::string line = std::string("[") + ts + "] " + msg;
        if (m_file.is_open()) { m_file << line << "\n"; m_file.flush(); }
#ifdef _WIN32
        OutputDebugStringA((line + "\n").c_str());
#endif
    }
};

#define LOG(msg) Logger::instance().write(std::string(msg))
#define LOGF(msg) Logger::instance().write(msg)
