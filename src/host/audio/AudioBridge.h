#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// Forward declare WASAPI COM interfaces
struct IMMDeviceEnumerator;
struct IMMDevice;
struct IAudioClient;
struct IAudioRenderClient;
struct IAudioCaptureClient;

typedef unsigned int UINT32; // Use standard type for header portability

namespace chimera::audio {

/**
 * @brief Bridges Android guest audio (VirtIO-snd) to Windows host audio (WASAPI).
 *
 * Implements WASAPI shared-mode output with a background render thread.
 * Guest audio frames are pushed to a lock-protected queue and drained by the thread.
 */
class AudioBridge {
public:
    struct Config {
        int sampleRate = 48000;
        int channels = 2;
        int bufferSize = 1024;
    };

    static AudioBridge &instance();

    bool initialize(const Config &config);
    void shutdown();

    // Called by guest virtio-snd device when audio frames are ready
    void writeGuestFrames(const std::vector<float> &frames);

    // Called to read microphone input from host and send to guest
    void readHostMicrophone(std::vector<float> &outFrames);

    bool isRunning() const;

private:
    AudioBridge() = default;
    ~AudioBridge() { shutdown(); }

    void renderThreadLoop();
    bool initRenderDevice();
    bool initCaptureDevice();
    void drainQueueToWasapi(UINT32 numFramesAvailable);

    bool m_running = false;
    std::atomic<bool> m_initialized{false};
    Config m_config;

    // WASAPI render
    IMMDeviceEnumerator *m_pEnumerator = nullptr;
    IMMDevice *m_pDevice = nullptr;
    IAudioClient *m_pAudioClient = nullptr;
    IAudioRenderClient *m_pRenderClient = nullptr;
    UINT32 m_bufferFrameCount = 0;

    // WASAPI capture
    IMMDevice *m_pCaptureDevice = nullptr;
    IAudioClient *m_pCaptureClient = nullptr;
    IAudioCaptureClient *m_pCaptureReader = nullptr;

    // Threading
    std::thread m_renderThread;
    std::vector<float> m_frameQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
};

} // namespace chimera::audio
