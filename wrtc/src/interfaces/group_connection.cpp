//
// Created by Laky64 on 01/10/24.
//

#include <p2p/base/dtls_transport.h>
#include <p2p/client/basic_port_allocator.h>
#include <wrtc/exceptions.hpp>
#include <wrtc/interfaces/group_connection.hpp>
#include <wrtc/models/simulcast_layer.hpp>
#include <modules/rtp_rtcp/source/rtp_header_extensions.h>

namespace wrtc {
    GroupConnection::GroupConnection(const bool isPresentation): isPresentation(isPresentation) {
        initConnection(true);
        generateSsrcs();
        beginAudioChannelCleanupTimer();
    }

    GroupConnection::~GroupConnection() {
        close();
    }

    void GroupConnection::generateSsrcs() {
        auto generator = std::mt19937(std::random_device()());
        auto distribution = std::uniform_int_distribution<uint32_t>();
        do {
            outgoingAudioSsrc = distribution(generator) & 0x7fffffffU;
        } while (!outgoingAudioSsrc);
        outgoingVideoSsrc = outgoingAudioSsrc + 1;
        const int numVideoSimulcastLayers = isPresentation ? 2:3;
        std::vector<SimulcastLayer> outgoingVideoSsrcs;
        outgoingVideoSsrcs.reserve(numVideoSimulcastLayers);
        for (int layerIndex = 0; layerIndex < numVideoSimulcastLayers; layerIndex++) {
            outgoingVideoSsrcs.emplace_back(outgoingVideoSsrc + layerIndex * 2 + 0, outgoingVideoSsrc + layerIndex * 2 + 1);
        }
        std::vector<uint32_t> simulcastGroupSsrcs;
        std::vector<cricket::SsrcGroup> fidGroups;
        for (const auto &layer : outgoingVideoSsrcs) {
            simulcastGroupSsrcs.push_back(layer.ssrc);
            cricket::SsrcGroup fidGroup(cricket::kFidSsrcGroupSemantics, { layer.ssrc, layer.fidSsrc });
            fidGroups.push_back(fidGroup);
        }

        if (simulcastGroupSsrcs.size() > 1) {
            SsrcGroup simulcastGroup;
            simulcastGroup.semantics = "SIM";
            simulcastGroup.ssrcs = simulcastGroupSsrcs;
            outgoingVideoSsrcGroups.push_back(simulcastGroup);
        }

        for (const auto& fidGroup : fidGroups) {
            SsrcGroup payloadFidGroup;
            payloadFidGroup.semantics = "FID";
            payloadFidGroup.ssrcs = fidGroup.ssrcs;
            outgoingVideoSsrcGroups.push_back(payloadFidGroup);
        }
    }

    void GroupConnection::stateUpdated(const bool isConnected) {
        if (isRtcConnected == isConnected) {
            return;
        }
        isRtcConnected = isConnected;
        updateIsConnected();
    }

    int GroupConnection::candidatePoolSize() const {
        return 2;
    }

    void GroupConnection::setPortAllocatorFlags(cricket::BasicPortAllocator* portAllocator) {
        portAllocator->set_flags(portAllocator->flags());
    }

    void GroupConnection::start() {
        transportChannel->MaybeStartGathering();
        restartDataChannel();
    }

    void GroupConnection::restartDataChannel() {
        dataChannelInterface = std::make_unique<SctpDataChannelProviderInterfaceImpl>(
            environment(),
            dtlsTransport.get(),
            true,
            networkThread(),
            signalingThread()
        );

        dataChannelInterface->onMessageReceived([this](const bytes::binary &data) {
           (void) dataChannelMessageCallback(data);
        });

        dataChannelInterface->onStateChanged([this](const bool isOpen) {
            if (!dataChannelOpen && isOpen) {
                dataChannelOpen = true;
                (void) dataChannelOpenedCallback();
            } else {
                dataChannelOpen = false;
            }
        });

        dataChannelInterface->onClosed([this] {
            dataChannelOpen = false;
            RTC_LOG(LS_INFO) << "Data channel closed, restarting";
            restartDataChannel();
        });

        dataChannelInterface->updateIsConnected(connected);
    }

    std::string GroupConnection::getJoinPayload() {
        json jsonRes;
        networkThread()->BlockingCall([this, &jsonRes] {
            const auto fingerprint = localFingerprint();
            jsonRes = {
                {"ufrag", localParameters.ufrag},
                {"pwd", localParameters.pwd},
                {"fingerprints",
                    {
                        {
                            {"hash", fingerprint->algorithm},
                            {"setup", "passive"},
                            {"fingerprint", fingerprint->GetRfc4572Fingerprint()}
                        }
                    }
                },
                {"ssrc", *reinterpret_cast<const int32_t *>(&outgoingAudioSsrc)},
                {"ssrc-groups", json::array()}
            };
            for (const auto& [semantics, sources] : outgoingVideoSsrcGroups) {
                std::vector<int32_t> signedSources;
                signedSources.reserve(sources.size());
                for (const auto source : sources) {
                    signedSources.push_back(*reinterpret_cast<const int32_t *>(&source));
                }
                jsonRes["ssrc-groups"].push_back({
                    {"sources", signedSources},
                    {"semantics", semantics}
                });
            }
        });
        return jsonRes.dump();
    }

    void GroupConnection::addIceCandidate(const IceCandidate& rawCandidate) const {
        const auto candidate = parseIceCandidate(rawCandidate)->candidate();
        networkThread()->PostTask([this, candidate] {
            transportChannel->AddRemoteCandidate(candidate);
        });
    }

    void GroupConnection::setRemoteParams(PeerIceParameters remoteIceParameters, std::unique_ptr<rtc::SSLFingerprint> fingerprint) {
        networkThread()->PostTask([this, remoteIceParameters = std::move(remoteIceParameters), fingerprint = std::move(fingerprint)] {
            remoteParameters = remoteIceParameters;
            const cricket::IceParameters parameters(
                remoteIceParameters.ufrag,
                remoteIceParameters.pwd,
                false
            );
            transportChannel->SetRemoteIceParameters(parameters);
            if (fingerprint) {
                dtlsTransport->SetRemoteFingerprint(fingerprint->algorithm, fingerprint->digest.data(), fingerprint->digest.size());
            }
        });
    }

    void GroupConnection::setConnectionMode(const Mode mode) {
        connectionMode = mode;
        switch (mode) {
        case Mode::Rtc:
            networkThread()->PostTask([this] {
                start();
            });
            break;
        default:
            throw RTCException("Unsupported connection mode");
        }
        updateIsConnected();
    }

    void GroupConnection::updateIsConnected() {
        bool isEffectivelyConnected = false;
        switch (connectionMode) {
            case Mode::Rtc:
                isEffectivelyConnected = isRtcConnected;
                break;
            case Mode::Rtmp:
                isEffectivelyConnected = isRtmpConnected;
                break;
            case Mode::None:
                break;
        }
        if (isEffectivelyConnected != lastEffectivelyConnected) {
            lastEffectivelyConnected = isEffectivelyConnected;
            ConnectionState newValue;
            if (isEffectivelyConnected) {
                newValue = ConnectionState::Connected;
            } else {
                newValue = ConnectionState::Connecting;
            }
            signalingThread()->PostTask([this, newValue] {
                (void) connectionChangeCallback(newValue);
            });
        }
    }

    void GroupConnection::RtpPacketReceived(const webrtc::RtpPacketReceived& packet) {
        const std::string endpoint = std::to_string(packet.Ssrc());
        if (packet.HasExtension(webrtc::kRtpExtensionAudioLevel)) {
            webrtc::AudioLevel audioLevel;
            if (packet.GetExtension<webrtc::AudioLevelExtension>(&audioLevel)) {
                if (incomingAudioChannels.contains(endpoint)) incomingAudioChannels[endpoint]->updateActivity();
            }
        }
        if (packet.PayloadType() == 111) {
            if (!incomingAudioChannels.contains(endpoint)) {
                addIncomingAudio(packet.Ssrc(), endpoint);
            } else {
                incomingAudioChannels[endpoint]->updateActivity();
            }
        }
    }

    void GroupConnection::createChannels(const ResponsePayload::Media& media) {
        mediaConfig = media;
        if (audioChannel && audioChannel->ssrc() != outgoingAudioSsrc) {
            audioChannel = nullptr;
        }
        MediaContent audioContent;
        audioContent.ssrc = outgoingAudioSsrc;
        audioContent.rtpExtensions = media.audioRtpExtensions;
        audioContent.payloadTypes = media.audioPayloadTypes;

        if (!audioChannel) {
            audioChannel = std::make_unique<OutgoingAudioChannel>(
                call.get(),
                channelManager.get(),
                dtlsSrtpTransport.get(),
                audioContent,
                workerThread(),
                networkThread(),
                &audioSink
            );
        }

        if (videoChannel && videoChannel->ssrc() != outgoingVideoSsrc) {
            videoChannel = nullptr;
        }

        MediaContent videoContent;
        videoContent.ssrc = outgoingVideoSsrc;
        videoContent.ssrcGroups = outgoingVideoSsrcGroups;
        videoContent.rtpExtensions = media.videoRtpExtensions;
        videoContent.payloadTypes = media.videoPayloadTypes;

        if (!videoChannel) {
            videoChannel = std::make_unique<OutgoingVideoChannel>(
                call.get(),
                channelManager.get(),
                dtlsSrtpTransport.get(),
                videoContent,
                workerThread(),
                networkThread(),
                &videoSink
            );
        }
    }

    uint32_t GroupConnection::addIncomingVideo(const std::string& endpoint, const std::vector<SsrcGroup>& ssrcGroups) {
        if (pendingContent.contains(endpoint)) {
            return 0;
        }
        MediaContent mediaContent;
        mediaContent.type = MediaContent::Type::Video;
        mediaContent.ssrcGroups = ssrcGroups;
        addIncomingSmartSource(endpoint, mediaContent);
        return mediaContent.mainSsrc();
    }

    bool GroupConnection::removeIncomingVideo(const std::string& endpoint) {
        if (!pendingContent.contains(endpoint)) {
            return false;
        }
        if (incomingVideoChannels.contains(endpoint)) incomingVideoChannels.erase(endpoint);
        pendingContent.erase(endpoint);
        return true;
    }

    void GroupConnection::addIncomingAudio(const uint32_t ssrc, const std::string& endpoint) {
        MediaContent audioContent;
        audioContent.type = MediaContent::Type::Audio;
        audioContent.ssrc = ssrc;
        audioContent.rtpExtensions = mediaConfig.audioRtpExtensions;
        audioContent.payloadTypes = mediaConfig.audioPayloadTypes;
        addIncomingSmartSource(endpoint, audioContent);
    }


    void GroupConnection::beginAudioChannelCleanupTimer() {
        workerThread()->PostDelayedTask([this] {
            std::lock_guard lock(mutex);
            if (isExiting) return;
            const auto timestamp = rtc::TimeMillis();
            std::vector<std::string> removeChannels;
            for (const auto& [channelId, channel] : incomingAudioChannels) {
                if (channel->getActivity() < timestamp - 1000) {
                    removeChannels.push_back(channelId);
                }
            }
            for (const auto &channelId : removeChannels) {
                removeIncomingAudio(channelId);
            }
            beginAudioChannelCleanupTimer();
        }, webrtc::TimeDelta::Millis(500));
    }

    void GroupConnection::close() {
        isExiting = true;
        std::lock_guard lock(mutex);
        outgoingVideoSsrcGroups.clear();
        NativeNetworkInterface::close();
    }

    bool GroupConnection::supportsRenomination() const {
        return false;
    }

    bool GroupConnection::getCustomParameterBool(const std::string& name) const {
        return false;
    }

    cricket::IceRole GroupConnection::iceRole() const {
        return cricket::ICEROLE_CONTROLLED;
    }

    cricket::IceMode GroupConnection::iceMode() const {
        return cricket::ICEMODE_LITE;
    }

    std::optional<rtc::SSLRole> GroupConnection::dtlsRole() const {
        return rtc::SSLRole::SSL_SERVER;
    }

    std::pair<cricket::ServerAddresses, std::vector<cricket::RelayServerConfig>> GroupConnection::getStunAndTurnServers() {
        return {{}, {}};
    }

    cricket::RelayPortFactoryInterface* GroupConnection::getRelayPortFactory() {
        return nullptr;
    }

    void GroupConnection::registerTransportCallbacks(cricket::P2PTransportChannel* transportChannel) {
        transportChannel->RegisterReceivedPacketCallback(this, [this](rtc::PacketTransportInternal*, const rtc::ReceivedPacket&) {
            lastNetworkActivityMs = rtc::TimeMillis();
        });
    }

    int GroupConnection::getRegatherOnFailedNetworksInterval() {
        return 2000;
    }
} // wrtc