module;

#include <filesystem>
#include <thread>
#include <windows.h>
#include <print>
#include <chrono>
#include <format>

#include <EASTL/atomic.h>
#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>

export module AngelEngine.ScriptWatcher;

import AngelEngine.Interfaces;
import AngelEngine.Logger;

namespace AngelEngine
{
    export class ScriptWatcher : public IScriptWatcher
    {
    public:
        ScriptWatcher(const std::filesystem::path& stdPath, const std::filesystem::path& modPath)
        {
            paths_.push_back(stdPath);
            paths_.push_back(modPath);
            
            stopRequested_.store(false, eastl::memory_order_relaxed);
            reloadPending_.store(false, eastl::memory_order_relaxed);
            
            watcherThread_ = std::thread(&ScriptWatcher::WatchLoop, this);
        }

        ~ScriptWatcher()
        {
            stopRequested_.store(true, eastl::memory_order_release);
            if (watcherThread_.joinable())
            {
                watcherThread_.join();
            }
        }

        bool CheckAndResetReloadFlag() override
        {
            return reloadPending_.exchange(false, eastl::memory_order_acquire);
        }

    private:
        void WatchLoop()
        {
            // We need a handle for each directory
            eastl::vector<HANDLE> dirHandles;
            eastl::vector<OVERLAPPED> overlaps;
            eastl::vector<eastl::vector<uint8_t>> buffers;
            
            dirHandles.reserve(paths_.size());
            overlaps.resize(paths_.size());
            buffers.resize(paths_.size());

            for (size_t i = 0; i < paths_.size(); ++i)
            {
                HANDLE hDir = CreateFileW(
                    paths_[i].c_str(),
                    FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                    NULL
                );

                if (hDir == INVALID_HANDLE_VALUE)
                {
                    Log::Error("[ScriptWatcher] Failed to watch directory: {}", paths_[i].string().c_str());
                    continue;
                }

                dirHandles.push_back(hDir);
                buffers[i].resize(1024); // 1KB buffer
                ZeroMemory(&overlaps[i], sizeof(OVERLAPPED));
                
                // Create an event for the overlapped structure
                overlaps[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                
                // Issue initial read
                ReadDirectoryChangesW(
                    hDir,
                    buffers[i].data(),
                    static_cast<DWORD>(buffers[i].size()),
                    TRUE, // Watch subtree
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION,
                    NULL,
                    &overlaps[i],
                    NULL
                );
            }

            if (dirHandles.empty()) return;

            while (!stopRequested_.load(eastl::memory_order_acquire))
            {
                // Wait for any of the directory events or a small timeout to check stop flag
                // We construct an array of handles to wait on
                eastl::vector<HANDLE> waitHandles;
                for(const auto& ov : overlaps) waitHandles.push_back(ov.hEvent);
                
                DWORD waitResult = WaitForMultipleObjects(
                    static_cast<DWORD>(waitHandles.size()),
                    waitHandles.data(),
                    FALSE,
                    200 // 200ms timeout to check stop flag
                );

                if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + waitHandles.size())
                {
                    // Change detected!
                    size_t index = waitResult - WAIT_OBJECT_0;
                    
                    // Debounce: Wait a bit to let file writes finish
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    
                    // Set flag
                    reloadPending_.store(true, eastl::memory_order_release);
                    
                    // Reset the event and reissue the read
                    ResetEvent(overlaps[index].hEvent);
                    
                    // We don't strictly need to process the buffer content, just knowing something changed is enough
                    // But we must reissue the read to catch future changes
                    ReadDirectoryChangesW(
                        dirHandles[index],
                        buffers[index].data(),
                        static_cast<DWORD>(buffers[index].size()),
                        TRUE,
                        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_CREATION,
                        NULL,
                        &overlaps[index],
                        NULL
                    );
                }
            }

            // Cleanup
            for (HANDLE h : dirHandles) CloseHandle(h);
            for (auto& ov : overlaps) CloseHandle(ov.hEvent);
        }

        eastl::vector<std::filesystem::path> paths_;
        std::thread watcherThread_;
        eastl::atomic<bool> stopRequested_;
        eastl::atomic<bool> reloadPending_;
    };
}