//
// Created by André Leite on 11/06/2026.
//
// OS audio output surface: open the default output device at a fixed
// format, drive a render proc from the OS audio thread, close. The proc
// fills interleaved F32 frames; it must be lock-free and allocation-free.
//

#pragma once

typedef void OS_AudioRenderProc(F32* samples, U32 frameCount, void* user);

struct OS_AudioOutput;

OS_AudioOutput* OS_audio_output_open(Arena* arena, U32 sampleRate, U32 channelCount,
                                     OS_AudioRenderProc* renderProc, void* user);
void OS_audio_output_close(OS_AudioOutput* output);
