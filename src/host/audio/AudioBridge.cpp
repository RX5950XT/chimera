#include "AudioBridge.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ole32.lib")

namespace chimera::audio {

AudioBridge &AudioBridge::instance() {
    static AudioBridge inst;
    return inst;
}

bool AudioBridge::initialize(const Config &config) {
    if (m_initialized) return true;
    m_config = config;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "AudioBridge: CoInitializeEx failed 0x%08lx\n", static_cast<unsigned long>(hr));
        return false;
    }
    // S_OK means we own this COM init; RPC_E_CHANGED_MODE means someone else does
    m_coOwned = (hr == S_OK);

    if (!initRenderDevice()) {
        if (m_coOwned) CoUninitialize();
        m_coOwned = false;
        return false;
    }

    m_running = true;
    m_initialized = true;
    m_renderThread = std::thread([this]() { renderThreadLoop(); });
    return true;
}

void AudioBridge::shutdown() {
    if (!m_initialized) return;
    m_running = false;
    m_queueCv.notify_all();
    if (m_renderThread.joinable()) m_renderThread.join();

    if (m_pRenderClient) { m_pRenderClient->Release(); m_pRenderClient = nullptr; }
    if (m_pAudioClient) { m_pAudioClient->Release(); m_pAudioClient = nullptr; }
    if (m_pDevice) { m_pDevice->Release(); m_pDevice = nullptr; }
    if (m_pEnumerator) { m_pEnumerator->Release(); m_pEnumerator = nullptr; }

    if (m_pCaptureReader) { m_pCaptureReader->Release(); m_pCaptureReader = nullptr; }
    if (m_pCaptureClient) { m_pCaptureClient->Release(); m_pCaptureClient = nullptr; }
    if (m_pCaptureDevice) { m_pCaptureDevice->Release(); m_pCaptureDevice = nullptr; }

    if (m_coOwned) {
        CoUninitialize();
        m_coOwned = false;
    }
    m_initialized = false;
}

bool AudioBridge::initRenderDevice() {
    auto cleanup = [this]() {
        if (m_pRenderClient) { m_pRenderClient->Release(); m_pRenderClient = nullptr; }
        if (m_pAudioClient) { m_pAudioClient->Release(); m_pAudioClient = nullptr; }
        if (m_pDevice) { m_pDevice->Release(); m_pDevice = nullptr; }
        if (m_pEnumerator) { m_pEnumerator->Release(); m_pEnumerator = nullptr; }
    };

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) return false;

    hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_pDevice);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_pAudioClient);
    if (FAILED(hr)) { cleanup(); return false; }

    WAVEFORMATEX *pwfx = nullptr;
    hr = m_pAudioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) { cleanup(); return false; }

    pwfx->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    pwfx->nChannels = static_cast<WORD>(m_config.channels);
    pwfx->nSamplesPerSec = static_cast<DWORD>(m_config.sampleRate);
    pwfx->wBitsPerSample = 32;
    pwfx->nBlockAlign = pwfx->nChannels * sizeof(float);
    pwfx->nAvgBytesPerSec = pwfx->nSamplesPerSec * pwfx->nBlockAlign;
    // GetMixFormat usually returns WAVE_FORMAT_EXTENSIBLE (cbSize=22). We now
    // describe a plain WAVEFORMATEX IEEE_FLOAT, so cbSize MUST be 0 — leaving the
    // stale EXTENSIBLE cbSize makes the struct inconsistent and Initialize reject it.
    pwfx->cbSize = 0;

    // The forced channels/rate above will differ from the device mix format on most
    // endpoints; in shared mode that needs AUTOCONVERTPCM (+ default-quality SRC) so
    // WASAPI inserts a converter instead of failing with AUDCLNT_E_UNSUPPORTED_FORMAT.
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        10000000, // 1-second buffer
        0,
        pwfx,
        nullptr);
    CoTaskMemFree(pwfx);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pAudioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_pRenderClient);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pAudioClient->Start();
    if (FAILED(hr)) { cleanup(); return false; }

    return true;
}

bool AudioBridge::initCaptureDevice() {
    if (!m_pEnumerator) return false;

    auto cleanup = [this]() {
        if (m_pCaptureReader) { m_pCaptureReader->Release(); m_pCaptureReader = nullptr; }
        if (m_pCaptureClient) { m_pCaptureClient->Release(); m_pCaptureClient = nullptr; }
        if (m_pCaptureDevice) { m_pCaptureDevice->Release(); m_pCaptureDevice = nullptr; }
    };

    HRESULT hr = m_pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &m_pCaptureDevice);
    if (FAILED(hr)) return false;

    hr = m_pCaptureDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&m_pCaptureClient);
    if (FAILED(hr)) { cleanup(); return false; }

    WAVEFORMATEX *pwfx = nullptr;
    hr = m_pCaptureClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pCaptureClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,
        0,
        pwfx,
        nullptr);
    CoTaskMemFree(pwfx);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pCaptureClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pCaptureReader);
    if (FAILED(hr)) { cleanup(); return false; }

    hr = m_pCaptureClient->Start();
    if (FAILED(hr)) { cleanup(); return false; }

    return true;
}

void AudioBridge::renderThreadLoop() {
    while (m_running) {
        if (!m_pAudioClient || !m_pRenderClient) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        UINT32 padding = 0;
        HRESULT hr = m_pAudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        UINT32 available = m_bufferFrameCount - padding;
        if (available > 0) {
            drainQueueToWasapi(available);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void AudioBridge::drainQueueToWasapi(UINT32 numFramesAvailable) {
    BYTE *pData = nullptr;
    HRESULT hr = m_pRenderClient->GetBuffer(numFramesAvailable, &pData);
    if (FAILED(hr)) return;

    UINT32 framesToWrite = numFramesAvailable;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        size_t samplesAvailable = m_frameQueue.size();
        UINT32 framesInQueue = static_cast<UINT32>(samplesAvailable / m_config.channels);
        if (framesToWrite > framesInQueue) framesToWrite = framesInQueue;

        if (framesToWrite > 0) {
            size_t samplesToCopy = static_cast<size_t>(framesToWrite) * m_config.channels;
            std::memcpy(pData, m_frameQueue.data(), samplesToCopy * sizeof(float));
            m_frameQueue.erase(m_frameQueue.begin(), m_frameQueue.begin() + samplesToCopy);
        }
    }

    if (framesToWrite < numFramesAvailable) {
        // Fill remaining with silence
        size_t offset = static_cast<size_t>(framesToWrite) * m_config.channels * sizeof(float);
        std::memset(pData + offset, 0,
                    (static_cast<size_t>(numFramesAvailable) - framesToWrite) * m_config.channels * sizeof(float));
    }

    m_pRenderClient->ReleaseBuffer(numFramesAvailable, 0);
}

void AudioBridge::writeGuestFrames(const std::vector<float> &frames) {
    if (!m_running || !m_initialized) return;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_frameQueue.insert(m_frameQueue.end(), frames.begin(), frames.end());
        // Prevent unbounded growth: cap at ~2 seconds of audio
        size_t maxSamples = static_cast<size_t>(m_config.sampleRate * m_config.channels * 2);
        if (m_frameQueue.size() > maxSamples) {
            m_frameQueue.erase(m_frameQueue.begin(), m_frameQueue.begin() + (m_frameQueue.size() - maxSamples));
        }
    }
    m_queueCv.notify_one();
}

void AudioBridge::readHostMicrophone(std::vector<float> &outFrames) {
    if (!m_running || !m_pCaptureReader) {
        outFrames.resize(m_config.bufferSize * m_config.channels, 0.0f);
        return;
    }

    UINT32 packetLength = 0;
    HRESULT hr = m_pCaptureReader->GetNextPacketSize(&packetLength);
    if (FAILED(hr) || packetLength == 0) {
        outFrames.resize(m_config.bufferSize * m_config.channels, 0.0f);
        return;
    }

    BYTE *pData = nullptr;
    UINT32 numFramesAvailable = 0;
    DWORD flags = 0;
    hr = m_pCaptureReader->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
    if (FAILED(hr)) {
        outFrames.resize(m_config.bufferSize * m_config.channels, 0.0f);
        return;
    }

    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        size_t samples = static_cast<size_t>(numFramesAvailable) * m_config.channels;
        outFrames.resize(samples);
        std::memcpy(outFrames.data(), pData, samples * sizeof(float));
    } else {
        outFrames.resize(static_cast<size_t>(numFramesAvailable) * m_config.channels, 0.0f);
    }

    m_pCaptureReader->ReleaseBuffer(numFramesAvailable);
}

bool AudioBridge::isRunning() const {
    return m_running && m_initialized;
}

} // namespace chimera::audio
