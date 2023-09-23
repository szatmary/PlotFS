#pragma once

#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/fs.h>
#include <memory>
#include <string>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

class FileHandle {
private:
    int fd_ = -1;

public:
    FileHandle(int fd)
        : fd_(fd) {};
    FileHandle(const FileHandle&) = delete;
    static std::shared_ptr<FileHandle> open(const std::string& path, int flags = O_RDONLY, mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
    {
        auto fd = ::open(path.c_str(), flags, mode);
        if (fd < 0) {
            std::cerr << "ERROR: Failed to open " << path << " " << strerror(errno) << std::endl;
            return nullptr;
        }
        return std::make_shared<FileHandle>(fd);
    }
    virtual ~FileHandle() { close(); }
    void close()
    {
        if (fd_ > 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool is_open() const { return fd_ >= 0; }
    struct stat stat() const
    {
        struct stat st;
        std::memset(&st, 0, sizeof(st));
        if (0 != ::fstat(fd_, &st)) {
            std::cerr << "fstat error" << std::endl;
        }
        if (S_ISBLK(st.st_mode)) {
            uint64_t size;
            if (0 > ioctl(fd_, BLKGETSIZE64, &size)) {
                std::cerr << "ioctl error" << std::endl;
            }
            st.st_blksize = 512;
            st.st_blocks = size / st.st_blksize;
            st.st_size = st.st_blksize * st.st_blocks;
        }

        return st;
    }
    uint64_t size() const { return stat().st_size; }
    bool seek(off64_t offset) { return offset == ::lseek64(fd_, offset, SEEK_SET); }
    bool truncate(off64_t offset = 0) { return 0 == ::ftruncate(fd_, offset); }
    bool lock(int operation) { return 0 == ::flock(fd_, operation); }
    bool sync() { return 0 == ::syncfs(fd_); }

    int read(uint8_t* data, size_t size)
    {
        auto tsize = size;
        while (size) {
            auto rsize = ::read(fd_, data, size);
            if (rsize == 0) {
                return tsize - size;
            } else if (rsize < 0) {
                return -1;
            }
            size -= rsize, data += rsize;
        }
        return tsize;
    }

    int write(const uint8_t* data, size_t size)
    {
        auto tsize = size;
        while (size) {
            auto wsize = ::write(fd_, data, size);
            if (wsize < 0) {
                return tsize > size ? tsize - size : -1;
            }
            size -= wsize, data += wsize;
        }
        return tsize;
    }
    int write(const std::string& buffer) { return ::write(fd_, buffer.c_str(), buffer.size()); }
    int fd() { return fd_; }
    int release()
    {
        auto fd = fd_;
        fd_ = -1;
        return fd;
    }
};