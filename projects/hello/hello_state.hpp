#pragma once

#define HELLO_STATE_VERSION 1u

struct HelloState {
    U8 text[128];
    U32 textLength;
    U32 clickCount;
};
