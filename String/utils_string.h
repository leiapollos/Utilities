#pragma once

#include "string.hpp"

namespace utils {
    inline string to_string(const int32_t val) {
        const bool neg = (val < 0);
        uint32_t n = neg ? static_cast<uint32_t>(-val) : static_cast<uint32_t>(val);
        char buf[32];
        int32_t i = 0;
        do {
            buf[i++] = static_cast<char>('0' + (n % 10));
            n /= 10;
        }
        while (n > 0);
        if (neg) {
            buf[i++] = '-';
        }
        buf[i] = '\0';
        for (int32_t start = 0, end = i - 1; start < end; start++, end--) {
            const char tmp = buf[start];
            buf[start] = buf[end];
            buf[end] = tmp;
        }
        return {buf};
    }

    inline string to_string(const int64_t val) {
        const bool neg = (val < 0);
        uint64_t n = neg ? static_cast<uint64_t>(-val) : static_cast<uint64_t>(val);
        char buf[64];
        int32_t i = 0;
        do {
            buf[i++] = static_cast<char>('0' + (n % 10));
            n /= 10;
        }
        while (n > 0);
        if (neg) {
            buf[i++] = '-';
        }
        buf[i] = '\0';
        for (int32_t start = 0, end = i - 1; start < end; start++, end--) {
            const char tmp = buf[start];
            buf[start] = buf[end];
            buf[end] = tmp;
        }
        return {buf};
    }

    inline string to_string(uint32_t val) {
        char buf[32];
        int32_t i = 0;
        do {
            buf[i++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
        while (val > 0);
        buf[i] = '\0';
        for (int32_t start = 0, end = i - 1; start < end; start++, end--) {
            const char tmp = buf[start];
            buf[start] = buf[end];
            buf[end] = tmp;
        }
        return {buf};
    }

    inline string to_string(uint64_t val) {
        char buf[64];
        int32_t i = 0;
        do {
            buf[i++] = static_cast<char>('0' + (val % 10));
            val /= 10;
        }
        while (val > 0);
        buf[i] = '\0';
        for (int32_t start = 0, end = i - 1; start < end; start++, end--) {
            const char tmp = buf[start];
            buf[start] = buf[end];
            buf[end] = tmp;
        }
        return {buf};
    }

    inline string to_string(float val) {
        const bool neg = (val < 0.0f);
        if (neg) {
            val = -val;
        }
        const int32_t intPart = static_cast<int32_t>(val);
        float frac = val - intPart;
        string result = to_string(intPart);
        if (neg) {
            if (intPart == 0) {
                result = string("-") + result;
            }
        }
        if (frac != 0.0f) {
            result += ".";
            for (int32_t i = 0; i < 6; i++) {
                frac *= 10.0f;
                const int32_t digit = static_cast<int32_t>(frac);
                frac -= digit;
                char c[2];
                c[0] = static_cast<char>('0' + digit);
                c[1] = '\0';
                result += c;
                if (frac == 0.0f) {
                    break;
                }
            }
        }
        return result;
    }

    inline string to_string(double val) {
        const bool neg = (val < 0.0);
        if (neg) {
            val = -val;
        }
        const int64_t intPart = static_cast<int64_t>(val);
        double frac = val - static_cast<double>(intPart);
        string result = to_string(intPart);
        if (neg) {
            if (intPart == 0) {
                result = string("-") + result;
            }
        }
        if (frac != 0.0) {
            result += ".";
            for (int32_t i = 0; i < 6; i++) {
                frac *= 10.0;
                const int32_t digit = static_cast<int32_t>(frac);
                frac -= digit;
                char c[2];
                c[0] = static_cast<char>('0' + digit);
                c[1] = '\0';
                result += c;
                if (frac == 0.0) {
                    break;
                }
            }
        }
        return result;
    }

    inline string to_string(const char* val) {
        if (!val) {
            return {};
        }
        return {val};
    }


    inline bool is_digit(const char c) {
        return c >= '0' && c <= '9';
    }

    inline int stoi(const char* str) {
        int result = 0;
        int i = 0;
        int sign = 1;

        if (str[0] == '-') {
            sign = -1;
            i++;
        } else if (str[0] == '+') {
            i++;
        }

        while (str[i] != '\0') {
            if (!is_digit(str[i])) {
                break;
            }
            result = (result * 10) + (str[i] - '0');
            i++;
        }

        return sign * result;
    }
}
