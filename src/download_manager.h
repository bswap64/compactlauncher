#pragma once
#include "launcher.h"
#include <QObject>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>

class DownloadManager : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(QObject* parent = nullptr) : QObject(parent) {}

    int           threads = 8;
    bool          forceRedownload = false;
    int           maxRetries = 5;

    std::atomic<int>  totalFiles{0};
    std::atomic<int>  doneFiles{0};
    std::atomic<int>  failedFiles{0};
    std::atomic<bool> cancelled{false};

    void run(std::vector<DownloadEntry> entries) {
        totalFiles  = (int)entries.size();
        doneFiles   = 0;
        failedFiles = 0;
        cancelled   = false;

        if (entries.empty()) {
            emit downloadDone();
            return;
        }

        auto qMutex   = std::make_shared<std::mutex>();
        auto mkdirMtx = std::make_shared<std::mutex>();
        auto queue    = std::make_shared<std::queue<DownloadEntry>>();
        for (auto& e : entries) queue->push(e);

        int workers = std::min(threads, (int)entries.size());
        std::vector<std::thread> pool;
        pool.reserve(workers);

        for (int i = 0; i < workers; i++) {
            pool.emplace_back([this, qMutex, mkdirMtx, queue]() {
                while (true) {
                    
                    if (cancelled) break;

                    DownloadEntry entry;
                    {
                        std::lock_guard<std::mutex> lk(*qMutex);
                        if (queue->empty()) break;
                        
                        if (cancelled) break;
                        entry = queue->front();
                        queue->pop();
                    }

                    
                    if (cancelled) break;

                    if (!forceRedownload && entry.expectedSize > 0) {
                        long long sz = fileSize(entry.localPath);
                        if (sz == entry.expectedSize) {
                            ++doneFiles;
                            emit downloadProgress(doneFiles.load(), totalFiles.load());
                            continue;
                        }
                    } else if (!forceRedownload && fileSize(entry.localPath) > 0) {
                        ++doneFiles;
                        emit downloadProgress(doneFiles.load(), totalFiles.load());
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lk(*mkdirMtx);
                        mkdirRecursive(fs::path(entry.localPath).parent_path().string());
                    }

                    bool ok = false;
                    for (int attempt = 0; attempt < maxRetries && !cancelled; attempt++) {
                        
                        ok = httpDownloadToFile(entry.url, entry.localPath, cancelled);
                        if (ok || cancelled) break;
                        
                        int sleepMs = 500 * (attempt + 1);
                        for (int s = 0; s < sleepMs && !cancelled; s += 50)
                            std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }

                    if (cancelled) break;

                    if (ok) {
                        ++doneFiles;
                        emit downloadProgress(doneFiles.load(), totalFiles.load());
                    } else {
                        LOG("Failed after " + std::to_string(maxRetries) + " retries: " + entry.url);
                        ++failedFiles;
                        ++doneFiles;
                        emit downloadProgress(doneFiles.load(), totalFiles.load());
                    }
                }
            });
        }

        std::thread([this, pool = std::move(pool)]() mutable {
            for (auto& t : pool) if (t.joinable()) t.join();
            if (cancelled)
                emit downloadCancelled();
            else if (failedFiles > 0)
                emit downloadFailed(failedFiles.load());
            else
                emit downloadDone();
        }).detach();
    }

    void cancel() { cancelled = true; }

signals:
    void downloadProgress(int done, int total);
    void downloadDone();
    void downloadFailed(int failedCount);
    void downloadCancelled();
};
