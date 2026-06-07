#include <zlib.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLen;
    uint16_t extraLen;
};

struct ZipCentralDir {
    uint32_t signature;
    uint16_t versionMade;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLen;
    uint16_t extraLen;
    uint16_t commentLen;
    uint16_t diskStart;
    uint16_t internalAttr;
    uint32_t externalAttr;
    uint32_t localOffset;
};

struct ZipEOCD {
    uint32_t signature;
    uint16_t diskNum;
    uint16_t startDisk;
    uint16_t diskEntries;
    uint16_t totalEntries;
    uint32_t centralDirSize;
    uint32_t centralDirOffset;
    uint16_t commentLen;
};
#pragma pack(pop)

static bool unzipFile(const std::string& zipPath, const std::string& destDir) {
    std::ifstream f(zipPath, std::ios::binary | std::ios::ate);
    if (!f) return false;

    std::streamsize fileSize = f.tellg();
    f.seekg(0, std::ios::beg);

    std::vector<char> data((size_t)fileSize);
    if (!f.read(data.data(), fileSize)) return false;
    f.close();

    const uint8_t* buf = (const uint8_t*)data.data();
    size_t sz = (size_t)fileSize;

    uint32_t eocdOffset = 0;
    for (size_t i = sz >= 22 ? sz - 22 : 0; i != SIZE_MAX; i--) {
        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x05 && buf[i+3] == 0x06) {
            eocdOffset = (uint32_t)i;
            break;
        }
    }
    if (eocdOffset == 0) return false;

    const ZipEOCD* eocd = (const ZipEOCD*)(buf + eocdOffset);
    uint32_t cdOffset = eocd->centralDirOffset;
    uint16_t entries  = eocd->totalEntries;

    const uint8_t* cdPtr = buf + cdOffset;

    for (uint16_t i = 0; i < entries; i++) {
        const ZipCentralDir* cd = (const ZipCentralDir*)cdPtr;
        if (cd->signature != 0x02014B50) break;

        std::string name((const char*)(cdPtr + sizeof(ZipCentralDir)), cd->fileNameLen);
        cdPtr += sizeof(ZipCentralDir) + cd->fileNameLen + cd->extraLen + cd->commentLen;

        bool isDir = (!name.empty() && (name.back() == '/' || name.back() == '\\'));

        std::string outPath = destDir + "/" + name;
        for (char& c : outPath) if (c == '\\') c = '/';

        if (isDir) {
            fs::create_directories(outPath);
            continue;
        }

        fs::create_directories(fs::path(outPath).parent_path());

        const uint8_t* local = buf + cd->localOffset;
        const ZipLocalHeader* lh = (const ZipLocalHeader*)local;
        if (lh->signature != 0x04034B50) continue;

        const uint8_t* compData = local + sizeof(ZipLocalHeader) + lh->fileNameLen + lh->extraLen;

        std::ofstream outFile(outPath, std::ios::binary);
        if (!outFile) continue;

        if (cd->compression == 0) {
            outFile.write((const char*)compData, cd->compressedSize);
        } else if (cd->compression == 8) {
            std::vector<uint8_t> decomp(cd->uncompressedSize);
            z_stream zs = {};
            zs.next_in   = (Bytef*)compData;
            zs.avail_in  = cd->compressedSize;
            zs.next_out  = decomp.data();
            zs.avail_out = cd->uncompressedSize;

            if (inflateInit2(&zs, -MAX_WBITS) == Z_OK) {
                inflate(&zs, Z_FINISH);
                inflateEnd(&zs);
                outFile.write((const char*)decomp.data(), zs.total_out);
            }
        }
    }
    return true;
}

static void waitForPid(unsigned long pid) {
#ifdef _WIN32
    HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!hProc) return;
    WaitForSingleObject(hProc, 60000);
    CloseHandle(hProc);
#else
    waitpid((pid_t)pid, nullptr, 0);
#endif
}

static void sleepMs(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

static void launchApp(const std::string& exePath, const std::string& workDir) {
#ifdef _WIN32
    std::string exeWin = exePath;
    std::string dirWin = workDir;
    for (char& c : exeWin) if (c == '/') c = '\\';
    for (char& c : dirWin) if (c == '/') c = '\\';

    std::string cmd = "\"" + exeWin + "\"";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(0);

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);

    CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                   0, nullptr, dirWin.c_str(), &si, &pi);

    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread)  CloseHandle(pi.hThread);
#else
    pid_t child = fork();
    if (child == 0) {
        if (chdir(workDir.c_str()) != 0) _exit(1);
        char* args[] = { const_cast<char*>(exePath.c_str()), nullptr };
        execv(exePath.c_str(), args);
        _exit(1);
    }
#endif
}

static std::string getArg(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == key)
            return std::string(argv[i + 1]);
    }
    return "";
}

int main(int argc, char** argv) {
    std::string pidStr  = getArg(argc, argv, "--pid");
    std::string zipPath = getArg(argc, argv, "--zip");
    std::string destDir = getArg(argc, argv, "--dir");

    if (pidStr.empty() || zipPath.empty() || destDir.empty())
        return 1;

    unsigned long pid = std::stoul(pidStr);

    waitForPid(pid);

    sleepMs(500);

    if (!unzipFile(zipPath, destDir))
        return 2;

    fs::remove(zipPath);

#ifdef _WIN32
    std::string exePath = destDir + "\\CompactLauncher.exe";
    for (char& c : exePath) if (c == '/') c = '\\';
#else
    std::string exePath = destDir + "/CompactLauncher";
#endif

    launchApp(exePath, destDir);

    return 0;
}
