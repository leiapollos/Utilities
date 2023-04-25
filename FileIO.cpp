//
//  FileIO.cpp
//  Utilities
//
//  Created by André Leite on 25/04/2023.
//

#include "FileIO.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <system_error>
#include <unordered_map>

class FileIOImpl {
public:
    std::unordered_map<std::string, std::mutex> fileMutexMap;
    std::mutex mapMutex;

    std::mutex& getMutexForPath(const std::string& path);
    bool readAsString(const std::string& path, std::string& content);
    bool readAsBinary(const std::string& path, std::vector<uint8_t>& content);    
    bool write(const std::string& path, const std::string& content, FileIO::WriteMode mode);
    bool copy(const std::string& src, const std::string& dest);
    bool move(const std::string& src, const std::string& dest);
};

std::mutex& FileIOImpl::getMutexForPath(const std::string& path) {
    std::lock_guard<std::mutex> mapLock(mapMutex);
    return fileMutexMap[path];
}

bool FileIOImpl::readAsString(const std::string& path, std::string& content) {
    std::lock_guard<std::mutex> lock(getMutexForPath(path));
    std::ifstream file(path, std::ios_base::in);
    if (!file.is_open()) {
        return false;
    }

    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return true;
}

bool FileIOImpl::readAsBinary(const std::string& path, std::vector<uint8_t>& content) {
    std::lock_guard<std::mutex> lock(getMutexForPath(path));
    std::ifstream file(path, std::ios_base::in | std::ios_base::binary);
    if (!file.is_open()) {
        return false;
    }

    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return true;
}

bool FileIOImpl::write(const std::string& path, const std::string& content, FileIO::WriteMode mode) {
    std::lock_guard<std::mutex> lock(getMutexForPath(path));

    std::ios_base::openmode openMode;
    switch (mode) {
        case FileIO::WriteMode::Append: {
            openMode = std::ios_base::app;
            break;
        }
        case FileIO::WriteMode::Truncate:
        default: {
            openMode = std::ios_base::trunc;
            break;
        }
    }

    std::ofstream file(path, openMode);
    if (!file.is_open()) {
        return false;
    }

    file << content;
    file.close();
    return true;
}

bool FileIOImpl::copy(const std::string& src, const std::string& dest) {
    if (src == dest || src.empty() || dest.empty()) return false;
    std::lock_guard<std::mutex> srcLock(getMutexForPath(src));
    std::lock_guard<std::mutex> destLock(getMutexForPath(dest));
    std::error_code ec;
    std::filesystem::copy(src, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return false;
    }
    return true;
}

bool FileIOImpl::move(const std::string& src, const std::string& dest) {
    if (src == dest || src.empty() || dest.empty()) return false;
    std::lock_guard<std::mutex> srcLock(getMutexForPath(src));
    std::lock_guard<std::mutex> destLock(getMutexForPath(dest));
    std::error_code ec;
    std::filesystem::rename(src, dest, ec);
    if (ec) {
        return false;
    }
    return true;
}

FileIOImpl FileIO::_impl;

bool FileIO::readAsString(const std::string& path, std::string& content) {
    return _impl.readAsString(path, content);
}

bool FileIO::readAsBinary(const std::string& path, std::vector<uint8_t>& content) {
    return _impl.readAsBinary(path, content);
}

bool FileIO::write(const std::string& path, const std::string& content, WriteMode mode) {
    return _impl.write(path, content, mode);
}

bool FileIO::copy(const std::string& src, const std::string& dest) {
    return _impl.copy(src, dest);
}

bool FileIO::move(const std::string& src, const std::string& dest) {
    return _impl.move(src, dest);
}