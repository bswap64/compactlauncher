#pragma once

#ifdef _WIN32

#include "launcher.h"
#include "json_parser.h"
#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QMetaObject>
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <string>
#include <vector>
#include <functional>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wininet.h>
#include <filesystem>

static const char* GITHUB_API_LATEST = "https://api.github.com/repos/bswap64/compactlauncher/releases/latest";

static std::vector<int> parseVersionParts(const std::string& v) {
    std::vector<int> parts;
    std::istringstream ss(v);
    std::string token;
    while (std::getline(ss, token, '.')) {
        try { parts.push_back(std::stoi(token)); }
        catch (...) { parts.push_back(0); }
    }
    return parts;
}

static bool isVersionNewer(const std::string& remote, const std::string& local) {
    auto r = parseVersionParts(remote);
    auto l = parseVersionParts(local);
    size_t n = std::max(r.size(), l.size());
    r.resize(n, 0);
    l.resize(n, 0);
    for (size_t i = 0; i < n; i++) {
        if (r[i] > l[i]) return true;
        if (r[i] < l[i]) return false;
    }
    return false;
}

struct UpdateInfo {
    std::string remoteVersion;
    std::string zipUrl;
    std::string zipName;
};

static UpdateInfo fetchUpdateInfo() {
    UpdateInfo info;
    std::string json = httpGetString(GITHUB_API_LATEST);
    if (json.empty()) return info;

    try {
        auto root = parseJson(json);
        std::string tagName = root["tag_name"].asString();
        if (tagName.empty()) return info;
        if (!tagName.empty() && tagName[0] == 'v')
            tagName = tagName.substr(1);
        info.remoteVersion = tagName;

        const auto& assets = root["assets"];
        for (size_t i = 0; i < assets.arr.size(); i++) {
            std::string name = assets[i]["name"].asString();
            std::string url  = assets[i]["browser_download_url"].asString();
            if (name.empty() || url.empty()) continue;
            std::string nameLow = name;
            for (char& c : nameLow) c = (char)tolower((unsigned char)c);
            if (nameLow.size() >= 4 &&
                nameLow.substr(nameLow.size() - 4) == ".zip" &&
                nameLow.find("windows") != std::string::npos)
            {
                info.zipUrl  = url;
                info.zipName = name;
                break;
            }
        }
    } catch (...) {}
    return info;
}

class UpdateProgressDialog : public QDialog {
public:
    UpdateProgressDialog(QWidget* parent, const UpdateInfo& info)
        : QDialog(parent), m_info(info), m_cancelled(false)
    {
        setWindowTitle("Updating...");
        setFixedWidth(250);
        setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

        auto* layout = new QVBoxLayout(this);

        m_statusLabel = new QLabel("Downloading update...");
        layout->addWidget(m_statusLabel);

        m_progressBar = new QProgressBar();
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        layout->addWidget(m_progressBar);

        m_cancelBtn = new QPushButton("Cancel");
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch(1);
        btnRow->addWidget(m_cancelBtn);
        layout->addLayout(btnRow);

        connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
            m_cancelled = true;
            m_cancelBtn->setEnabled(false);
            m_statusLabel->setText("Cancelling...");
        });
    }

    void startDownload() {
        std::string workDir = QCoreApplication::applicationDirPath().toStdString();
        for (char& c : workDir) if (c == '\\') c = '/';
        std::string zipPath = workDir + "/" + m_info.zipName;

        std::thread([this, workDir, zipPath]() {
            bool ok = downloadZipWithProgress(m_info.zipUrl, zipPath);

            if (!ok || m_cancelled) {
                std::filesystem::remove(zipPath);
                QMetaObject::invokeMethod(this, [this]() {
                    if (!m_cancelled)
                        QMessageBox::critical(this, "Update Failed", "Failed to download the update.");
                    reject();
                }, Qt::QueuedConnection);
                return;
            }

            QMetaObject::invokeMethod(this, [this, workDir, zipPath]() {
                m_statusLabel->setText("Launching updater...");
                m_cancelBtn->setEnabled(false);
                launchUpdater(workDir, zipPath);
                accept();
                QCoreApplication::quit();
            }, Qt::QueuedConnection);
        }).detach();
    }

private:
    bool downloadZipWithProgress(const std::string& url, const std::string& dest) {
        HINTERNET hSess = InternetOpenA("Compact Launcher/2.0",
                                        INTERNET_OPEN_TYPE_DIRECT,
                                        nullptr, nullptr, 0);
        if (!hSess) return false;

        DWORD timeout = 30000;
        InternetSetOptionA(hSess, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hSess, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        InternetSetOptionA(hSess, INTERNET_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));

        DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE;
        HINTERNET hUrl = InternetOpenUrlA(hSess, url.c_str(), nullptr, 0, flags, 0);
        if (!hUrl) {
            InternetCloseHandle(hSess);
            return false;
        }

        DWORD contentLen = 0;
        DWORD bufSize = sizeof(contentLen);
        HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                       &contentLen, &bufSize, nullptr);

        std::ofstream out(dest, std::ios::binary);
        if (!out) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hSess);
            return false;
        }

        if (contentLen > 0) {
            QMetaObject::invokeMethod(this, [this]() {
                m_progressBar->setRange(0, 100);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this]() {
                m_progressBar->setRange(0, 0);
            }, Qt::QueuedConnection);
        }

        std::vector<char> buf(65536);
        DWORD read = 0;
        DWORD totalRead = 0;
        bool ok = true;

        while (!m_cancelled) {
            if (!InternetReadFile(hUrl, buf.data(), (DWORD)buf.size(), &read)) { ok = false; break; }
            if (read == 0) break;
            out.write(buf.data(), read);
            if (!out) { ok = false; break; }
            totalRead += read;
            if (contentLen > 0) {
                int pct = (int)((long long)totalRead * 100 / contentLen);
                QMetaObject::invokeMethod(this, [this, pct]() {
                    m_progressBar->setValue(pct);
                }, Qt::QueuedConnection);
            }
        }

        InternetCloseHandle(hUrl);
        InternetCloseHandle(hSess);
        out.close();

        if (!ok || m_cancelled) {
            std::filesystem::remove(dest);
            return false;
        }
        QMetaObject::invokeMethod(this, [this]() {
            m_progressBar->setRange(0, 100);
            m_progressBar->setValue(100);
        }, Qt::QueuedConnection);
        return true;
    }

    void launchUpdater(const std::string& workDir, const std::string& zipPath) {
        std::string updaterExe = workDir + "/updater.exe";
        if (!std::filesystem::exists(updaterExe)) {
            QMessageBox::critical(this, "Update Error",
                "updater.exe not found next to CompactLauncher.exe.\n"
                "Please re-download the launcher manually.");
            return;
        }

        DWORD pid = GetCurrentProcessId();

        std::string zipWin = zipPath;
        for (char& c : zipWin) if (c == '/') c = '\\';
        std::string workWin = workDir;
        for (char& c : workWin) if (c == '/') c = '\\';
        std::string updaterWin = updaterExe;
        for (char& c : updaterWin) if (c == '/') c = '\\';

        std::string cmdLine = "\"" + updaterWin + "\""
            + " --pid " + std::to_string(pid)
            + " --zip \"" + zipWin + "\""
            + " --dir \"" + workWin + "\"";

        std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(0);

        STARTUPINFOA si = {};
        PROCESS_INFORMATION pi = {};
        si.cb = sizeof(si);

        CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS,
                       nullptr, workWin.c_str(), &si, &pi);

        if (pi.hProcess) CloseHandle(pi.hProcess);
        if (pi.hThread)  CloseHandle(pi.hThread);
    }

    UpdateInfo m_info;
    std::atomic<bool> m_cancelled;
    QProgressBar* m_progressBar;
    QLabel*       m_statusLabel;
    QPushButton*  m_cancelBtn;
};

inline void checkForUpdates(QWidget* parent, QSettings* cfg,
                            std::initializer_list<QWidget*> lockWidgets = {}) {
    if (!cfg->value("Launcher/AutoUpdate", true).toBool()) return;

    std::vector<QWidget*> widgets(lockWidgets);

    std::thread([parent, widgets]() {
        UpdateInfo info = fetchUpdateInfo();
        if (info.remoteVersion.empty() || info.zipUrl.empty()) return;
        if (!isVersionNewer(info.remoteVersion, LAUNCHER_VERSION)) return;

        QMetaObject::invokeMethod(parent, [parent, info, widgets]() {
            auto reply = QMessageBox::question(
                parent,
                "Update Available",
                QString("A newer version of CLauncher is available.\n"
                        "Would you like to download and install it?\n\n"
                        "Current version: %1\n"
                        "Latest version:  %2")
                    .arg(LAUNCHER_VERSION)
                    .arg(QString::fromStdString(info.remoteVersion)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::Yes
            );

            if (reply != QMessageBox::Yes) return;

            for (auto* w : widgets) w->setEnabled(false);

            auto* dlg = new UpdateProgressDialog(parent, info);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(dlg, &QDialog::rejected, parent, [widgets]() {
                for (auto* w : widgets) w->setEnabled(true);
            });
            dlg->show();
            dlg->startDownload();
        }, Qt::QueuedConnection);
    }).detach();
}

#endif
