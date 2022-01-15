#pragma once

#include <iomanip>
#include <sstream>
#include <vector>

class PlotFile : public FileHandle {
private:
    uint8_t k_;
    std::vector<uint8_t> id_;

public:
    PlotFile(int fd, uint8_t k, const std::vector<uint8_t> id)
        : FileHandle(fd)
        , k_(k)
        , id_(id) {};

    const uint8_t k() const { return k_; }
    const std::vector<uint8_t>& id() const { return id_; }
    static std::shared_ptr<PlotFile> open(const std::string& path)
    {
        uint8_t k = 0;
        std::string signature;
        std::vector<uint8_t> id(32);
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return nullptr;
        }

        signature.resize(19);
        if (::read(fd, signature.data(), signature.size()) != signature.size()) {
            ::close(fd);
            return nullptr;
        }
        if (::read(fd, id.data(), id.size()) != id.size()) {
            ::close(fd);
            return nullptr;
        }
        if (::read(fd, &k, 1) != 1) {
            ::close(fd);
            return nullptr;
        }
        if (0 != ::lseek64(fd, 0, SEEK_SET)) {
            ::close(fd);
            return nullptr;
        }
        return std::make_shared<PlotFile>(fd, k, id);
    }
};