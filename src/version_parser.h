#pragma once
#include "launcher.h"
#include "json_parser.h"
#include <string>
#include <vector>
#include <set>
#include <sstream>

static bool libraryAllowed(const JsonValue& lib) {
    if (!lib.has("rules")) return true;
    const auto& rules = lib["rules"];
    bool allowed = false;
#ifdef _WIN32
    const std::string currentOs = "windows";
#elif defined(__APPLE__)
    const std::string currentOs = "osx";
#else
    const std::string currentOs = "linux";
#endif
    for (size_t k = 0; k < rules.arr.size(); k++) {
        const auto& rule = rules[k];
        std::string action = rule["action"].asString();
        bool hasOs = rule.has("os");
        std::string osName = hasOs ? rule["os"]["name"].asString() : "";
        if (action == "allow") {
            if (!hasOs) allowed = true;
            else if (osName == currentOs) allowed = true;
        } else {
            if (osName == currentOs) { allowed = false; break; }
        }
    }
    return allowed;
}

struct LibraryName {
    std::string group, artifact, version, classifier;

    std::string toPath(bool native = false) const {
        std::string g = group;
        for (char& c : g) if (c == '.') c = '/';
        std::string base = g + "/" + artifact + "/" + version + "/" + artifact + "-" + version;
        if (!classifier.empty()) base += "-" + classifier;
        if (native && classifier.empty()) {
#ifdef _WIN32
            base += "-natives-windows";
#elif defined(__APPLE__)
            base += "-natives-osx";
#else
            base += "-natives-linux";
#endif
        }
        return base + ".jar";
    }
    std::string groupArtifactPath() const {
        std::string g = group;
        for (char& c : g) if (c == '.') c = '/';
        return g + "/" + artifact;
    }
};

static LibraryName parseLibName(const std::string& name) {
    LibraryName r;
    std::istringstream ss(name);
    std::getline(ss, r.group, ':');
    std::getline(ss, r.artifact, ':');
    std::getline(ss, r.version, ':');
    std::getline(ss, r.classifier);
    return r;
}

static bool isNativesClassifier(const std::string& classifier) {
    return classifier.find("natives-") == 0 ||
           classifier.find("linux") != std::string::npos ||
           classifier.find("osx") != std::string::npos ||
           classifier.find("macos") != std::string::npos;
}

static bool isWindowsNativesClassifier(const std::string& classifier) {
    if (classifier.find("natives-windows") != std::string::npos) return true;
    if (classifier.find("natives-linux") != std::string::npos)   return false;
    if (classifier.find("natives-macos") != std::string::npos)   return false;
    if (classifier.find("natives-osx") != std::string::npos)     return false;
    if (classifier.find("linux") != std::string::npos)           return false;
    if (classifier.find("osx") != std::string::npos)             return false;
    if (classifier.find("aarch") != std::string::npos)           return false;
    return false;
}

inline std::vector<LibraryInfo> parseLibraries(
    const std::string& workDir,
    const JsonValue& json,
    const std::string& clientVersion,
    bool prepareDownload = false,
    const std::set<std::string>& alreadyIncluded = {})
{
    std::vector<LibraryInfo> result;
    const auto& libsArr = json["libraries"];
    for (size_t i = 0; i < libsArr.arr.size(); i++) {
        const auto& lib = libsArr[i];
        if (!libraryAllowed(lib)) continue;
        std::string nameStr = lib["name"].asString();
        LibraryName ln = parseLibName(nameStr);
        std::string ga = ln.groupArtifactPath();
        if (alreadyIncluded.count(ga)) continue;

        bool hasNativesField = lib.has("natives");
        const auto& downloads = lib["downloads"];

        if (hasNativesField) {
#ifdef _WIN32
            std::string nativeKey = lib["natives"]["windows"].asString("natives-windows");
#elif defined(__APPLE__)
            std::string nativeKey = lib["natives"]["osx"].asString(lib["natives"]["macos"].asString("natives-osx"));
#else
            std::string nativeKey = lib["natives"]["linux"].asString("natives-linux");
#endif
            size_t archPos = nativeKey.find("${arch}");
            if (archPos != std::string::npos)
                nativeKey.replace(archPos, 7, "64");
            LibraryInfo ni;
            ni.isNative = true;
            if (downloads.has("classifiers") && downloads["classifiers"].has(nativeKey)) {
                const auto& cls = downloads["classifiers"][nativeKey];
                ni.downloadUrl  = cls["url"].asString();
                ni.size         = cls["size"].asInt();
                ni.jarPath      = cls["path"].asString(
                    ln.groupArtifactPath() + "/" + ln.version + "/" + ln.artifact + "-" + ln.version + "-" + nativeKey + ".jar");
            } else {
                ni.jarPath = ln.toPath(false);
                ni.downloadUrl = "https://libraries.minecraft.net/" + ni.jarPath;
            }
            result.push_back(std::move(ni));
        } else if (!ln.classifier.empty() && isNativesClassifier(ln.classifier)) {
#ifdef _WIN32
            if (!isWindowsNativesClassifier(ln.classifier)) continue;
#elif defined(__APPLE__)
            if (ln.classifier.find("natives-osx") == std::string::npos &&
                ln.classifier.find("natives-macos") == std::string::npos &&
                ln.classifier.find("osx") == std::string::npos) continue;
#else
            if (ln.classifier.find("natives-linux") == std::string::npos &&
                ln.classifier.find("linux") == std::string::npos) continue;
#endif
            LibraryInfo ni;
            ni.isNative = true;
            if (downloads.has("artifact")) {
                const auto& art = downloads["artifact"];
                ni.downloadUrl = art["url"].asString();
                ni.size        = art["size"].asInt();
                ni.jarPath     = art["path"].asString(ln.toPath());
            } else {
                ni.jarPath     = ln.toPath();
                ni.downloadUrl = "https://libraries.minecraft.net/" + ni.jarPath;
            }
            result.push_back(std::move(ni));
        } else if (ln.classifier.empty()) {
            LibraryInfo li;
            li.isNative = false;
            if (downloads.has("artifact")) {
                const auto& art = downloads["artifact"];
                li.downloadUrl = art["url"].asString();
                li.size        = art["size"].asInt();
                li.jarPath     = art["path"].asString(ln.toPath());
            } else if (lib.has("url")) {
                std::string base = lib["url"].asString();
                if (!base.empty() && base.back() != '/') base += '/';
                li.jarPath     = ln.toPath();
                li.downloadUrl = base + li.jarPath;
            } else {
                li.jarPath     = ln.toPath();
                li.downloadUrl = "https://libraries.minecraft.net/" + li.jarPath;
            }
            result.push_back(std::move(li));
        }
    }
    return result;
}

inline std::string buildClasspath(
    const std::string& workDir,
    const std::vector<LibraryInfo>& libs,
    const std::string& clientJar)
{
    std::string cp;
    for (auto& lib : libs) {
        if (!lib.isNative) {
            cp += workDir + "/libraries/" + lib.jarPath;
#ifdef _WIN32
            cp += ";";
#else
            cp += ":";
#endif
        }
    }
    cp += clientJar;
    return cp;
}

#ifdef _WIN32
#include <windows.h>
#include <zlib.h>



#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t compression;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t extraLen;
};
#pragma pack(pop)

static bool extractZipNative(const std::string& zipPath, const std::string& destDir) {
    FILE* zf = fopen(zipPath.c_str(), "rb");
    if (!zf) {
        LOG("extractZip: cannot open " + zipPath);
        return false;
    }

    bool anyOk = false;

    while (true) {
        ZipLocalHeader hdr = {};
        if (fread(&hdr, sizeof(hdr), 1, zf) != 1) break;
        if (hdr.signature != 0x04034b50) break;

        std::string name(hdr.nameLen, '\0');
        if (hdr.nameLen > 0 && fread(&name[0], 1, hdr.nameLen, zf) != hdr.nameLen) break;

        if (hdr.extraLen > 0) fseek(zf, hdr.extraLen, SEEK_CUR);

        bool isDir = (!name.empty() && name.back() == '/');
        bool isMeta = (name.find("META-INF") != std::string::npos);

        if (isDir || isMeta) {
            fseek(zf, hdr.compSize, SEEK_CUR);
            continue;
        }

        bool isNativeFile = (name.find(".dll")    != std::string::npos ||
                             name.find(".so")     != std::string::npos ||
                             name.find(".jnilib") != std::string::npos ||
                             name.find(".dylib")  != std::string::npos);
        if (!isNativeFile) {
            fseek(zf, hdr.compSize, SEEK_CUR);
            continue;
        }

        std::string baseName = name;
        size_t slash = name.rfind('/');
        if (slash != std::string::npos) baseName = name.substr(slash + 1);

        std::string destPath = destDir + "/" + baseName;

        if (fs::exists(destPath)) {
            fseek(zf, hdr.compSize, SEEK_CUR);
            anyOk = true;
            continue;
        }

        std::vector<uint8_t> compData(hdr.compSize);
        if (hdr.compSize > 0 && fread(compData.data(), 1, hdr.compSize, zf) != hdr.compSize) break;

        FILE* out = fopen(destPath.c_str(), "wb");
        if (!out) {
            LOG("extractZip: cannot write " + destPath);
            continue;
        }

        bool ok = false;

        if (hdr.compression == 0) {
            ok = (fwrite(compData.data(), 1, hdr.compSize, out) == hdr.compSize);
        } else if (hdr.compression == 8) {
            std::vector<uint8_t> uncompData(hdr.uncompSize);
            z_stream strm = {};
            strm.next_in   = compData.data();
            strm.avail_in  = hdr.compSize;
            strm.next_out  = uncompData.data();
            strm.avail_out = hdr.uncompSize;
            if (inflateInit2(&strm, -MAX_WBITS) == Z_OK) {
                int ret = inflate(&strm, Z_FINISH);
                inflateEnd(&strm);
                if (ret == Z_STREAM_END) {
                    ok = (fwrite(uncompData.data(), 1, hdr.uncompSize, out) == hdr.uncompSize);
                }
            }
        }

        fclose(out);

        if (!ok) {
            fs::remove(destPath);
            LOG("extractZip: failed to extract " + baseName);
        } else {
            LOG("extractZip: extracted " + baseName);
            anyOk = true;
        }
    }

    fclose(zf);
    return anyOk;
}

inline void extractNatives(
    const std::string& workDir,
    const std::string& clientVersion,
    const std::vector<LibraryInfo>& libs)
{
    std::string nativesDir = workDir + "/versions/" + clientVersion + "/natives";
    fs::create_directories(nativesDir);
    for (auto& lib : libs) {
        if (!lib.isNative) continue;
        std::string jarPath = workDir + "/libraries/" + lib.jarPath;
        if (!fs::exists(jarPath)) continue;
        extractZipNative(jarPath, nativesDir);
    }
    std::string metaInf = nativesDir + "/META-INF";
    std::error_code ec;
    if (fs::exists(metaInf, ec))
        fs::remove_all(metaInf, ec);
}

#else
inline void extractNatives(
    const std::string& workDir,
    const std::string& clientVersion,
    const std::vector<LibraryInfo>& libs)
{
    std::string nativesDir = workDir + "/versions/" + clientVersion + "/natives";
    fs::create_directories(nativesDir);
    for (auto& lib : libs) {
        if (!lib.isNative) continue;
        std::string jarPath = workDir + "/libraries/" + lib.jarPath;
        if (!fs::exists(jarPath)) continue;
        std::string cmd = "unzip -o '" + jarPath + "' -d '" + nativesDir + "' '*.so' '*.jnilib' '*.dylib' 2>/dev/null";
        system(cmd.c_str());
    }
}
#endif

inline std::vector<DownloadEntry> buildLibraryDownloadList(
    const std::string& workDir,
    const std::vector<LibraryInfo>& libs,
    bool forceRedownload = false)
{
    std::vector<DownloadEntry> entries;
    for (auto& lib : libs) {
        if (lib.downloadUrl.empty()) continue;
        std::string dest = workDir + "/libraries/" + lib.jarPath;
        long long existing = fileSize(dest);
        if (!forceRedownload && existing > 0 && (lib.size == 0 || existing == lib.size))
            continue;
        mkdirRecursive(fs::path(dest).parent_path().string());
        entries.push_back({lib.downloadUrl, dest, lib.size});
    }
    return entries;
}
