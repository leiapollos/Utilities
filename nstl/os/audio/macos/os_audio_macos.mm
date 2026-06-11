//
// Created by André Leite on 11/06/2026.
//

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

struct OS_AudioOutput {
    AudioComponentInstance unit;
    OS_AudioRenderProc* renderProc;
    void* user;
};

// The HAL delivers property replies through the main run loop by default;
// an engine frame loop never services it the way CoreAudio expects, and
// component/property calls block forever inside coreaudiod traffic. NULL
// tells the HAL to run its own notification thread — the standard
// incantation for custom-loop apps.
static void os_audio_detach_hal_run_loop_(void) {
    AudioObjectPropertyAddress address = {};
    address.mSelector = kAudioHardwarePropertyRunLoop;
    address.mScope = kAudioObjectPropertyScopeGlobal;
    address.mElement = kAudioObjectPropertyElementMain;
    CFRunLoopRef nullLoop = 0;
    AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0, 0,
                               sizeof(nullLoop), &nullLoop);
}

static OSStatus os_audio_render_thunk_(void* inRefCon, AudioUnitRenderActionFlags* ioActionFlags,
                                       const AudioTimeStamp* inTimeStamp, UInt32 inBusNumber,
                                       UInt32 inNumberFrames, AudioBufferList* ioData) {
    (void)ioActionFlags;
    (void)inTimeStamp;
    (void)inBusNumber;
    OS_AudioOutput* output = (OS_AudioOutput*)inRefCon;
    if (ioData->mNumberBuffers >= 1u && ioData->mBuffers[0].mData) {
        output->renderProc((F32*)ioData->mBuffers[0].mData, (U32)inNumberFrames, output->user);
    }
    return noErr;
}

OS_AudioOutput* OS_audio_output_open(Arena* arena, U32 sampleRate, U32 channelCount,
                                     OS_AudioRenderProc* renderProc, void* user) {
    ASSERT_ALWAYS(arena != 0);
    ASSERT_ALWAYS(renderProc != 0);

    os_audio_detach_hal_run_loop_();

    AudioComponentDescription componentDesc = {};
    componentDesc.componentType = kAudioUnitType_Output;
    componentDesc.componentSubType = kAudioUnitSubType_DefaultOutput;
    componentDesc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent component = AudioComponentFindNext(0, &componentDesc);
    if (!component) {
        return 0;
    }

    OS_AudioOutput* output = ARENA_PUSH_STRUCT(arena, OS_AudioOutput);
    if (!output) {
        return 0;
    }
    MEMSET(output, 0, sizeof(*output));
    output->renderProc = renderProc;
    output->user = user;

    if (AudioComponentInstanceNew(component, &output->unit) != noErr) {
        return 0;
    }

    // The output unit converts to the device format internally; the render
    // proc always sees packed interleaved F32 at the requested rate.
    AudioStreamBasicDescription format = {};
    format.mSampleRate = (Float64)sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1u;
    format.mChannelsPerFrame = channelCount;
    format.mBitsPerChannel = 32u;
    format.mBytesPerFrame = channelCount * (U32)sizeof(F32);
    format.mBytesPerPacket = channelCount * (U32)sizeof(F32);

    AURenderCallbackStruct callback = {};
    callback.inputProc = os_audio_render_thunk_;
    callback.inputProcRefCon = output;

    if (AudioUnitSetProperty(output->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0u, &format, sizeof(format)) != noErr ||
        AudioUnitSetProperty(output->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0u, &callback, sizeof(callback)) != noErr ||
        AudioUnitInitialize(output->unit) != noErr ||
        AudioOutputUnitStart(output->unit) != noErr) {
        AudioComponentInstanceDispose(output->unit);
        return 0;
    }

    return output;
}

void OS_audio_output_close(OS_AudioOutput* output) {
    if (!output || !output->unit) {
        return;
    }
    AudioOutputUnitStop(output->unit);
    AudioUnitUninitialize(output->unit);
    AudioComponentInstanceDispose(output->unit);
    output->unit = 0;
}
