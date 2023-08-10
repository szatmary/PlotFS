#pragma once

#include "file.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <random>

static const std::string DeviceSignature = "PlotFS\nby: Matthew Szatmary <matt@szatmary.org> (@m3u8)\nDonate Chia to: xch1hsyyclxn2v59ysd4n8nk577sduw64sg90nr8z26c3h8emq7magdqqzq9n5\n";

// Abstract device interface
class IDevice {
public:
    virtual ~IDevice() = default;
    virtual uint64_t begin() const = 0;
    virtual uint64_t end() const = 0;
    virtual const std::vector<uint8_t>& id() const = 0;
};

class DeviceHandle : public FileHandle, public IDevice {
private:
    uint64_t begin_ = 0;
    uint64_t end_ = 0;
    std::vector<uint8_t> id_;

public:
    uint64_t begin() const override { return begin_; }
    uint64_t end() const override { return end_; }
    const std::vector<uint8_t>& id() const override { return id_; }

    DeviceHandle(int fd, uint64_t begin, uint64_t end, const std::vector<uint8_t>& id)
        : FileHandle(fd)
        , begin_(begin)
        , end_(end)
        , id_(id.begin(), id.end()) {};

    static std::shared_ptr<DeviceHandle> format(const std::string& path)
    {
        // generate 32 byte random id
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<uint64_t> dist(0, 255);
        auto id = std::vector<uint8_t>(32);
        for (auto& i : id) {
            i = dist(mt);
        }
        return format(path, id);
    }

    static std::shared_ptr<DeviceHandle> format(const std::string& path, const std::vector<uint8_t>& dev_id)
    {
        auto fd = FileHandle::open(path, O_RDWR);
        if (!fd) {
            std::cerr << "Failed to open device: " << path << std::endl;
            return nullptr;
        }
        auto stat = fd->stat();
        if (stat.st_size == 0) {
            std::cerr << "Device is empty, cannot format" << std::endl;
            return nullptr;
        }

        static std::array<uint8_t, 512 * 2> first_block {}; // wipe gpt header
        std::copy(DeviceSignature.begin(), DeviceSignature.end(), first_block.begin());
        std::copy(dev_id.begin(), dev_id.end(), first_block.begin() + 256);

        auto begin = static_cast<uint64_t>(512 * 2);
        first_block[256 + 32] = static_cast<uint8_t>(begin >> 56), first_block[256 + 32 + 1] = static_cast<uint8_t>(begin >> 48), first_block[256 + 32 + 2] = static_cast<uint8_t>(begin >> 40), first_block[256 + 32 + 3] = static_cast<uint8_t>(begin >> 32);
        first_block[256 + 32 + 4] = static_cast<uint8_t>(begin >> 24), first_block[256 + 32 + 5] = static_cast<uint8_t>(begin >> 16), first_block[256 + 32 + 6] = static_cast<uint8_t>(begin >> 8), first_block[256 + 32 + 7] = static_cast<uint8_t>(begin);

        auto end = static_cast<uint64_t>(stat.st_size);
        first_block[256 + 32 + 8] = static_cast<uint8_t>(end >> 56), first_block[256 + 32 + 9] = static_cast<uint8_t>(end >> 48), first_block[256 + 32 + 10] = static_cast<uint8_t>(end >> 40), first_block[256 + 32 + 11] = static_cast<uint8_t>(end >> 32);
        first_block[256 + 32 + 12] = static_cast<uint8_t>(end >> 24), first_block[256 + 32 + 13] = static_cast<uint8_t>(end >> 16), first_block[256 + 32 + 14] = static_cast<uint8_t>(end >> 8), first_block[256 + 32 + 15] = static_cast<uint8_t>(end);
        if (!fd->write(first_block.data(), first_block.size()) && fd->sync()) {
            return nullptr;
        }
        if (!fd->sync()) {
            return nullptr;
        }
        return std::make_shared<DeviceHandle>(fd->release(), begin, end, dev_id);
    }

    static std::shared_ptr<DeviceHandle> open(const std::string& path, bool require_signature = false, int mode = O_RDONLY)
    {
        auto fd = FileHandle::open(path, mode);
        static std::array<uint8_t, 512> first_block;
        if (first_block.size() != fd->read(first_block.data(), first_block.size())) {
            std::cerr << "Error: Failed to read first block of " << path << std::endl;
            return nullptr;
        }

        // check signature
        if (DeviceSignature != std::string(first_block.begin(), first_block.begin() + DeviceSignature.size())) {
            if(require_signature) {
                std::cerr << "Error: Missing device signature for " << path << std::endl;
            }
            return nullptr;
        }

        std::vector<uint8_t> id(32);
        std::copy(first_block.begin() + 256, first_block.begin() + 256 + 32, id.begin());
        auto begin = static_cast<uint64_t>(first_block[256 + 32]) << 56 | static_cast<uint64_t>(first_block[256 + 32 + 1]) << 48 | static_cast<uint64_t>(first_block[256 + 32 + 2]) << 40 | static_cast<uint64_t>(first_block[256 + 32 + 3]) << 32
            | static_cast<uint64_t>(first_block[256 + 32 + 4]) << 24 | static_cast<uint64_t>(first_block[256 + 32 + 5]) << 16 | static_cast<uint64_t>(first_block[256 + 32 + 6]) << 8 | static_cast<uint64_t>(first_block[256 + 32 + 7]);
        auto end = static_cast<uint64_t>(first_block[256 + 32 + 8]) << 56 | static_cast<uint64_t>(first_block[256 + 32 + 9]) << 48 | static_cast<uint64_t>(first_block[256 + 32 + 10]) << 40 | static_cast<uint64_t>(first_block[256 + 32 + 11]) << 32
            | static_cast<uint64_t>(first_block[256 + 32 + 12]) << 24 | static_cast<uint64_t>(first_block[256 + 32 + 13]) << 16 | static_cast<uint64_t>(first_block[256 + 32 + 14]) << 8 | static_cast<uint64_t>(first_block[256 + 32 + 15]);
        if (begin > end) {
            std::cerr << "Error: Invalid device signature for " << path << "begin > end" << std::endl;
            return nullptr;
        }

        return std::make_shared<DeviceHandle>(fd->release(), begin, end, id);
    }
};

class DirectoryDevice : public IDevice {
private:
    uint64_t begin_ = 0;
    uint64_t end_ = 0;
    std::vector<uint8_t> id_;
    std::string path_; // Path to the directory

    // Helper method to calculate the total size of all files in the directory
    uint64_t calculateDirectorySize(const std::string& path) {
        uint64_t totalSize = 0;
        for (const auto & entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                totalSize += entry.file_size();
            }
        }
        return totalSize;
    }

public:
    DirectoryDevice(const std::string& path) : path_(path) {
        // Set begin to 0 for directories
        begin_ = 0;

        // Calculate the total size of the directory
        end_ = calculateDirectorySize(path);

        // Generate a random 32-byte ID for the directory
        std::random_device rd;
        std::mt19937 mt(rd());
        std::uniform_int_distribution<uint64_t> dist(0, 255);
        id_.resize(32);
        for (auto& i : id_) {
            i = dist(mt);
        }
    }

    uint64_t begin() const override { return begin_; }
    uint64_t end() const override { return end_; }
    const std::vector<uint8_t>& id() const override { return id_; }
};