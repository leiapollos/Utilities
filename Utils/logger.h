#pragma once

#include "utils_iostream.h"
#include "../String/string.hpp"
#include "../String/utils_string.h"
#include "../Vector/vector.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

namespace utils {
#ifdef _DEBUG
#define LOG(level, format, ...) utils::logger::get_instance().log(level, format, __VA_ARGS__)
#else
#define LOG(level, format, ...)
#endif

	class logger {
	public:
		enum class log_level {
			Debug = 0,
			Info,
			Warning,
			Error,
			Critical,
		};

		// ANSI escape codes for color
		//struct log_color {
			const string RESET = "\033[0m";
			const string RED = "\033[31m";
			const string GREEN = "\033[32m";
			const string YELLOW = "\033[33m";
			const string BLUE = "\033[34m";
			const string MAGENTA = "\033[35m";
		//};

		static logger& get_instance() {
			static logger instance;
			return instance;
		}

		logger(const log_level level = log_level::Debug, const bool printNewline = true, const bool buffering = false)
			: _logLevel(level), _printNewline(printNewline), _buffering(buffering) {
#ifdef _WIN32
			enableANSIEscapeSequences();
#endif
			_isTerminalSmart = is_terminal_smart();
		}

		virtual ~logger() = default;

		static void populate_arg_strings(utils::vector<utils::string>& /*vec*/) {
			// Do nothing
		}
		
		template<typename T, typename... Rest>
		static void populate_arg_strings(utils::vector<utils::string>& vec, T first, Rest... rest) {
			vec.push_back(utils::to_string(first));
			populate_arg_strings(vec, rest...);
		}
		
		template<typename... Args>
		void log(const log_level msgLevel, const string& format, Args... args) {
			if (msgLevel < _logLevel) return;
			vector<string> argStrings;
			populate_arg_strings(argStrings, args...);
			
			utils::string message;
			append_log_level_prefix(msgLevel, message);
			format_message(format, argStrings, message);
			(_isTerminalSmart) ? message += RESET : message; // Reset color

			if (_buffering) {
				_logBuffer += message.c_str();
				if (_printNewline) {
					_logBuffer += '\n';
				}
			}
			else {
				output(message.c_str());
				if (_printNewline) {
					output("\n");
				}
			}
		}

		virtual void flush() {
			if (_buffering) {
				output(_logBuffer.c_str());
				_logBuffer = ""; // Clear the buffer
				_logBuffer.clear();
			}
		}

		virtual void output(const string& message) {
			utils::cout << message.c_str();
		}

	protected:
		log_level _logLevel;
		const bool _printNewline;
		const bool _buffering;
		bool _isTerminalSmart;
		utils::string _logBuffer;

	private:
		void append_log_level_prefix(const log_level msgLevel, utils::string& message) const {
			switch (msgLevel) {
			case log_level::Debug:
				message += (((_isTerminalSmart) ? GREEN : "") + "[Debug] ");
				break;
			case log_level::Info:
				message += (((_isTerminalSmart) ? BLUE : "") + "[Info] ");
				break;
			case log_level::Warning:
				message += (((_isTerminalSmart) ? YELLOW : "") + "[Warning] ");
				break;
			case log_level::Error:
				message += (((_isTerminalSmart) ? RED : "") + "[Error] ");
				break;
			case log_level::Critical:
				message += (((_isTerminalSmart) ? MAGENTA : "") + "[Critical] ");
				break;
			}
		}


		static void format_message(const string& format, const vector<string>& argStrings, utils::string& message) {
			bool braceOpen = false;
			string innerBuffer;
			size_t argIndex = 0;
			for (char c : format) {
				if (braceOpen) {
					if (c == '}') {
						size_t index;
						if (!innerBuffer.empty()) {
							index = utils::stoi(innerBuffer.c_str());
						}
						else {
							index = argIndex++;
						}
						innerBuffer.clear();
						if (index >= argStrings.size()) {
						}
						message += argStrings[index];
						braceOpen = false;
					} else if (utils::is_digit(c)) {
						innerBuffer += c;
					} else {
						UTILS_DEBUG_ASSERT(false);
					}
				} else {
					if (c == '{') {
						braceOpen = true;
					} else {
						message += c;
					}
				}
			}
		}

		static bool is_terminal_smart() {
#ifdef _WIN32
			const DWORD WINDOWS_10_VERSION = 10;
			DWORD type;
			if (GetProductInfo(WINDOWS_10_VERSION, 0, 0, 0, &type)) {
				return true; // Windows 10 or later
			}
			return true;
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


	/*class file_logger : public logger {
	public:
		file_logger(const std::string& filename, log_level level = log_level::Info, const bool printNewline = true, const bool buffering = false)
			: logger(level, printNewline, buffering), _logFile(filename, std::ofstream::out | std::ofstream::app) {
			if (!_logFile.is_open()) {
				UTILS_DEBUG_ASSERT(false);
				//throw std::runtime_error("Could not open log file");
			}
		}

		~file_logger() {
			if (_logFile.is_open()) {
				_logFile.close();
			}
		}

		void output(const std::string& message) override {
			if (!_logFile.is_open()) {
				UTILS_DEBUG_ASSERT(false);
				//throw std::runtime_error("Log file is not open");
			}
			_logFile << message.c_str();
		}

	private:
		std::ofstream _logFile;
	};*/
}