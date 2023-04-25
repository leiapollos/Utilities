//
//  FileIO.hpp
//  Utilities
//
//  Created by André Leite on 25/04/2023.
//

#pragma once

#include <string>
#include <vector>

class FileIOImpl;

class FileIO {
public:
    enum class WriteMode {
        Append,
        Truncate
    };

    static bool readAsString(const std::string& path, std::string& content);
    static bool readAsBinary(const std::string& path, std::vector<uint8_t>& content);
    static bool write(const std::string& path, const std::string& content, WriteMode mode = WriteMode::Truncate);
    static bool copy(const std::string& src, const std::string& dest);
    static bool move(const std::string& src, const std::string& dest);

private:
    FileIO() = delete;
    static FileIOImpl _impl;
};
