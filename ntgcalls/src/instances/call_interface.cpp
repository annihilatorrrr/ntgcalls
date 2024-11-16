//
// Created by Laky64 on 15/03/2024.
//

#include <ntgcalls/instances/call_interface.hpp>

namespace ntgcalls {
    CallInterface::CallInterface(rtc::Thread* updateThread): updateThread(updateThread) {
        networkThread = rtc::Thread::Create();
        networkThread->Start();
        streamManager = std::make_shared<StreamManager>(updateThread);
    }

    CallInterface::~CallInterface() {
        RTC_LOG(LS_VERBOSE) << "Destroying CallInterface";
        isExiting = true;
        networkThread->BlockingCall([this] {
            std::lock_guard lock(mutex);
            connectionChangeCallback = nullptr;
            streamManager = nullptr;
            if (connection) {
                RTC_LOG(LS_VERBOSE) << "Removing connection listener";
                connection->onConnectionChange(nullptr);
                RTC_LOG(LS_VERBOSE) << "Closing connection";
                connection->close();
                RTC_LOG(LS_VERBOSE) << "Connection closed";
                connection = nullptr;
                RTC_LOG(LS_VERBOSE) << "Connection destroyed";
            }
            updateThread = nullptr;
            cancelNetworkListener();
            RTC_LOG(LS_VERBOSE) << "CallInterface destroyed";
        });
    }

    bool CallInterface::pause() const {
        return streamManager->pause();
    }

    bool CallInterface::resume() const {
        return streamManager->resume();
    }

    bool CallInterface::mute() const {
        return streamManager->mute();
    }

    bool CallInterface::unmute() const {
        return streamManager->unmute();
    }

    void CallInterface::setStreamSources(const StreamManager::Mode mode, const MediaDescription& config) const {
        streamManager->setStreamSources(mode, config);
        if (mode == StreamManager::Mode::Playback && connection) {
            streamManager->optimizeSources(connection.get());
        }
    }

    void CallInterface::onStreamEnd(const std::function<void(StreamManager::Type, StreamManager::Device)>& callback) {
        std::lock_guard lock(mutex);
        streamManager->onStreamEnd(callback);
    }

    void CallInterface::onConnectionChange(const std::function<void(CallNetworkState)>& callback) {
        std::lock_guard lock(mutex);
        connectionChangeCallback = callback;
    }

    void CallInterface::onFrame(const std::function<void(int64_t, StreamManager::Mode, StreamManager::Device, const bytes::binary&, wrtc::FrameData frameData)>& callback) {
        std::lock_guard lock(mutex);
        streamManager->onFrame(callback);
    }

    void CallInterface::onRemoteSourceChange(const std::function<void(RemoteSource)>& callback) {
        std::lock_guard lock(mutex);
        remoteSourceCallback = callback;
    }

    uint64_t CallInterface::time(const StreamManager::Mode mode) const {
        return streamManager->time(mode);
    }

    MediaState CallInterface::getState() const {
        return streamManager->getState();
    }

    StreamManager::Status CallInterface::status(const StreamManager::Mode mode) const {
        return streamManager->status(mode);
    }

    void CallInterface::sendExternalFrame(const StreamManager::Device device, const bytes::binary& data, const wrtc::FrameData frameData) const {
        streamManager->sendExternalFrame(device, data, frameData);
    }

    void CallInterface::cancelNetworkListener() {
        if (networkThread) {
            networkThread->Stop();
            networkThread = nullptr;
        }
    }

    void CallInterface::setConnectionObserver(CallNetworkState::Kind kind) {
        RTC_LOG(LS_INFO) << "Connecting...";
        (void) connectionChangeCallback({CallNetworkState::ConnectionState::Connecting, kind});
        connection->onConnectionChange([this, kind](const wrtc::ConnectionState state) {
            networkThread->PostTask([this, kind, state] {
                if (isExiting) return;
                std::lock_guard lock(mutex);
                switch (state) {
                case wrtc::ConnectionState::Connecting:
                    if (connected) {
                        RTC_LOG(LS_INFO) << "Reconnecting...";
                    }
                    break;
                case wrtc::ConnectionState::Connected:
                    RTC_LOG(LS_INFO) << "Connection established";
                    if (!connected && streamManager) {
                        connected = true;
                        streamManager->start();
                        RTC_LOG(LS_INFO) << "Stream started";
                        (void) connectionChangeCallback({CallNetworkState::ConnectionState::Connected, kind});
                    }
                    break;
                case wrtc::ConnectionState::Disconnected:
                case wrtc::ConnectionState::Failed:
                case wrtc::ConnectionState::Closed:
                    updateThread->PostTask([this] {
                        if (connection) {
                            connection->onConnectionChange(nullptr);
                        }
                    });
                    if (state == wrtc::ConnectionState::Failed) {
                        RTC_LOG(LS_ERROR) << "Connection failed";
                        (void) connectionChangeCallback({CallNetworkState::ConnectionState::Failed, kind});
                    } else {
                        RTC_LOG(LS_INFO) << "Connection closed";
                        (void) connectionChangeCallback({CallNetworkState::ConnectionState::Closed, kind});
                    }
                    break;
                default:
                    break;
                }
                cancelNetworkListener();
            });
        });
        networkThread->PostDelayedTask([this, kind] {
            if (!connected) {
                RTC_LOG(LS_ERROR) << "Connection timeout";
                (void) connectionChangeCallback({CallNetworkState::ConnectionState::Timeout, kind});
            }
        }, webrtc::TimeDelta::Seconds(20));
    }

    RemoteSource::State CallInterface::parseVideoState(const signaling::MediaStateMessage::VideoState state) {
        switch (state){
        case signaling::MediaStateMessage::VideoState::Active:
            return RemoteSource::State::Active;
        case signaling::MediaStateMessage::VideoState::Inactive:
            return RemoteSource::State::Inactive;
        case signaling::MediaStateMessage::VideoState::Suspended:
            return RemoteSource::State::Suspended;
        }
        return RemoteSource::State::Inactive;
    }
} // ntgcalls