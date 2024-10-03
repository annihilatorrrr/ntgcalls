//
// Created by Laky64 on 15/03/2024.
//

#include <ntgcalls/instances/group_call.hpp>

#include <future>

#include <ntgcalls/exceptions.hpp>
#include <wrtc/interfaces/group_connection.hpp>
#include <wrtc/models/response_payload.hpp>

namespace ntgcalls {
    GroupCall::~GroupCall() {
        stopPresentation();
    }

    std::string GroupCall::init(const MediaDescription& config) {
        RTC_LOG(LS_INFO) << "Initializing group call";
        std::lock_guard lock(mutex);
        if (connection) {
            RTC_LOG(LS_ERROR) << "Connection already made";
            throw ConnectionError("Connection already made");
        }
        connection = std::make_unique<wrtc::GroupConnection>(false);
        RTC_LOG(LS_INFO) << "Group call initialized";
        streamManager->setStreamSources(StreamManager::Mode::Playback, config);

        streamManager->addTrack(StreamManager::Mode::Playback, StreamManager::Device::Microphone, connection);
        streamManager->addTrack(StreamManager::Mode::Playback, StreamManager::Device::Camera, connection);
        RTC_LOG(LS_INFO) << "AVStream settings applied";
        return Safe<wrtc::GroupConnection>(connection)->getJoinPayload();
    }

    std::string GroupCall::initPresentation() {
        RTC_LOG(LS_INFO) << "Initializing screen sharing";
        std::lock_guard lock(mutex);
        if (presentationConnection) {
            RTC_LOG(LS_ERROR) << "Screen sharing already initialized";
            throw ConnectionError("Screen sharing already initialized");
        }
        presentationConnection = std::make_unique<wrtc::GroupConnection>(true);
        streamManager->addTrack(StreamManager::Mode::Playback, StreamManager::Device::Speaker, presentationConnection);
        streamManager->addTrack(StreamManager::Mode::Playback, StreamManager::Device::Screen, presentationConnection);
        RTC_LOG(LS_INFO) << "Screen sharing initialized";
        return Safe<wrtc::GroupConnection>(presentationConnection)->getJoinPayload();
    }

    void GroupCall::connect(const std::string& jsonData, bool isPresentation) {
        RTC_LOG(LS_INFO) << "Connecting to group call";
        std::lock_guard lock(mutex);
        const auto &conn = isPresentation ? presentationConnection : connection;
        if (!conn) {
            RTC_LOG(LS_ERROR) << "Connection not initialized";
            throw ConnectionError("Connection not initialized");
        }
        wrtc::ResponsePayload payload(jsonData);
        Safe<wrtc::GroupConnection>(conn)->setConnectionMode(payload.isRtmp ? wrtc::GroupConnection::Mode::Rtmp : wrtc::GroupConnection::Mode::Rtc);
        if (!payload.isRtmp) {
            Safe<wrtc::GroupConnection>(conn)->setRemoteParams(payload.remoteIceParameters, std::move(payload.fingerprint));
            for (const auto& rawCandidate : payload.candidates) {
                webrtc::JsepIceCandidate iceCandidate{std::string(), 0, rawCandidate};
                conn->addIceCandidate(wrtc::IceCandidate(&iceCandidate));
            }
            Safe<wrtc::GroupConnection>(conn)->createChannels(payload.media);
            RTC_LOG(LS_INFO) << "Remote parameters set";
        } else {
            RTC_LOG(LS_ERROR) << "RTMP connection not supported";
            throw RTMPNeeded("RTMP connection not supported");
        }
        setConnectionObserver(isPresentation ? CallNetworkState::Kind::Presentation : CallNetworkState::Kind::Normal);
    }

    void GroupCall::stopPresentation(const bool force) {
        if (!force && !presentationConnection) {
            return;
        }
        if (presentationConnection) {
            presentationConnection->close();
            presentationConnection = nullptr;
        } else {
            throw ConnectionError("Presentation not initialized");
        }
    }

    void GroupCall::onUpgrade(const std::function<void(MediaState)>& callback) {
        std::lock_guard lock(mutex);
        streamManager->onUpgrade(callback);
    }

    CallInterface::Type GroupCall::type() const {
        return Type::Group;
    }
} // ntgcalls