//
// Created by André Leite on 11/06/2026.
//
// WASAPI shared-mode output, event-driven render thread. Written blind per
// standing practice; AUTOCONVERTPCM lets the engine keep its fixed
// 48 kHz F32 format regardless of the device mix format.
//

#include <mmdeviceapi.h>
#include <audioclient.h>

#if !defined(AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM)
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000u
#endif
#if !defined(AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY)
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000u
#endif

struct OS_AudioOutput {
    IAudioClient* client;
    IAudioRenderClient* renderClient;
    HANDLE renderEvent;
    HANDLE renderThread;
    U32 bufferFrames;
    OS_AudioRenderProc* renderProc;
    void* user;
    volatile LONG quit;
};

static DWORD WINAPI os_audio_render_thread_(LPVOID param) {
    OS_AudioOutput* output = (OS_AudioOutput*)param;
    while (!InterlockedCompareExchange(&output->quit, 0, 0)) {
        DWORD wait = WaitForSingleObject(output->renderEvent, 2000);
        if (wait != WAIT_OBJECT_0) {
            continue;
        }
        UINT32 padding = 0u;
        if (FAILED(output->client->GetCurrentPadding(&padding))) {
            continue;
        }
        UINT32 frames = output->bufferFrames - padding;
        if (frames == 0u) {
            continue;
        }
        BYTE* data = 0;
        if (FAILED(output->renderClient->GetBuffer(frames, &data)) || !data) {
            continue;
        }
        output->renderProc((F32*)data, (U32)frames, output->user);
        output->renderClient->ReleaseBuffer(frames, 0);
    }
    return 0;
}

OS_AudioOutput* OS_audio_output_open(Arena* arena, U32 sampleRate, U32 channelCount,
                                     OS_AudioRenderProc* renderProc, void* user) {
    ASSERT_ALWAYS(arena != 0);
    ASSERT_ALWAYS(renderProc != 0);

    OS_AudioOutput* output = ARENA_PUSH_STRUCT(arena, OS_AudioOutput);
    if (!output) {
        return 0;
    }
    MEMSET(output, 0, sizeof(*output));
    output->renderProc = renderProc;
    output->user = user;

    IMMDeviceEnumerator* enumerator = 0;
    IMMDevice* device = 0;
    B32 ok = 0;
    do {
        if (FAILED(CoInitializeEx(0, COINIT_MULTITHREADED)) &&
            FAILED(CoInitializeEx(0, COINIT_APARTMENTTHREADED))) {
            // Already initialized on this thread is fine; CoCreateInstance
            // decides below.
        }
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), 0, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), (void**)&enumerator))) {
            break;
        }
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
            break;
        }
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, 0,
                                    (void**)&output->client))) {
            break;
        }

        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = (WORD)channelCount;
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = 32u;
        format.nBlockAlign = (WORD)(channelCount * sizeof(F32));
        format.nAvgBytesPerSec = sampleRate * channelCount * (U32)sizeof(F32);

        const REFERENCE_TIME bufferDuration = 200000; // 20 ms in 100ns units
        if (FAILED(output->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                              AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                              AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                              bufferDuration, 0, &format, 0))) {
            break;
        }

        output->renderEvent = CreateEventA(0, FALSE, FALSE, 0);
        if (!output->renderEvent ||
            FAILED(output->client->SetEventHandle(output->renderEvent)) ||
            FAILED(output->client->GetBufferSize(&output->bufferFrames)) ||
            FAILED(output->client->GetService(__uuidof(IAudioRenderClient),
                                              (void**)&output->renderClient))) {
            break;
        }

        output->renderThread = CreateThread(0, 0, os_audio_render_thread_, output, 0, 0);
        if (!output->renderThread) {
            break;
        }
        if (FAILED(output->client->Start())) {
            break;
        }
        ok = 1;
    } while (0);

    if (device) {
        device->Release();
    }
    if (enumerator) {
        enumerator->Release();
    }
    if (!ok) {
        OS_audio_output_close(output);
        return 0;
    }
    return output;
}

void OS_audio_output_close(OS_AudioOutput* output) {
    if (!output) {
        return;
    }
    InterlockedExchange(&output->quit, 1);
    if (output->renderEvent) {
        SetEvent(output->renderEvent);
    }
    if (output->renderThread) {
        WaitForSingleObject(output->renderThread, 5000);
        CloseHandle(output->renderThread);
        output->renderThread = 0;
    }
    if (output->client) {
        output->client->Stop();
    }
    if (output->renderClient) {
        output->renderClient->Release();
        output->renderClient = 0;
    }
    if (output->client) {
        output->client->Release();
        output->client = 0;
    }
    if (output->renderEvent) {
        CloseHandle(output->renderEvent);
        output->renderEvent = 0;
    }
}
