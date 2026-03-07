#pragma once
#include "launcher.h"
#include "json_parser.h"
#include "version_parser.h"
#include "download_manager.h"
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QMetaObject>
#include <QMessageBox>
#include <functional>

class DownloaderDialog : public QDialog {
    Q_OBJECT
public:
    DownloaderDialog(QWidget* parent, QSettings* cfg, const std::string& workDir,
                     const std::string& manifestJson, std::function<void()> onVersionInstalled)
        : QDialog(parent)
        , m_cfg(cfg), m_workDir(workDir), m_manifest(manifestJson)
        , m_onVersionInstalled(onVersionInstalled)
    {
        setWindowTitle("Download Minecraft Version");
        resize(360, 320);

        auto* sizer = new QVBoxLayout(this);

        sizer->addWidget(new QLabel("Select version:"));
        m_versionCombo = new QComboBox();
        sizer->addWidget(m_versionCombo);

        m_showAll = new QCheckBox("Show all versions (snapshots, betas, alphas)");
        m_showAll->setChecked(cfg->value(CFG_SHOW_ALL_VER, DEFAULT_SHOW_ALL).toBool());
        sizer->addWidget(m_showAll);

        m_forceRedownload = new QCheckBox("Force re-download all files");
        m_forceRedownload->setChecked(cfg->value(CFG_REDOWNLOAD, DEFAULT_REDOWNLOAD).toBool());
        sizer->addWidget(m_forceRedownload);

        sizer->addStretch(1);

        m_progressLabel = new QLabel("");
        m_gauge = new QProgressBar();
        m_gauge->setRange(0, 100);
        m_fileLabel = new QLabel("");
        sizer->addWidget(m_progressLabel);
        sizer->addWidget(m_gauge);
        sizer->addWidget(m_fileLabel);

        m_progressLabel->hide();
        m_gauge->hide();
        m_fileLabel->hide();

        auto* btnRow = new QHBoxLayout();
        m_downloadBtn = new QPushButton("Download");
        m_cancelBtn   = new QPushButton("Cancel download");
        m_closeBtn    = new QPushButton("Close");
        m_cancelBtn->setEnabled(false);
        btnRow->addWidget(m_downloadBtn);
        btnRow->addWidget(m_cancelBtn);
        btnRow->addWidget(m_closeBtn);
        sizer->addLayout(btnRow);

        populateVersions();

        connect(m_showAll, &QCheckBox::toggled, this, [this](bool) {
            populateVersions();
            m_cfg->setValue(CFG_SHOW_ALL_VER, m_showAll->isChecked());
        });
        connect(m_forceRedownload, &QCheckBox::toggled, this, [this](bool) {
            updateDownloadButton();
        });
        connect(m_versionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            updateDownloadButton();
        });
        connect(m_downloadBtn, &QPushButton::clicked, this, &DownloaderDialog::onDownload);
        connect(m_cancelBtn, &QPushButton::clicked, this, &DownloaderDialog::onCancelDownload);
        connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
            if (m_dlManager && !m_dlManager->cancelled)
                m_dlManager->cancel();
            reject();
        });
    }

    ~DownloaderDialog() {
        if (m_dlManager) {
            m_dlManager->cancel();
            m_dlManager->deleteLater();
            m_dlManager = nullptr;
        }
    }

private:
    bool isVersionInstalled(const std::string& version) {
        std::string jsonPath = m_workDir + "/versions/" + version + "/" + version + ".json";
        std::string jarPath  = m_workDir + "/versions/" + version + "/" + version + ".jar";
        return fs::exists(jsonPath) && fs::exists(jarPath);
    }

    void updateDownloadButton() {
        std::string version = m_versionCombo->currentText().toStdString();
        bool installed = isVersionInstalled(version);
        bool force = m_forceRedownload->isChecked();
        if (installed && !force) {
            m_downloadBtn->setText("Already downloaded");
            m_downloadBtn->setEnabled(false);
        } else {
            m_downloadBtn->setText("Download");
            m_downloadBtn->setEnabled(true);
        }
    }

    void populateVersions() {
        m_versionCombo->clear();
        if (m_manifest.empty()) { m_versionCombo->addItem("(no internet)"); return; }
        try {
            auto json = parseJson(m_manifest);
            const auto& versions = json["versions"];
            bool all = m_showAll->isChecked();
            for (size_t i = 0; i < versions.arr.size(); i++) {
                std::string type = versions[i]["type"].asString();
                if (!all && type != "release") continue;
                std::string id = versions[i]["id"].asString();
                QString label = QString::fromStdString(id);
                if (isVersionInstalled(id))
                    label += " [installed]";
                m_versionCombo->addItem(label, QString::fromStdString(id));
            }
            if (m_versionCombo->count() > 0)
                m_versionCombo->setCurrentIndex(0);
        } catch (...) { m_versionCombo->addItem("(parse error)"); }
        updateDownloadButton();
    }

private slots:
    void onDownload() {
        std::string version = m_versionCombo->currentData().toString().toStdString();
        if (version.empty()) version = m_versionCombo->currentText().toStdString();
        if (version.empty() || version == "(no internet)") return;

        bool installed = isVersionInstalled(version);
        bool force = m_forceRedownload->isChecked();
        if (installed && !force) {
            QMessageBox::information(this, "Already downloaded",
                QString("Version %1 is already downloaded.\nEnable \"Force re-download\" to re-download it.")
                    .arg(QString::fromStdString(version)));
            return;
        }

        m_downloadBtn->setEnabled(false);
        m_closeBtn->setEnabled(false);
        m_cancelBtn->setEnabled(true);
        m_currentDownloadVersion = version;

        m_progressLabel->setText(QString("Downloading %1...").arg(QString::fromStdString(version)));
        m_progressLabel->show();
        m_gauge->setValue(0);
        m_gauge->show();
        m_fileLabel->setText("Preparing...");
        m_fileLabel->show();

        bool forceRedownload = force;
        long dlThreads = m_cfg->value(CFG_DL_THREADS, DEFAULT_DL_THREADS).toInt();

        std::thread([this, version, forceRedownload, dlThreads]() {
            try {
                buildDownloadListAndStart(version, forceRedownload, dlThreads);
            } catch (const std::exception& e) {
                std::string msg = e.what();
                QMetaObject::invokeMethod(this, [this, msg]() {
                    onFailed(1);
                    m_fileLabel->setText(QString("Error: %1").arg(QString::fromStdString(msg)));
                }, Qt::QueuedConnection);
            }
        }).detach();
    }

    void onCancelDownload() {
        if (m_dlManager) {
            m_dlManager->cancel();
        }
        m_progressLabel->setText("Cancelling...");
        m_fileLabel->setText("Please wait...");
        m_cancelBtn->setEnabled(false);
    }

    void onCancelled() {
        if (!m_currentDownloadVersion.empty()) {
            std::string versionDir = m_workDir + "/versions/" + m_currentDownloadVersion;
            std::error_code ec;
            if (fs::exists(versionDir, ec))
                fs::remove_all(versionDir, ec);
            m_currentDownloadVersion.clear();
        }
        m_progressLabel->setText("Cancelled.");
        m_fileLabel->setText("Download cancelled.");
        m_gauge->setValue(0);
        m_downloadBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
        m_closeBtn->setEnabled(true);
        populateVersions();
    }

    void onProgress(int done, int total) {
        if (total > 0) {
            m_gauge->setRange(0, total);
            m_gauge->setValue(done);
        }
        m_fileLabel->setText(QString("Files: %1 / %2").arg(done).arg(total));
    }

    void onDone() {
        LOG("Download complete!");
        m_gauge->setValue(m_gauge->maximum());
        m_fileLabel->setText("Download complete!");
        m_progressLabel->setText("Done!");
        m_downloadBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
        m_closeBtn->setEnabled(true);
        m_currentDownloadVersion.clear();
        m_cfg->setValue(CFG_REDOWNLOAD, m_forceRedownload->isChecked());
        populateVersions();
        if (m_onVersionInstalled) m_onVersionInstalled();
    }

    void onFailed(int failed) {
        LOG("Download failed: " + std::to_string(failed) + " files.");
        if (!m_currentDownloadVersion.empty()) {
            std::string versionDir = m_workDir + "/versions/" + m_currentDownloadVersion;
            std::error_code ec;
            if (fs::exists(versionDir, ec))
                fs::remove_all(versionDir, ec);
            m_currentDownloadVersion.clear();
        }
        m_fileLabel->setText(QString("Download failed! %1 files missing.").arg(failed));
        m_progressLabel->setText("Incomplete!");
        m_downloadBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
        m_closeBtn->setEnabled(true);
        populateVersions();
    }

private:
    void buildDownloadListAndStart(const std::string& version, bool forceRedownload, long dlThreads) {
        LOG(":: Starting download for version: " + version );

        std::string versionJsonUrl;
        {
            auto manifest = parseJson(m_manifest);
            const auto& versions = manifest["versions"];
            for (size_t i = 0; i < versions.arr.size(); i++) {
                if (versions[i]["id"].asString() == version) {
                    versionJsonUrl = versions[i]["url"].asString();
                    break;
                }
            }
        }
        if (versionJsonUrl.empty()) {
            LOG("Error: Version not found in manifest: " + version);
            throw std::runtime_error("Version not found in manifest");
        }

        std::string versionDir = m_workDir + "/versions/" + version;
        mkdirRecursive(versionDir);
        std::string versionJsonPath = versionDir + "/" + version + ".json";

        std::string versionJsonStr = httpGetString(versionJsonUrl);
        if (versionJsonStr.empty()) throw std::runtime_error("Failed to download version JSON");
        writeFileText(versionJsonPath, versionJsonStr);

        auto vJson = parseJson(versionJsonStr);

        std::string assetsIndex = vJson["assets"].asString("legacy");
        std::string assetIndexUrl = vJson["assetIndex"]["url"].asString();
        mkdirRecursive(m_workDir + "/assets/indexes");
        std::string assetIndexPath = m_workDir + "/assets/indexes/" + assetsIndex + ".json";
        std::string assetIndexStr = httpGetString(assetIndexUrl);
        if (!assetIndexStr.empty()) writeFileText(assetIndexPath, assetIndexStr);

        std::vector<DownloadEntry> entries;

        std::string clientUrl = vJson["downloads"]["client"]["url"].asString();
        long long clientSize  = vJson["downloads"]["client"]["size"].asInt();
        std::string clientJar = versionDir + "/" + version + ".jar";
        if (forceRedownload || fileSize(clientJar) != clientSize)
            entries.push_back({clientUrl, clientJar, clientSize});

        auto libs = parseLibraries(m_workDir, vJson, version, true);
        auto libEntries = buildLibraryDownloadList(m_workDir, libs, forceRedownload);
        for (auto& e : libEntries) entries.push_back(e);

        if (!assetIndexStr.empty()) {
            auto assetJson = parseJson(assetIndexStr);
            const auto& objects = assetJson["objects"];
            for (auto& [name, obj] : objects.obj) {
                std::string hash = obj["hash"].asString();
                long long sz = obj["size"].asInt();
                std::string prefix = hash.substr(0, 2);
                std::string dest = m_workDir + "/assets/objects/" + prefix + "/" + hash;
                if (!forceRedownload && fileSize(dest) == sz) continue;
                mkdirRecursive(m_workDir + "/assets/objects/" + prefix);
                entries.push_back({
                    "https://resources.download.minecraft.net/" + prefix + "/" + hash,
                    dest, sz
                });
            }
        }

        if (vJson.has("logging")) {
            std::string logId  = vJson["logging"]["client"]["file"]["id"].asString();
            std::string logUrl = vJson["logging"]["client"]["file"]["url"].asString();
            long long   logSz  = vJson["logging"]["client"]["file"]["size"].asInt();
            if (!logId.empty()) {
                mkdirRecursive(m_workDir + "/assets/log_configs");
                std::string logDest = m_workDir + "/assets/log_configs/" + logId;
                if (forceRedownload || fileSize(logDest) != logSz)
                    entries.push_back({logUrl, logDest, logSz});
            }
        }

        LOG("Total files to download: " + std::to_string(entries.size()));

        QMetaObject::invokeMethod(this, [this, total = (int)entries.size()]() {
            onProgress(0, total);
        }, Qt::QueuedConnection);

        m_dlManager = new DownloadManager();
        m_dlManager->threads = (int)dlThreads;
        m_dlManager->forceRedownload = forceRedownload;

        connect(m_dlManager, &DownloadManager::downloadProgress, this, &DownloaderDialog::onProgress, Qt::QueuedConnection);
        connect(m_dlManager, &DownloadManager::downloadDone, this, &DownloaderDialog::onDone, Qt::QueuedConnection);
        connect(m_dlManager, &DownloadManager::downloadFailed, this, &DownloaderDialog::onFailed, Qt::QueuedConnection);
        connect(m_dlManager, &DownloadManager::downloadCancelled, this, &DownloaderDialog::onCancelled, Qt::QueuedConnection);

        m_dlManager->run(std::move(entries));
    }

    QSettings*    m_cfg;
    std::string   m_workDir;
    std::string   m_manifest;
    std::function<void()> m_onVersionInstalled;

    QComboBox*    m_versionCombo;
    QCheckBox*    m_showAll;
    QCheckBox*    m_forceRedownload;
    QProgressBar* m_gauge;
    QLabel*       m_progressLabel;
    QLabel*       m_fileLabel;
    QPushButton*  m_downloadBtn;
    QPushButton*  m_cancelBtn;
    QPushButton*  m_closeBtn;

    DownloadManager* m_dlManager = nullptr;
    std::string   m_currentDownloadVersion;
};
