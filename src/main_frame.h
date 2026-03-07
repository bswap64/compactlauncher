#pragma once
#include "launcher.h"
#include "json_parser.h"
#include "version_parser.h"
#include "download_manager.h"
#include "settings_dialog.h"
#include "downloader_dialog.h"
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSettings>
#include <QApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QCoreApplication>
#include <QSpinBox>
#include <set>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

class MainFrame : public QMainWindow {
    Q_OBJECT
public:
    explicit MainFrame(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Compact Launcher");
        setFixedSize(280, 290);

        m_workDir = QCoreApplication::applicationDirPath().toStdString();
        for (char& c : m_workDir) if (c == '\\') c = '/';

        m_cfg = new QSettings("Compact Launcher", "Compact Launcher", this);

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* sizer = new QVBoxLayout(central);
        sizer->setContentsMargins(8, 8, 8, 8);
        sizer->setSpacing(8);

        m_nameCtrl = new QLineEdit(m_cfg->value(CFG_NAME, DEFAULT_NAME).toString());
        m_nameCtrl->setMaxLength(16);
        m_nameCtrl->setToolTip("Player name (3-16 characters, no spaces)");
        m_nameCtrl->setPlaceholderText("Nickname");
        sizer->addWidget(m_nameCtrl);

        {
            int maxRam = getTotalRamMB();
            int curRam = m_cfg->value(CFG_RAM, DEFAULT_RAM).toInt();
            if (curRam < 350)   curRam = 350;
            if (curRam > maxRam) curRam = maxRam;
            m_ramCtrl = new QSpinBox();
            m_ramCtrl->setRange(350, maxRam);
            m_ramCtrl->setValue(curRam);
            m_ramCtrl->setSuffix(" MB");
            m_ramCtrl->setToolTip(
                QString("RAM to allocate (350 \u2013 %1 MB)").arg(maxRam));
        }
        sizer->addWidget(m_ramCtrl);

        m_versionCombo = new QComboBox();
        m_versionCombo->setToolTip("Select installed Minecraft version");
        sizer->addWidget(m_versionCombo);

        m_javaCombo = new QComboBox();
        m_javaCombo->setToolTip("Select Java installation");
        sizer->addWidget(m_javaCombo);

        m_playBtn = new QPushButton("▶  PLAY");
        QFont boldFont = m_playBtn->font();
        boldFont.setPointSize(12);
        boldFont.setBold(true);
        m_playBtn->setFont(boldFont);
        sizer->addWidget(m_playBtn);

        auto* btnRow = new QHBoxLayout();
        m_downloadBtn = new QPushButton("Download");
        m_deleteBtn   = new QPushButton("Delete");
        m_settingsBtn = new QPushButton("Settings");
        btnRow->addWidget(m_downloadBtn);
        btnRow->addWidget(m_deleteBtn);
        btnRow->addWidget(m_settingsBtn);
        sizer->addLayout(btnRow);

        m_statusLabel = new QLabel("Ready");
        QFont smallFont = m_statusLabel->font();
        smallFont.setPointSize(8);
        m_statusLabel->setFont(smallFont);
        sizer->addWidget(m_statusLabel);

        auto* infoRow = new QHBoxLayout();
        auto* verLabel = new QLabel(QString("v") + LAUNCHER_VERSION);
        QFont tinyFont = verLabel->font();
        tinyFont.setPointSize(7);
        verLabel->setFont(tinyFont);
        infoRow->addStretch(1);
        infoRow->addWidget(verLabel);
        sizer->addLayout(infoRow);

        refreshVersions();
        refreshJavaList();

        connect(m_playBtn,     &QPushButton::clicked, this, &MainFrame::onPlay);
        connect(m_downloadBtn, &QPushButton::clicked, this, &MainFrame::onDownloader);
        connect(m_deleteBtn,   &QPushButton::clicked, this, &MainFrame::onDeleteVersion);
        connect(m_settingsBtn, &QPushButton::clicked, this, &MainFrame::onSettings);
    }

    ~MainFrame() {
        if (m_dlManager) m_dlManager->cancel();
    }

private:
    void refreshVersions() {
        m_versionCombo->clear();
        auto versions = listInstalledVersions(m_workDir);
        std::string chosen = m_cfg->value(CFG_CHOSEN_VER, "").toString().toStdString();
        for (auto& v : versions) m_versionCombo->addItem(QString::fromStdString(v));
        if (!versions.empty()) {
            int sel = 0;
            for (size_t i = 0; i < versions.size(); i++)
                if (versions[i] == chosen) { sel = (int)i; break; }
            m_versionCombo->setCurrentIndex(sel);
            m_playBtn->setEnabled(true);
            m_deleteBtn->setEnabled(true);
        } else {
            m_versionCombo->addItem("No versions found");
            m_versionCombo->setCurrentIndex(0);
            m_playBtn->setEnabled(false);
            m_deleteBtn->setEnabled(false);
        }
    }

    void refreshJavaList() {
        m_javaCombo->clear();
        if (m_cfg->value(CFG_USE_CUSTOM_JAVA, DEFAULT_USE_CUSTOM_JAVA).toBool()) {
            m_javaCombo->addItem("Custom Java (see Settings)");
            m_javaCombo->setCurrentIndex(0);
            m_javaCombo->setEnabled(false);
            return;
        }
        m_javaCombo->setEnabled(true);
        auto javas = findJavaInstalls();
        for (auto& j : javas) m_javaCombo->addItem(QString::fromStdString(j));
        if (!javas.empty()) {
            m_javaCombo->setCurrentIndex(0);
            m_playBtn->setEnabled(!m_versionCombo->currentText().contains("No versions"));
        } else {
            m_javaCombo->addItem("Java not found — install Java first");
            m_javaCombo->setCurrentIndex(0);
            m_javaCombo->setEnabled(false);
            m_playBtn->setEnabled(false);
        }
    }

    static int getJavaMajorVersion(const std::string& javaExe) {
        if (javaExe.empty() || !fs::exists(javaExe)) return 0;
        std::string out;
#ifdef _WIN32

        HANDLE hReadOut, hWriteOut;
        SECURITY_ATTRIBUTES sa = {};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        if (!CreatePipe(&hReadOut, &hWriteOut, &sa, 0)) return 0;
        SetHandleInformation(hReadOut, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = {};
        PROCESS_INFORMATION pi = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWriteOut;
        si.hStdError  = hWriteOut; // java -version writes to stderr
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

        std::string cmd = "\"" + javaExe + "\" -version";
        std::vector<char> buf(cmd.begin(), cmd.end());
        buf.push_back(0);

        if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hReadOut);
            CloseHandle(hWriteOut);
            return 0;
        }
        CloseHandle(hWriteOut);
        CloseHandle(pi.hThread);

        char rbuf[256];
        DWORD read;
        while (ReadFile(hReadOut, rbuf, sizeof(rbuf) - 1, &read, nullptr) && read > 0) {
            rbuf[read] = 0;
            out += rbuf;
        }
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(hReadOut);
#else
        std::string cmd = "\"" + javaExe + "\" -version 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return 0;
        char rbuf[256] = {};
        while (fgets(rbuf, sizeof(rbuf), p)) out += rbuf;
        pclose(p);
#endif
        const char* v = strstr(out.c_str(), "version \"");
        if (!v) return 0;
        v += 9;
        if (strncmp(v, "1.", 2) == 0) v += 2;
        return atoi(v);
    }

    static int requiredJavaForVersion(const std::string& versionJsonStr) {
        if (!versionJsonStr.empty()) {
            try {
                auto j = parseJson(versionJsonStr);
                if (j.has("javaVersion")) {
                    int v = (int)j["javaVersion"]["majorVersion"].asInt(0);
                    if (v > 0) return v;
                }
            } catch (...) {}
        }
        return 8;
    }

private slots:
    void onPlay() {
        std::string name    = m_nameCtrl->text().toStdString();

        std::string version = m_versionCombo->currentText().toStdString();
        std::string java;

        if (m_cfg->value(CFG_USE_CUSTOM_JAVA, DEFAULT_USE_CUSTOM_JAVA).toBool())
            java = m_cfg->value(CFG_JAVA_PATH, DEFAULT_JAVA_PATH).toString().toStdString();
        else
            java = m_javaCombo->currentText().toStdString();

        name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
        if (name.size() < 3) { QMessageBox::critical(this, "Error", "Name must be at least 3 characters!"); return; }
        if (name.size() > 16) name = name.substr(0, 16);

        long ram = m_ramCtrl->value();
        

        if (version.empty() || version.find("No versions") != std::string::npos) {
            QMessageBox::critical(this, "Error", "No version selected!"); return;
        }
        if (java.empty() || !fs::exists(java)) {
            QMessageBox::critical(this, "Error", "Java not found!\nCheck Settings → Custom Java path."); return;
        }

        m_cfg->setValue(CFG_NAME, QString::fromStdString(name));
        m_cfg->setValue(CFG_RAM, QString::fromStdString(std::to_string(ram)));
        m_cfg->setValue(CFG_CHOSEN_VER, QString::fromStdString(version));
        m_cfg->sync();

        m_statusLabel->setText(QString("Launching %1...").arg(QString::fromStdString(version)));

        std::thread([this, name, ram, version, java]() {
            try {
                std::string vjson = readFileText(m_workDir + "/versions/" + version + "/" + version + ".json");
                int required = requiredJavaForVersion(vjson);
                int actual   = getJavaMajorVersion(java);
                if (actual < required) {
                    QMetaObject::invokeMethod(this, [this, required]() {
                        QMessageBox::critical(this, "Java Required",
                            QString("Java %1 is not installed or not selected.\nPlease install Java %1 and select it in the launcher.").arg(required));
                        m_statusLabel->setText("Ready");
                    }, Qt::QueuedConnection);
                    return;
                }

                std::string args = buildLaunchArgs(name, ram, version, java);
                if (args.empty()) return;

#ifdef _WIN32
                STARTUPINFOA si = {};
                PROCESS_INFORMATION pi = {};
                si.cb = sizeof(si);
                std::string cmd = "\"" + java + "\" " + args;
                std::vector<char> buf(cmd.begin(), cmd.end());
                buf.push_back(0);


                std::string nativesDir = m_workDir + "/versions/" + version + "/natives";

                std::string nativesDirWin = nativesDir;
                for (char& c : nativesDirWin) if (c == '/') c = '\\';

                char existingPath[32767] = {};
                GetEnvironmentVariableA("PATH", existingPath, sizeof(existingPath));
                std::string newPath = "PATH=" + nativesDirWin + ";" + std::string(existingPath);

                std::string oldPathStr = existingPath;
                SetEnvironmentVariableA("PATH", (nativesDirWin + ";" + oldPathStr).c_str());


                std::string versionDir = m_workDir + "/versions/" + version;
                std::string workDirWin = m_workDir;
                for (char& c : workDirWin) if (c == '/') c = '\\';

                if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                                   workDirWin.c_str(), &si, &pi)) {

                    SetEnvironmentVariableA("PATH", oldPathStr.c_str());
                    QMetaObject::invokeMethod(this, [this]() {
                        QMessageBox::critical(this, "Error", "Failed to launch Java!\nCheck Java path.");
                        m_statusLabel->setText("Launch failed");
                    }, Qt::QueuedConnection);
                    return;
                }

                SetEnvironmentVariableA("PATH", oldPathStr.c_str());
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
#else
                pid_t pid = fork();
                if (pid == 0) {
                    setsid();
                    std::vector<std::string> tokens;
                    std::istringstream iss(java + " " + args);
                    std::string tok;
                    while (iss >> std::quoted(tok)) tokens.push_back(tok);
                    std::vector<char*> argv2;
                    for (auto& t : tokens) argv2.push_back(const_cast<char*>(t.c_str()));
                    argv2.push_back(nullptr);
                    execvp(argv2[0], argv2.data());
                    _exit(1);
                } else if (pid < 0) {
                    QMetaObject::invokeMethod(this, [this]() {
                        QMessageBox::critical(this, "Error", "Failed to launch Java!");
                        m_statusLabel->setText("Launch failed");
                    }, Qt::QueuedConnection);
                    return;
                }
#endif

                if (m_cfg->value(CFG_SAVE_LAUNCH_STR, DEFAULT_SAVE_LAUNCH).toBool()) {
                    writeFileText(m_workDir + "/launch_string.txt", "\"" + java + "\" " + args);
                }

                writeFileText(m_workDir + "/launch_string.txt", "\"" + java + "\" " + args);

                QMetaObject::invokeMethod(this, [this]() {
                    m_statusLabel->setText("Game launched!");
                    if (!m_cfg->value(CFG_KEEP_OPEN, DEFAULT_KEEP_OPEN).toBool())
                        close();
                }, Qt::QueuedConnection);

            } catch (const std::exception& e) {
                std::string msg = e.what();
                QMetaObject::invokeMethod(this, [this, msg]() {
                    QMessageBox::critical(this, "Launch Error", QString::fromStdString(msg));
                    m_statusLabel->setText(QString("Error: %1").arg(QString::fromStdString(msg)));
                }, Qt::QueuedConnection);
            }
        }).detach();
    }

    void onDeleteVersion() {
        std::string version = m_versionCombo->currentText().toStdString();
        if (version.empty() || version.find("No versions") != std::string::npos) return;

        auto reply = QMessageBox::question(this, "Delete Version",
            QString("Delete version \"%1\"?\nAll version files will be removed.")
                .arg(QString::fromStdString(version)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

        if (reply != QMessageBox::Yes) return;

        std::string versionDir = m_workDir + "/versions/" + version;
        std::error_code ec;
        if (fs::exists(versionDir, ec))
            fs::remove_all(versionDir, ec);

        m_statusLabel->setText(QString("Deleted %1").arg(QString::fromStdString(version)));
        refreshVersions();
    }

    void onDownloader() {
        m_statusLabel->setText("Fetching version manifest...");
        m_downloadBtn->setEnabled(false);

        std::thread([this]() {
            std::string manifest = httpGetString(VERSIONS_MANIFEST_URL);
            QMetaObject::invokeMethod(this, [this, manifest]() {
                m_statusLabel->setText("Ready");
                m_downloadBtn->setEnabled(true);

                if (manifest.empty()) {
                    QMessageBox::critical(this, "Network Error", "Cannot reach Mojang servers.\nCheck your internet connection.");
                    return;
                }

                DownloaderDialog dlg(this, m_cfg, m_workDir, manifest, [this]() {
                    QMetaObject::invokeMethod(this, [this]() { refreshVersions(); }, Qt::QueuedConnection);
                });
                dlg.exec();
            }, Qt::QueuedConnection);
        }).detach();
    }

    void onSettings() {
        SettingsDialog dlg(this, m_cfg);
        if (dlg.exec() == QDialog::Accepted) {
            refreshJavaList();
        }
    }

private:
    std::string buildLaunchArgs(
        const std::string& name,
        long ram,
        const std::string& version,
        const std::string& java)
    {
        std::string versionDir  = m_workDir + "/versions/" + version;
        std::string versionJson = versionDir + "/" + version + ".json";
        std::string jsonStr     = readFileText(versionJson);
        if (jsonStr.empty()) {
            QMetaObject::invokeMethod(this, [this]() {
                QMessageBox::critical(this, "Error", "Version JSON not found!");
                m_statusLabel->setText("Error");
            }, Qt::QueuedConnection);
            return "";
        }

        auto json = parseJson(jsonStr);

        std::string inheritsFrom = json["inheritsFrom"].asString();
        JsonValue baseJson;
        std::string baseVersion = version;
        if (!inheritsFrom.empty()) {
            std::string baseJsonStr = readFileText(m_workDir + "/versions/" + inheritsFrom + "/" + inheritsFrom + ".json");
            if (baseJsonStr.empty()) {
                QMetaObject::invokeMethod(this, [this, inheritsFrom]() {
                    QMessageBox::critical(this, "Error", QString("Missing base version: %1\nPlease download it first.").arg(QString::fromStdString(inheritsFrom)));
                    m_statusLabel->setText("Error");
                }, Qt::QueuedConnection);
                return "";
            }
            baseJson = parseJson(baseJsonStr);
            baseVersion = inheritsFrom;
        }

        std::string clientJar = versionDir + "/" + version + ".jar";
        if (!fs::exists(clientJar) && !inheritsFrom.empty()) {
            std::string baseJar = m_workDir + "/versions/" + inheritsFrom + "/" + inheritsFrom + ".jar";
            if (fs::exists(baseJar))
                fs::copy_file(baseJar, clientJar, fs::copy_options::overwrite_existing);
        }
        if (!fs::exists(clientJar)) {
            QMetaObject::invokeMethod(this, [this]() {
                QMessageBox::critical(this, "Error", "Client JAR not found! Download the version first.");
                m_statusLabel->setText("Error");
            }, Qt::QueuedConnection);
            return "";
        }

        long long releaseTime = 0;
        {
            const JsonValue& rt = inheritsFrom.empty() ? json : baseJson;
            std::string rtStr = rt["releaseTime"].asString();
            if (!rtStr.empty()) {
                int year = 0, month = 0;
                sscanf(rtStr.c_str(), "%d-%d", &year, &month);
                releaseTime = year * 365LL + month * 30;
            }
        }

        std::string assetsIndex;
        if (!inheritsFrom.empty() && baseJson.has("assets"))
            assetsIndex = baseJson["assets"].asString();
        else if (json.has("assets"))
            assetsIndex = json["assets"].asString();
        if (assetsIndex.empty()) {
            if (releaseTime > 0 && releaseTime < 734925) assetsIndex = "pre-1.6";
            else assetsIndex = "legacy";
        }

        std::string nativesPath = m_workDir + "/versions/" + baseVersion + "/natives";

        std::vector<LibraryInfo> allLibs;
        std::set<std::string> includedGA;

        auto addLibs = [&](const JsonValue& j, const std::string& ver) {
            auto libs = parseLibraries(m_workDir, j, ver, false, includedGA);
            for (auto& lib : libs) {
                includedGA.insert(fs::path(lib.jarPath).parent_path().parent_path().string());
                allLibs.push_back(lib);
            }
        };

        addLibs(json, version);
        if (!inheritsFrom.empty()) addLibs(baseJson, inheritsFrom);

        if (m_cfg->value(CFG_DL_MISSING_LIBS, DEFAULT_DL_MISSING).toBool()) {
            auto dlEntries = buildLibraryDownloadList(m_workDir, allLibs, false);
            if (!dlEntries.empty()) {
                m_dlManager = std::make_unique<DownloadManager>();
                m_dlManager->threads = m_cfg->value(CFG_DL_THREADS, DEFAULT_DL_THREADS).toInt();
                QMetaObject::invokeMethod(this, [this, count = dlEntries.size()]() {
                    m_statusLabel->setText(QString("Downloading %1 missing files...").arg(count));
                }, Qt::QueuedConnection);
                for (auto& e : dlEntries) {
                    mkdirRecursive(fs::path(e.localPath).parent_path().string());
                    httpDownloadToFile(e.url, e.localPath);
                }
            }
        }

        extractNatives(m_workDir, baseVersion, allLibs);

        std::string cp = buildClasspath(m_workDir, allLibs, clientJar);

        std::string mainClass = json["mainClass"].asString();
        if (mainClass.empty() && !inheritsFrom.empty())
            mainClass = baseJson["mainClass"].asString();

        std::string jvmArgs;
        std::string gameArgs;

        auto isRuleAllowed = [](const JsonValue& entry) -> bool {
            if (!entry.has("rules")) return true;
            const auto& rules = entry["rules"];
            bool allowed = false;
            for (size_t k = 0; k < rules.arr.size(); k++) {
                const auto& rule = rules[k];
                std::string action = rule["action"].asString();
                if (rule.has("features")) return false;
                bool hasOs = rule.has("os");
                if (hasOs) {
                    std::string osName = rule["os"]["name"].asString();
                    std::string osArch = rule["os"]["arch"].asString();
                    if (!osName.empty()) {
#ifdef _WIN32
                        const std::string curOs = "windows";
#elif defined(__APPLE__)
                        const std::string curOs = "osx";
#else
                        const std::string curOs = "linux";
#endif
                        if (action == "allow" && osName == curOs) allowed = true;
                        else if (action == "disallow" && osName == curOs) { allowed = false; break; }
                    } else if (!osArch.empty()) {
                        if (action == "allow" && osArch == "x86") allowed = false;
                    }
                } else {
                    if (action == "allow") allowed = true;
                    else { allowed = false; break; }
                }
            }
            return allowed;
        };

        auto appendArgValue = [](std::string& out, const JsonValue& val) {
            if (val.isString()) {
                out += " " + val.asString();
            } else if (val.isArray()) {
                for (size_t k = 0; k < val.arr.size(); k++)
                    if (val[k].isString()) out += " " + val[k].asString();
            }
        };

        auto collectArgs = [&](const JsonValue& j) {
            if (j.has("arguments")) {
                const auto& gameArr = j["arguments"]["game"];
                for (size_t i = 0; i < gameArr.arr.size(); i++) {
                    if (gameArr[i].isString()) {
                        gameArgs += " " + gameArr[i].asString();
                    } else if (gameArr[i].isObject() && isRuleAllowed(gameArr[i])) {
                        appendArgValue(gameArgs, gameArr[i]["value"]);
                    }
                }
                const auto& jvmArr = j["arguments"]["jvm"];
                for (size_t i = 0; i < jvmArr.arr.size(); i++) {
                    if (jvmArr[i].isString()) {
                        jvmArgs += " " + jvmArr[i].asString();
                    } else if (jvmArr[i].isObject() && isRuleAllowed(jvmArr[i])) {
                        appendArgValue(jvmArgs, jvmArr[i]["value"]);
                    }
                }
            } else if (j.has("minecraftArguments")) {
                gameArgs += " " + j["minecraftArguments"].asString();
            }
        };

        collectArgs(json);
        if (!inheritsFrom.empty()) collectArgs(baseJson);

        if (jvmArgs.empty()) {
            jvmArgs = "-Djava.library.path=\"" + nativesPath + "\" -cp \"" + cp + "\"";
        } else {
            auto replace = [](std::string s, const std::string& from, const std::string& to) {
                size_t p = 0;
                while ((p = s.find(from, p)) != std::string::npos) {
                    s.replace(p, from.size(), to);
                    p += to.size();
                }
                return s;
            };
            jvmArgs = replace(jvmArgs, "${natives_directory}", nativesPath);
            jvmArgs = replace(jvmArgs, "${launcher_name}", "\"Compact Launcher\"");
            jvmArgs = replace(jvmArgs, "${launcher_version}", LAUNCHER_VERSION);
            jvmArgs = replace(jvmArgs, "${classpath}", cp);
            jvmArgs = replace(jvmArgs, "${library_directory}", m_workDir + "/libraries");
#ifdef _WIN32
            jvmArgs = replace(jvmArgs, "${classpath_separator}", ";");
#else
            jvmArgs = replace(jvmArgs, "${classpath_separator}", ":");
#endif
        }

        std::string uuid = offlineUUID(name);

        auto replaceAll = [](std::string s, const std::string& f, const std::string& t) {
            size_t p = 0;
            while ((p = s.find(f, p)) != std::string::npos) { s.replace(p, f.size(), t); p += t.size(); }
            return s;
        };

        gameArgs = replaceAll(gameArgs, "${auth_player_name}",  name);
        gameArgs = replaceAll(gameArgs, "${version_name}",      version);
        gameArgs = replaceAll(gameArgs, "${game_directory}",    "\"" + m_workDir + "\"");
        gameArgs = replaceAll(gameArgs, "${assets_root}",       m_workDir + "/assets");
        gameArgs = replaceAll(gameArgs, "${auth_uuid}",         uuid);
        gameArgs = replaceAll(gameArgs, "${auth_access_token}", "0");
        gameArgs = replaceAll(gameArgs, "${clientid}",          "0");
        gameArgs = replaceAll(gameArgs, "${auth_xuid}",         "0");
        gameArgs = replaceAll(gameArgs, "${user_properties}",   "{}");
        gameArgs = replaceAll(gameArgs, "${user_type}",         "mojang");
        gameArgs = replaceAll(gameArgs, "${version_type}",      "release");
        gameArgs = replaceAll(gameArgs, "${assets_index_name}", assetsIndex);
        gameArgs = replaceAll(gameArgs, "${auth_session}",      "0");
        gameArgs = replaceAll(gameArgs, "${game_assets}",       m_workDir + "/resources");
        gameArgs = replaceAll(gameArgs, "${classpath}",         cp);

        std::string log4jArg;
        const JsonValue& logSrc = inheritsFrom.empty() ? json : baseJson;
        if (logSrc.has("logging")) {
            std::string logId = logSrc["logging"]["client"]["file"]["id"].asString();
            if (!logId.empty()) {
                std::string logPath = m_workDir + "/assets/log_configs/" + logId;
#ifdef _WIN32
                log4jArg = "-Dlog4j.configurationFile=file:///" + logPath;
#else
                log4jArg = "-Dlog4j.configurationFile=file://" + logPath;
#endif
            }
        }

        std::string extraJvm;
        if (m_cfg->value(CFG_USE_CUSTOM_ARGS, DEFAULT_USE_CUSTOM_ARGS).toBool()) {
            extraJvm = m_cfg->value(CFG_CUSTOM_ARGS, DEFAULT_JVM_ARGS_NEW).toString().toStdString();
        } else {
            if (releaseTime > 0 && releaseTime < 736780)
                extraJvm = DEFAULT_JVM_ARGS_OLD;
            else
                extraJvm = DEFAULT_JVM_ARGS_NEW;
        }

        if (assetsIndex == "pre-1.6" || assetsIndex == "legacy")
            assetsToResources(assetsIndex);

        std::string full =
            "-Xmx" + std::to_string(ram) + "M "
            + extraJvm + " "
            + "-Dlog4j2.formatMsgNoLookups=true "
            + (log4jArg.empty() ? "" : log4jArg + " ")
            + jvmArgs + " "
            + mainClass + " "
            + gameArgs;

        std::string cleaned;
        bool lastSpace = false;
        for (char c : full) {
            if (c == ' ') { if (!lastSpace) cleaned += c; lastSpace = true; }
            else { cleaned += c; lastSpace = false; }
        }

        return cleaned;
    }

    void assetsToResources(const std::string& assetsIndex) {
        std::string indexPath = m_workDir + "/assets/indexes/" + assetsIndex + ".json";
        std::string indexStr  = readFileText(indexPath);
        if (indexStr.empty()) return;
        try {
            auto json = parseJson(indexStr);
            const auto& objects = json["objects"];
            for (auto& [fileName, obj] : objects.obj) {
                std::string hash = obj["hash"].asString();
                long long sz = obj["size"].asInt();
                std::string src  = m_workDir + "/assets/objects/" + hash.substr(0,2) + "/" + hash;
                std::string dst  = m_workDir + "/resources/" + fileName;
                std::error_code ec;
                if (fileSize(dst) != sz) {
                    mkdirRecursive(fs::path(dst).parent_path().string());
                    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
                }
            }
        } catch(...) {}
    }


    static int getTotalRamMB() {
#ifdef _WIN32
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
            return static_cast<int>(ms.ullTotalPhys / (1024 * 1024));
#elif defined(__linux__)
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0)
            return static_cast<int>((long long)pages * pageSize / (1024 * 1024));
#elif defined(__APPLE__)
        int64_t mem = 0;
        size_t len = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
            return static_cast<int>(mem / (1024 * 1024));
#endif
        return 8192; // fallback 8gb
    }

    QSettings*   m_cfg;
    std::string  m_workDir;

    QLineEdit*   m_nameCtrl;
    QSpinBox*    m_ramCtrl;
    QComboBox*   m_versionCombo;
    QComboBox*   m_javaCombo;
    QPushButton* m_playBtn;
    QPushButton* m_downloadBtn;
    QPushButton* m_deleteBtn;
    QPushButton* m_settingsBtn;
    QLabel*      m_statusLabel;

    std::unique_ptr<DownloadManager> m_dlManager;
};
