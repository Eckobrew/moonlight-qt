#pragma once

#include <QObject>
#include <atomic>
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>
#include <opus.h>

class MicrophoneCapture : public QObject
{
    Q_OBJECT

public:
    explicit MicrophoneCapture(QObject* parent = nullptr);
    ~MicrophoneCapture() override;

    bool initialize(const std::string& deviceName = {}, int bitrate = 64000);
    bool start();
    void stop();

    void setEnabled(bool enabled);
    bool isEnabled() const;
    bool isStreaming() const;

private:
    static void SDLCALL audioCallback(void* userdata, Uint8* stream, int len);
    void handleAudioData(const Uint8* stream, int len);
    void clearBufferedSamples();
    void encoderLoop();

    SDL_AudioDeviceID m_DeviceId;
    SDL_AudioSpec m_ObtainedSpec;
    OpusEncoder* m_Encoder;
    std::vector<opus_int16> m_SampleBuffer;
    std::array<unsigned char, 248> m_EncodedPacket;
    std::array<unsigned char, 252> m_MicrophonePacket;
    std::atomic_bool m_Streaming;
    std::atomic_bool m_StopEncoderThread;
    bool m_AudioSubsystemInitialized = false;
    bool m_Initialized;
    bool m_Enabled;
    bool m_FirstPacketLogged;
    uint16_t m_MicrophoneSequenceNumber = 0;
    int m_Bitrate = 64000;
    std::mutex m_BufferMutex;
    std::condition_variable m_BufferCondition;
    std::thread m_EncoderThread;

    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 1;
    static constexpr int kFrameSize = 960;
};