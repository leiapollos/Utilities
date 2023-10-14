//
//  Logger.hpp
//  Utilities
//
//  Created by André Leite on 20/05/2023.
//
 
#pragma once
 
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
 
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _DEBUG
#define LOG(level, format, ...) Logger::getInstance().log(level, format, __VA_ARGS__)
#else
#define LOG(level, format, ...)
#endif

static constexpr bool k_shouldExceptOnCritical = true;

enum class LogLevel {
	Debug = 0,
	Info,
	Warning,
	Error,
	Critical,
};
 
// ANSI escape codes for color
namespace LoggerColor {
	const std::string RESET = "\033[0m";
	const std::string RED = "\033[31m";
	const std::string GREEN = "\033[32m";
	const std::string YELLOW = "\033[33m";
	const std::string BLUE = "\033[34m";
	const std::string MAGENTA = "\033[35m";
}
  

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    Logger(const LogLevel level = LogLevel::Debug, const bool printNewline = true, const bool buffering = false)
    : _logLevel(level), _printNewline(printNewline), _buffering(buffering) {
#ifdef _WIN32
        enableANSIEscapeSequences();
#endif
        _isTerminalSmart = isTerminalSmart();
    }
 
    virtual ~Logger() = default;
	Logger(const Logger&) = delete;
	Logger(const Logger&&) = delete;
	Logger& operator=(const Logger& other) = delete;
	Logger& operator=(const Logger&& other) = delete;
 
    template<typename... Args>
    void log(const LogLevel msgLevel, const std::string& format, Args... args) {
        if (msgLevel < _logLevel) return;
 
        const std::vector<std::string> argStrings {toString(args)...};
        std::ostringstream message;
        appendLogLevelPrefix(msgLevel, message);
        formatMessage(format, argStrings, message);
        (_isTerminalSmart)? message << LoggerColor::RESET : message; // Reset color
 
        std::unique_lock<std::mutex> lock(_logMutex);
        if (_buffering) {
            _logBuffer << message.str().c_str();
            if (_printNewline) {
                _logBuffer << '\n';
            }
        } else {
            output(message.str());
            if (_printNewline) {
                output("\n");
            }
        }

		if (k_shouldExceptOnCritical) {
			if (msgLevel == LogLevel::Critical) {
				lock.release();
				throw std::runtime_error("Critical log error!");
			}
		}
    }
 
    virtual void flush() {
        std::lock_guard<std::mutex> lock(_logMutex);
        if (_buffering) {
            output(_logBuffer.str());
            _logBuffer.str(""); // Clear the buffer
            _logBuffer.clear();
        }
    }
 
    virtual void output(const std::string& message) {
        std::cout << message;
    }

protected:
    LogLevel _logLevel;
    const bool _printNewline;
    const bool _buffering;
    bool _isTerminalSmart;
    std::ostringstream _logBuffer;
    std::mutex _logMutex;
 
private:
	void appendLogLevelPrefix(const LogLevel msgLevel, std::ostringstream& message) const {
	    switch (msgLevel) {
	        case LogLevel::Debug:
	            message << ((_isTerminalSmart)? LoggerColor::GREEN : "") << "[Debug] ";
	            break;
	        case LogLevel::Info:
	            message << ((_isTerminalSmart)? LoggerColor::BLUE : "") << "[Info] ";
	            break;
	        case LogLevel::Warning:
	            message << ((_isTerminalSmart)? LoggerColor::YELLOW : "") << "[Warning] ";
	            break;
	        case LogLevel::Error:
	            message << ((_isTerminalSmart)? LoggerColor::RED : "") << "[Error] ";
	            break;
	        case LogLevel::Critical:
	            message << ((_isTerminalSmart)? LoggerColor::MAGENTA : "") << "[Critical] ";
	            break;
	    }
	}
 
	static void formatMessage(const std::string& format, const std::vector<std::string>& argStrings, std::ostringstream& message) {
	    bool braceOpen = false;
	    std::string innerBuffer;
	    size_t argIndex = 0;
	    for(char c : format) {
	        if(braceOpen) {
	            if(c == '}') {
	                size_t index;
	                if (!innerBuffer.empty()) {
	                    index = std::stoi(innerBuffer);
	                } else {
	                    index = argIndex++;
	                }
	                innerBuffer.clear();
	                if(index >= argStrings.size()) {
	                    throw std::runtime_error("Placeholder index out of bounds");
	                }
	                message << argStrings[index];
	                braceOpen = false;
	            } else if(std::isdigit(c)) {
	                innerBuffer += c;
	            } else {
	                throw std::runtime_error("Invalid placeholder format");
	            }
	        } else {
	            if(c == '{') {
	                braceOpen = true;
	            } else {
	                message << c;
	            }
	        }
	    }
	}

	// Utility function to convert any type to string
	template<typename T>
	static std::string toString(const T& t) {
	    std::ostringstream oss;
	    oss << t;
	    return oss.str();
	}

    static bool isTerminalSmart() {
#ifdef _WIN32
		const DWORD WINDOWS_10_VERSION = 10;
		DWORD type;
	    if (GetProductInfo(WINDOWS_10_VERSION, 0, 0, 0, &type)) {
	        return true; // Windows 10 or later
	    }
	    return false;
#else
	    if (const char* term = std::getenv("TERM")) {
	        return std::string(term) != "dumb";
	    }
	    return false;
#endif
    }

    #ifdef _WIN32
	static void enableANSIEscapeSequences() {
	    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	    if (hOut == INVALID_HANDLE_VALUE) {
	        return;
	    }
	 
	    DWORD dwMode = 0;
	    if (!GetConsoleMode(hOut, &dwMode)) {
	        return;
	    }
	 
	    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	    if (!SetConsoleMode(hOut, dwMode)) {
	        return;
	    }
	}
#endif
};
 
 
class FileLogger : public Logger {
public:
    FileLogger(const std::string& filename, LogLevel level = LogLevel::Info, const bool printNewline = true, const bool buffering = false)
    : Logger(level, printNewline, buffering), _logFile(filename, std::ofstream::out | std::ofstream::app) {
        if (!_logFile.is_open()) {
            throw std::runtime_error("Could not open log file");
        }
    }
 
    ~FileLogger() override {
        if (_logFile.is_open()) {
            _logFile.close();
        }
    }
 
    void output(const std::string& message) override {
        if (!_logFile.is_open()) {
            throw std::runtime_error("Log file is not open");
        }
        _logFile << message;
    }

private:
    std::ofstream _logFile;
};