//
// Created by Laky64 on 23/08/2023.
//

#include "media_reader_factory.hpp"

#include "ntgcalls/io/file_reader.hpp"
#include "ntgcalls/io/shell_reader.hpp"

namespace ntgcalls {
    MediaReaderFactory::MediaReaderFactory(const MediaDescription& desc) {
        if (desc.audio) {
            audio = fromInput(desc.audio.value());
        }
        if (desc.video) {
            video = fromInput(desc.video.value());
        }
    }

    std::shared_ptr<BaseReader> MediaReaderFactory::fromInput(const BaseMediaDescription& desc) {
        constexpr auto allowedFlags = BaseMediaDescription::InputMode::NoLatency;
        bool noLatency = desc.inputMode & BaseMediaDescription::InputMode::NoLatency;
        // SUPPORTED ENCODERS
        if ((desc.inputMode & (BaseMediaDescription::InputMode::File | allowedFlags)) == desc.inputMode) {
            return std::make_shared<FileReader>(desc.input, noLatency);
        }
        if ((desc.inputMode & (BaseMediaDescription::InputMode::Shell | allowedFlags)) == desc.inputMode) {
#ifdef BOOST_ENABLED
            return std::make_shared<ShellReader>(desc.input, noLatency);
#else
            throw ShellError("Shell execution is not yet supported on your OS/Architecture");
#endif
        }
        if ((desc.inputMode & (BaseMediaDescription::InputMode::FFmpeg | allowedFlags)) == desc.inputMode) {
            throw FFmpegError("FFmpeg encoder is not yet supported");
        }
        throw InvalidParams("Encoder not found");
    }

    MediaReaderFactory::~MediaReaderFactory() {
        audio = nullptr;
        video = nullptr;
    }

} // ntgcalls