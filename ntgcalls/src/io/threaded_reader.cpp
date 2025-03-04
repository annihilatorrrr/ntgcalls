//
// Created by Laky64 on 28/09/24.
//

#include <thread>
#include <ntgcalls/io/threaded_reader.hpp>
#include <rtc_base/logging.h>

namespace ntgcalls {
    ThreadedReader::ThreadedReader(BaseSink *sink, const size_t bufferCount): BaseReader(sink) {
        bufferThreads.reserve(bufferCount);
    }

    void ThreadedReader::close() {
        RTC_LOG(LS_VERBOSE) << "ThreadedReader closing";
        exiting = true;
        const bool wasRunning = running;
        if (running) {
            running = false;
            cv.notify_all();
        }
        RTC_LOG(LS_VERBOSE) << "ThreadedReader closed notify";
        if (wasRunning) {
            for (auto& thread : bufferThreads) {
                RTC_LOG(LS_VERBOSE) << "ThreadedReader closing thread, line:" << currentLine;
                thread.Finalize();
            }
        }
        RTC_LOG(LS_VERBOSE) << "ThreadedReader closed";
    }

    void ThreadedReader::run(const std::function<bytes::unique_binary(int64_t)>& readCallback) {
        if (running) return;
        const size_t bufferCount = bufferThreads.capacity();
        running = true;
        auto frameTime = sink->frameTime();
        for (size_t i = 0; i < bufferCount; ++i) {
            bufferThreads.push_back(
                rtc::PlatformThread::SpawnJoinable(
                    [this, i, bufferCount, frameSize = sink->frameSize(), frameTime, readCallback] {
                        activeBufferCount++;
                        while (running) {
                            currentLine = 1;
                            std::unique_lock lock(mtx);
                            bytes::unique_binary data;
                            currentLine = 2;
                            try {
                                data = std::move(readCallback(frameSize));
                            } catch (...) {
                                currentLine = 4;
                                running = false;
                                break;
                            }
                            currentLine = 3;
                            cv.wait(lock, [this, i] {
                                return !running || (activeBuffer == i && enabled);
                            });
                            currentLine = 4;
                            if (!running) break;
                            currentLine = 5;
                            if (auto waitTime = lastTime - std::chrono::high_resolution_clock::now() + frameTime; waitTime.count() > 0) {
                                std::this_thread::sleep_for(waitTime);
                            }
                            currentLine = 6;
                            dataCallback(std::move(data), {});
                            currentLine = 7;
                            lastTime = std::chrono::high_resolution_clock::now();
                            activeBuffer = (activeBuffer + 1) % bufferCount;
                            currentLine = 8;
                            lock.unlock();
                            currentLine = 9;
                            cv.notify_all();
                        }
                        std::lock_guard lock(mtx);
                        activeBufferCount--;
                        if (activeBufferCount == 0) {
                            if (!exiting) (void) eofCallback();
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