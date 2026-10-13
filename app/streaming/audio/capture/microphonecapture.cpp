#include "microphonecapture.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <Limelight.h>

static constexpr uint16_t kMicrophonePacketType = 0x3003;

MicrophoneCapture::MicrophoneCapture(QObject* parent)
    : QObject(parent)
    , m_DeviceId(0)
    , m_ObtainedSpec({})
    , m_Encoder(nullptr)
    , m_Streaming(false)
    , m_StopEncoderThread(false)
    , m_Initialized(false)
    , m_Enabled(false)
    , m_FirstPacketLogged(false)
{
}

MicrophoneCapture::~MicrophoneCapture()
{
    stop();

    m_StopEncoderThread.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    if (m_EncoderThread.joinable()) {
        m_EncoderThread.join();
    }

    if (m_DeviceId != 0) {
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

    if (m_AudioSubsystemInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_AudioSubsystemInitialized = false;
    }
}

bool MicrophoneCapture::initialize(const std::string& deviceName, int bitrate)
{
    if (m_Initialized) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_InitSubSystem(SDL_INIT_AUDIO) failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }
    m_AudioSubsystemInitialized = true;

    m_Bitrate = std::max(6000, std::min(bitrate, 96000));

    int opusError = OPUS_OK;
    m_Encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &opusError);
    if (m_Encoder == nullptr || opusError != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "opus_encoder_create() failed for microphone capture: %s",
                    opus_strerror(opusError));
        return false;
    }

    opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(m_Bitrate));
    opus_encoder_ctl(m_Encoder, OPUS_SET_VBR(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(m_Encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(m_Encoder, OPUS_SET_LSB_DEPTH(16));
    opus_encoder_ctl(m_Encoder, OPUS_SET_DTX(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(m_Encoder, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(m_Encoder, OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_20_MS));

    SDL_AudioSpec desired = {};
    desired.freq = kSampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = kChannels;
    desired.samples = kFrameSize;
    desired.callback = &MicrophoneCapture::audioCallback;
    desired.userdata = this;

    const char* selectedDevice = deviceName.empty() ? nullptr : deviceName.c_str();
    m_DeviceId = SDL_OpenAudioDevice(selectedDevice, SDL_TRUE, &desired, &m_ObtainedSpec, 0);
    if (m_DeviceId == 0 && selectedDevice != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_OpenAudioDevice() failed for requested microphone '%s': %s. Falling back to default input device.",
                    selectedDevice,
                    SDL_GetError());
        m_DeviceId = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &desired, &m_ObtainedSpec, 0);
    }

    if (m_DeviceId == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL_OpenAudioDevice() failed for microphone capture: %s",
                    SDL_GetError());
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone capture device: %s (%d Hz, %d channels, format 0x%x, %d sample callback target)",
                selectedDevice != nullptr ? selectedDevice : "<default>",
                m_ObtainedSpec.freq,
                m_ObtainedSpec.channels,
                m_ObtainedSpec.format,
                m_ObtainedSpec.samples);

        if (m_ObtainedSpec.freq != kSampleRate || m_ObtainedSpec.format != AUDIO_S16SYS ||
            (m_ObtainedSpec.channels != kChannels && m_ObtainedSpec.channels != 2)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone capture format mismatch: got %d Hz, %d channels, format 0x%x",
                    m_ObtainedSpec.freq,
                    m_ObtainedSpec.channels,
                    m_ObtainedSpec.format);
        return false;
    }

    if (m_ObtainedSpec.samples != kFrameSize) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Microphone capture backend is delivering %d-sample callbacks; outgoing Opus packets will be paced at %d-sample (%d ms) intervals",
                    m_ObtainedSpec.samples,
                    kFrameSize,
                    (kFrameSize * 1000) / kSampleRate);
    }

    SDL_PauseAudioDevice(m_DeviceId, 1);
    m_SampleBuffer.reserve(kFrameSize * 4);
    m_StopEncoderThread.store(false, std::memory_order_release);
    m_EncoderThread = std::thread(&MicrophoneCapture::encoderLoop, this);
    m_Initialized = true;
    return true;
}

bool MicrophoneCapture::start()
{
    if (!m_Initialized || m_DeviceId == 0) {
        return false;
    }

    clearBufferedSamples();
    m_FirstPacketLogged = false;
    m_MicrophoneSequenceNumber = 0;
    m_Streaming.store(true, std::memory_order_release);
    m_BufferCondition.notify_all();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Microphone capture streaming started over the encrypted control stream");
    SDL_PauseAudioDevice(m_DeviceId, 0);
    return true;
}

void MicrophoneCapture::stop()
{
    if (m_DeviceId != 0) {
        SDL_PauseAudioDevice(m_DeviceId, 1);
    }

    m_Streaming.store(false, std::memory_order_release);
    clearBufferedSamples();
    m_BufferCondition.notify_all();
    if (m_DeviceId != 0) {
        SDL_CloseAudioDevice(m_DeviceId);
        m_DeviceId = 0;
    }
}

void MicrophoneCapture::setEnabled(bool enabled)
{
    m_Enabled = enabled;
    if (!m_Enabled) {
        stop();
    }
}

bool MicrophoneCapture::isEnabled() const
{
    return m_Enabled;
}

bool MicrophoneCapture::isStreaming() const
{
    return m_Streaming.load(std::memory_order_acquire);
}

void MicrophoneCapture::audioCallback(void* userdata, Uint8* stream, int len)
{
    try {
        auto* capture = static_cast<MicrophoneCapture*>(userdata);
        if (capture != nullptr) {
            capture->handleAudioData(stream, len);
        }
    }
    catch (...) {
    }
}

void MicrophoneCapture::handleAudioData(const Uint8* stream, int len)
{
    if (!m_Streaming.load(std::memory_order_acquire) || stream == nullptr || len <= 0) {
        return;
    }

    const auto* inputSamples = reinterpret_cast<const opus_int16*>(stream);
    const int sampleCount = len / static_cast<int>(sizeof(opus_int16));

    {
        std::lock_guard lock(m_BufferMutex);
        if (m_ObtainedSpec.channels == 2) {
            for (int i = 0; i + 1 < sampleCount; i += 2) {
                m_SampleBuffer.push_back(static_cast<opus_int16>(
                    (static_cast<int>(inputSamples[i]) + static_cast<int>(inputSamples[i + 1])) / 2));
            }
        }
        else {
            m_SampleBuffer.insert(m_SampleBuffer.end(), inputSamples, inputSamples + sampleCount);
        }
        constexpr size_t maxBufferedSamples = kFrameSize * 12;
        if (m_SampleBuffer.size() > maxBufferedSamples) {
            const auto trimSamples = m_SampleBuffer.size() - maxBufferedSamples;
            m_SampleBuffer.erase(m_SampleBuffer.begin(), m_SampleBuffer.begin() + trimSamples);
        }
    }
    m_BufferCondition.notify_one();
}

void MicrophoneCapture::encoderLoop()
{
    try {
    std::vector<opus_int16> frame(kFrameSize);
    const auto frameDuration = std::chrono::milliseconds((kFrameSize * 1000) / kSampleRate);
    auto nextSendDeadline = std::chrono::steady_clock::now();
    bool pacingActive = false;

    for (;;) {
        {
            std::unique_lock lock(m_BufferMutex);
            m_BufferCondition.wait(lock, [this] {
                return m_StopEncoderThread.load(std::memory_order_acquire) ||
                       (m_Streaming.load(std::memory_order_acquire) && m_SampleBuffer.size() >= static_cast<size_t>(kFrameSize));
            });

            if (m_StopEncoderThread.load(std::memory_order_acquire)) {
                break;
            }

            if (!m_Streaming.load(std::memory_order_acquire) || m_SampleBuffer.size() < static_cast<size_t>(kFrameSize)) {
                pacingActive = false;
                continue;
            }

            std::copy_n(m_SampleBuffer.begin(), kFrameSize, frame.begin());
            m_SampleBuffer.erase(m_SampleBuffer.begin(), m_SampleBuffer.begin() + kFrameSize);
        }

        const auto now = std::chrono::steady_clock::now();
        if (!pacingActive) {
            nextSendDeadline = now;
            pacingActive = true;
        }
        else if (now > nextSendDeadline + (frameDuration * 2)) {
            nextSendDeadline = now;
        }

        if (nextSendDeadline > now) {
            std::this_thread::sleep_until(nextSendDeadline);
        }

        nextSendDeadline += frameDuration;

        const int encodedBytes = opus_encode(m_Encoder,
                                             frame.data(),
                                             kFrameSize,
                                             m_EncodedPacket.data(),
                                             static_cast<opus_int32>(m_EncodedPacket.size()));
        if (encodedBytes <= 0) {
            continue;
        }

        if (encodedBytes > static_cast<int>(m_EncodedPacket.size())) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Dropping oversized microphone Opus packet: %d bytes",
                        encodedBytes);
            continue;
        }

        m_MicrophonePacket[0] = static_cast<unsigned char>(m_MicrophoneSequenceNumber >> 8);
        m_MicrophonePacket[1] = static_cast<unsigned char>(m_MicrophoneSequenceNumber & 0xff);
        m_MicrophonePacket[2] = 1;
        m_MicrophonePacket[3] = 0;
        std::memcpy(m_MicrophonePacket.data() + 4, m_EncodedPacket.data(), static_cast<size_t>(encodedBytes));
        ++m_MicrophoneSequenceNumber;

        const int sendResult = LiSendRawControlStreamPacket(kMicrophonePacketType,
                                                            m_MicrophonePacket.data(),
                                                            4 + encodedBytes);
        if (sendResult == 0 && !m_FirstPacketLogged) {
            m_FirstPacketLogged = true;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Sent first microphone control packet (%d bytes Opus)",
                        encodedBytes);
        }
        else if (sendResult != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "LiSendRawControlStreamPacket() failed for microphone capture: %d",
                        sendResult);
        }
    }
    }
    catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone encoder thread stopped after an exception: %s",
                     error.what());
    }
    catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Microphone encoder thread stopped after an unknown exception");
    }
}

void MicrophoneCapture::clearBufferedSamples()
{
    {
        std::lock_guard lock(m_BufferMutex);
        m_SampleBuffer.clear();
    }
}