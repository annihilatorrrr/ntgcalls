//
// Created by Laky64 on 28/09/24.
//

#include <thread>
#include <ntgcalls/io/threaded_reader.hpp>

namespace ntgcalls {
    ThreadedReader::ThreadedReader(BaseSink *sink, const size_t bufferCount): BaseReader(sink) {
        bufferThreads.reserve(bufferCount);
    }

    ThreadedReader::~ThreadedReader() {
        const bool wasRunning = running;
        if (running) {
            running = false;
            cv.notify_all();
        }
        if (wasRunning) {
            for (auto& thread : bufferThreads) {
                thread.Finalize();
            }
        }
    }

    void ThreadedReader::open() {
        if (running) return;
        const size_t bufferCount = bufferThreads.capacity();
        running = true;
        auto frameSize = sink->frameSize();
        auto frameTime = sink->frameTime();
        for (size_t i = 0; i < bufferCount; ++i) {
            bufferThreads.push_back(
                rtc::PlatformThread::SpawnJoinable(
                    [this, i, frameSize, frameTime] {
                        activeBufferCount++;
                        while (running) {
                            std::unique_lock lock(mtx);
                            bytes::unique_binary data;
                            try {
                                data = std::move(read(frameSize));
                            } catch (...) {
                                running = false;
                                break;
                            }
                            cv.wait(lock, [this, i] {
                                return (!running || activeBuffer == i) && enabled;
                            });
                            if (!running) break;
                            if (auto waitTime = lastTime - std::chrono::high_resolution_clock::now() + frameTime; waitTime.count() > 0) {
                                std::this_thread::sleep_for(waitTime);
                            }
                            dataCallback(std::move(data), {});
                            lastTime = std::chrono::high_resolution_clock::now();
                            activeBuffer = (activeBuffer + 1) % bufferThreads.size();
                            lock.unlock();
                            cv.notify_all();
                        }
                        std::lock_guard lock(mtx);
                        activeBufferCount--;
                        if (activeBufferCount == 0) {
                            (void) eofCallback();
                        } else {
                            cv.notify_all();
                        }
                    },
                    "ThreadedReader_" + std::to_string(bufferCount),
                    rtc::ThreadAttributes().SetPriority(rtc::ThreadPriority::kRealtime)
                )
            );
        }
    }

    bool ThreadedReader::set_enabled(const bool status) {
        const auto res = BaseReader::set_enabled(status);
        cv.notify_all();
        return res;
    }
} // ntgcalls