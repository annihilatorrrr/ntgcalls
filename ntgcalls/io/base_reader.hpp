//
// Created by Laky64 on 04/08/2023.
//

#pragma once


#include <shared_mutex>
#include <vector>

#include <wrtc/wrtc.hpp>
#include "../utils/dispatch_queue.hpp"

namespace ntgcalls {
    class BaseReader {
        std::vector<wrtc::binary> nextBuffer;
        std::atomic_bool _eof = false, running = false, noLatency = false;
        std::shared_ptr<DispatchQueue> dispatchQueue;
        std::shared_mutex mutex;
        std::shared_ptr<std::promise<void>> promise;

    protected:
        int64_t readChunks = 0;

        explicit BaseReader(bool noLatency);

        virtual ~BaseReader();

        virtual wrtc::binary readInternal(int64_t size) = 0;

    public:
        wrtc::binary read(int64_t size);

        [[nodiscard]] bool eof();

        virtual void close();
    };
}
