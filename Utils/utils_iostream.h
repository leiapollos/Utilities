#pragma once

#include "utils_helpers.h"
#include "../String/string.hpp"
#include "windowsdefs.h"

namespace utils {

    struct output {
        output& operator<<(const char* str) {
            if (!str) return *this;
            int len = 0;
            
            while (str[len] != '\0') len++;

            int processed = 0;
            while (processed < len) {
                int chunkSize = len - processed;
                chunkSize = min(chunkSize, _maxBufferSize - 1);
                char buffer[_maxBufferSize];
                for (int i = 0; i < chunkSize; ++i) {
                    buffer[i] = str[processed + i];
                }
                buffer[chunkSize] = '\0';

#ifdef _WIN32
                const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
                WriteConsoleA(hConsole, buffer, chunkSize, 0, 0);
#else
                write(1, buffer, chunk_size);
#endif

                processed += chunkSize;
			}
            return *this;
        }

        // Integer types
        output& operator<<(int num) {
            return print_signed<int>(num);
        }

        output& operator<<(long num) {
            return print_signed<long>(num);
        }

        output& operator<<(long long num) {
            return print_signed<long long>(num);
        }

        output& operator<<(uint32_t num) {
            return print_unsigned<uint32_t>(num);
        }

        output& operator<<(unsigned long num) {
            return print_unsigned<unsigned long>(num);
        }

        output& operator<<(unsigned long long num) {
            return print_unsigned<unsigned long long>(num);
        }

        // Floating types
        output& operator<<(float num) {
            return print_float(static_cast<double>(num));
        }

        output& operator<<(double num) {
            return print_float(num);
        }

    private:
        static constexpr int _maxBufferSize = 32;
        static constexpr int _floatDecimalPrecision = 6;

        template<typename T>
        output& print_signed(T num) {
            char buffer[_maxBufferSize];
            int i = 0;
            bool negative = false;

            if (num < 0) {
                negative = true;
                // Prevent overflow when num is the minimum value
                if (num == min_value<T>()) {
                    return *this << "Number too large";
                }
                num = -num;
            }

            // Convert number to string
            do {
                if (i >= _maxBufferSize - 1) break;
                buffer[i++] = '0' + (num % 10);
                num /= 10;
            } while (num > 0);

            if (negative) {
                if (i >= _maxBufferSize - 1) return *this << "Number too large";
                buffer[i++] = '-';
            }

            buffer[i] = '\0';

            // Reverse the buffer
            for (int j = 0; j < i / 2; ++j) {
                const char temp = buffer[j];
                buffer[j] = buffer[i - j - 1];
                buffer[i - j - 1] = temp;
            }
            *this << buffer;
            return *this;
        }

        template<typename T>
        output& print_unsigned(T num) {
            char buffer[_maxBufferSize];
            int i = 0;

            // Convert number to string
            do {
                if (i >= _maxBufferSize - 1) break;
                buffer[i++] = '0' + (num % 10);
                num /= 10;
            } while (num > 0);

            buffer[i] = '\0';

            // Reverse the buffer safely
            for (int j = 0; j < i / 2; ++j) {
                const char temp = buffer[j];
                buffer[j] = buffer[i - j - 1];
                buffer[i - j - 1] = temp;
            }
            *this << buffer;
            return *this;
        }

        output& print_float(double num) {
            if (num < 0) {
                *this << "-";
                num = -num;
            }

            // Integer part
            const long intPart = static_cast<long>(num);
            *this << intPart << ".";

            // Fractional part with fixed precision
            double fracPart = num - intPart;
            for (int j = 0; j < _floatDecimalPrecision; ++j) {
                fracPart *= 10;
                const int digit = static_cast<int>(fracPart);
                *this << static_cast<char>('0' + digit);
                fracPart -= digit;
            }

            return *this;
        }
    };

    inline output cout;
}