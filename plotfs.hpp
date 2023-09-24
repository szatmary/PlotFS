#pragma once

// local headers
#include "device.hpp"
#include "file.hpp"
#include "plot.hpp"

#include "plotfs_generated.h"

#include <pwd.h>
#include <sys/sendfile.h>

#include <random>

static const auto default_config_path = std::string("/var/local/plotfs/plotfs.bin");

static bool operator==(const flatbuffers::Vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

static bool operator==(const flatbuffers::Vector<uint8_t>& a, const flatbuffers::Vector<uint8_t>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

std::string to_string(const std::vector<uint8_t>& data)
{
    std::stringstream ss;
    ss << std::hex;
    for (auto c : data) {
        ss << std::setw(2) << std::setfill('0') << (int)c;
    }
    return ss.str();
}

std::string to_string(const flatbuffers::Vector<uint8_t>& data)
{
    std::stringstream ss;
    ss << std::hex;
    for (auto c : data) {
        ss << std::setw(2) << std::setfill('0') << (int)c;
    }
    return ss.str();
}

const static int recovery_point_size = 64; // DONT MODIFY THIS VALUE
std::array<uint8_t, recovery_point_size> get_recovery_point(uint64_t size, const std::vector<uint8_t>& next_device_id = std::vector<uint8_t>(), uint64_t next_device_offset = 0)
{
    std::vector<uint8_t> header;
    static const auto text = std::string("PlotFS Recovery Point");
    header.insert(header.end(), text.begin(), text.end());
    header.push_back(0);
    header.push_back(recovery_point_size);
    header.insert(header.end(), { static_cast<uint8_t>(size >> 56), static_cast<uint8_t>(size >> 48), static_cast<uint8_t>(size >> 40), static_cast<uint8_t>(size >> 32), static_cast<uint8_t>(size >> 24), static_cast<uint8_t>(size >> 16), static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size) });
    if (next_device_id.size() == 32) {
        header.insert(header.end(), next_device_id.begin(), next_device_id.end());
    } else {
        header.insert(header.end(), 32, 0);
    }
    header.insert(header.end(), { static_cast<uint8_t>(next_device_offset >> 56), static_cast<uint8_t>(next_device_offset >> 48), static_cast<uint8_t>(next_device_offset >> 40), static_cast<uint8_t>(next_device_offset >> 32), static_cast<uint8_t>(next_device_offset >> 24), static_cast<uint8_t>(next_device_offset >> 16), static_cast<uint8_t>(next_device_offset >> 8), static_cast<uint8_t>(next_device_offset) });

    std::array<uint8_t, recovery_point_size> array;
    std::copy(header.begin(), header.end(), array.begin());
    return array;
}

class PlotFS {
private:
    GeometryT geom;
    std::shared_ptr<FileHandle> fd;

    bool save()
    {
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(Geometry::Pack(fbb, &geom));
        if (!fd->seek(0)) {
            return false;
        }
        if (!fd->truncate()) {
            return false;
        }

        auto data = fbb.GetBufferPointer();
        auto size = fbb.GetSize();
        if (!fd->write(data, size)) {
            return false;
        }
        fd->sync();
        return true;
    }
    
public:
    struct GeometryRO {
        std::vector<uint8_t> buffer;
        const Geometry* geom = nullptr;
    };

    static std::shared_ptr<const struct GeometryRO> loadGeometry(std::shared_ptr<FileHandle>& fd)
    {
        if (!fd->seek(0)) {
            return nullptr;
        }

        std::vector<uint8_t> buffer(fd->size());
        if (!fd->read(buffer.data(), buffer.size())) {
            std::cerr << "Failed to read geometry" << std::endl;
            return nullptr;
        }
        if (buffer.empty()) {
            std::cerr << "Geometry file is empty" << std::endl;
            return nullptr;
        }
        auto verifier = flatbuffers::Verifier(buffer.data(), buffer.size());
        if (!VerifyGeometryBuffer(verifier)) {
            std::cerr << "Failed to verify geometry" << std::endl;
            return nullptr;
        }
        auto geom = GetGeometry(buffer.data());
        return std::make_shared<const GeometryRO>(GeometryRO { std::move(buffer), geom });
    }

    static std::shared_ptr<const struct GeometryRO> loadGeometry(const std::string& path)
    {
        auto fd = FileHandle::open(path, O_RDONLY);
        if (!fd) {
            std::cerr << "Failed to open " << path << ": " << strerror(errno) << std::endl;
            return nullptr;
        }
        if (!fd->lock(LOCK_SH)) {
            std::cerr << "Failed to lock " << path << ": " << strerror(errno) << std::endl;
            return nullptr;
        }

        auto g = loadGeometry(fd);
        return g;
    }

    static bool init(const std::string& path, bool force)
    {
        auto fd = FileHandle::open(path, O_RDWR | O_CREAT, 0644);
        if (!fd) {
            return false;
        }
        if (!fd->lock(LOCK_EX)) {
            std::cerr << "Failed exclusive lock on file " << path << std::endl;
            return false;
        }
        if (fd->stat().st_size == 0 || force) {
            GeometryT geom;
            flatbuffers::FlatBufferBuilder fbb;
            fbb.Finish(Geometry::Pack(fbb, &geom));
            auto data = fbb.GetBufferPointer();
            auto size = fbb.GetSize();
            if (!fd->write(data, size)) {
                std::cerr << "Failed writing to geometry file" << std::endl;
                return false;
            }
            fd->sync();
        } else {
            std::cerr << "Geometry file is not empty." << std::endl;
            return false;
        }
        return true;
    }

    PlotFS(const std::string& path)
    {
        fd = FileHandle::open(path, O_RDWR, 0644);
        if (!fd) {
            return;
        }
        if (!fd->lock(LOCK_EX)) {
            fd->close();
            return;
        }
        auto g = loadGeometry(fd);
        if (!g) {
            return;
        }

        g->geom->UnPackTo(&geom);
    }

    bool isOpen() const { return !!fd; }
    bool removePlot(const std::vector<uint8_t>& plot_id)
    {
        auto plot_it = std::find_if(geom.plots.begin(), geom.plots.end(), [&](const auto& p) {
            return p->id == plot_id;
        });
        if (plot_it == geom.plots.end()) {
            std::cerr << "warning: plot not found" << std::endl;
            return false;
        }
        geom.plots.erase(plot_it);
        return save();
    }

    bool setPlotFlags(const std::vector<uint8_t>& plot_id, uint64_t flags, bool clear = false)
    {
        auto plot_it = std::find_if(geom.plots.begin(), geom.plots.end(), [&](const auto& p) {
            return !!p && p->id == plot_id;
        });
        if (plot_it == geom.plots.end()) {
            std::cerr << "warning: plot not found" << std::endl;
            return false;
        }
        if (clear) {
            (*plot_it)->flags = static_cast<PlotFlags>((*plot_it)->flags & ~flags);
        } else {
            (*plot_it)->flags = static_cast<PlotFlags>((*plot_it)->flags | flags);
        }
        return save();
    }

    bool clearPlotFlags(const std::vector<uint8_t>& plot_id, uint64_t flags)
    {
        return setPlotFlags(plot_id, flags, true);
    }

    bool addDevice(const std::string& dev_path, bool force)
    {
        auto existing = std::find_if(geom.devices.begin(), geom.devices.end(), [&](const auto& d) {
            if (d->path == dev_path) {
                return true;
            }
            return false;
        });
        if(existing != geom.devices.end()) {
            std::cerr << "A device (" << to_string((*existing)->id) << ") is already registered path " << dev_path << "." <<  std::endl;
            std::cerr << "Remove the existing device if you want to add a different one at the same path. " <<  std::endl;
            return false;
        }
        auto device = DeviceHandle::open(dev_path, true);
        if (device) {
            if (!force) {
                std::cerr << "This looks like a PlotFS partition. Use --force if you want to reset and add it." << std::endl;
                return false;
            }
            device.reset();
        }

        // format partition
        device = DeviceHandle::format(dev_path);
        if (!device) {
            std::cerr << "Failed to format device" << std::endl;
            return false;
        }
        {
            auto d = std::make_unique<DeviceT>();
            d->path = dev_path;
            d->id = device->id();
            d->begin = device->begin();
            d->end = device->end();
            geom.devices.emplace_back(std::move(d));
        }
        return save();
    }

    bool removeDevice(const std::vector<uint8_t>& dev_id)
    {
        geom.devices.erase(std::remove_if(geom.devices.begin(), geom.devices.end(), [&](const auto& d){
            return d->id == dev_id;
        }), geom.devices.end());
        return save();
    }

    bool fixDevice(const std::vector<uint8_t>& dev_id)
    {
        auto device = std::find_if(geom.devices.begin(), geom.devices.end(), [&](const auto& d){
            return d->id == dev_id;
        });
        if(device == std::end(geom.devices)) {
            std::cerr << "device not found: " << to_string(dev_id) << std::endl;
            return false;
        }
        std::cerr << "Fixing signature of " << to_string(dev_id) << " at " << device->get()->path << std::endl;
        return DeviceHandle::format(device->get()->path, dev_id) != nullptr;
    }

    bool addPlot(const std::string& plot_path)
    {
        if (geom.devices.empty()) {
            std::cerr << "No devices registered" << std::endl;
            return false;
        }
        auto plot_file = PlotFile::open(plot_path);
        if (!plot_file) {
            std::cerr << "failed to open plot: " << plot_path << std::endl;
            return false;
        }

        auto plot_stat = plot_file->stat();
        if (plot_stat.st_size == 0) {
            std::cerr << "plot file is empty" << std::endl;
            return false;
        }

        for (const auto& plot : geom.plots) {
            if (plot->id == plot_file->id()) {
                std::cerr << "plot already exists" << std::endl;
                return false;
            }
        }

        class DeviceInitializer {
            private:
                DeviceT device;
                std::shared_ptr<DeviceHandle> handle;
            public:
                DeviceInitializer(DeviceT d) : device(d) {}
                
                std::string path() { return device.path; }
                const std::vector<uint8_t>& id() const { return device.id; }

                std::shared_ptr<DeviceHandle> loadHandle() {
                    if(!handle) {
                        std::cout << "Loadind device handle for " << path() << "...";
                        std::shared_ptr<DeviceHandle> dh = DeviceHandle::open(device.path, true, O_RDWR);
                        if (!dh) {
                            std::cout << std::endl;
                            std::cerr << "ERROR: Failed to open device: " << to_string(device.id) << " at " << device.path << std::endl;
                        } else if(dh->id() != device.id) {
                            std::cout << std::endl;
                            std::cerr << "ERROR: Wrong device id for " << device.path << " expected " << to_string(device.id) << " but was " << to_string(dh->id()) << std::endl;
                        } else {
                            std::cout << " Done." << std::endl;
                            handle = dh;
                        }
                    }

                    return handle;
                }
                
                bool isAccessible() {
                    return !!loadHandle();
                }
        };

        struct free_shard {
            uint64_t begin;
            uint64_t end;
            std::shared_ptr<uint64_t> device_free;
            std::shared_ptr<DeviceInitializer> device;
        };

        std::vector<free_shard> freespace;
        for (const auto& device : geom.devices) {
            freespace.push_back(
                free_shard { device->begin, device->end, std::make_shared<uint64_t>(device->end - device->end), std::make_shared<DeviceInitializer>(DeviceInitializer(*device))});
        }

        // Caclulate the free space runs in the pool by assuming every device is empty
        // then subtracting the used space from the free space resulting in fragmented runs
        for (const auto& plot : geom.plots) {
            for (const auto& shard : plot->shards) {
                // The filesystem minimizes fragmentation, so the freepace vector should be small
                auto freespace_iter = std::find_if(freespace.begin(), freespace.end(), [&](const auto& freespace) {
                    return shard->device_id == freespace.device->id()
                        && shard->begin < freespace.end
                        && shard->end > freespace.begin;
                });
                if (freespace_iter == freespace.end()) {
                    // this should not happen
                    std::cerr << "warning: plot block not found in freespace" << std::endl;
                    continue;
                }
                auto freeblock = *freespace_iter;
                freespace.erase(freespace_iter);

                // keep track of the free space so we can sort by it later
                *freeblock.device_free -= (shard->end - shard->begin);
                if (shard->end < freeblock.end) {
                    // shard:         |----|
                    // freeblock:     |-----------|
                    // new freeblock:      |------|
                    freespace.push_back(free_shard { shard->end, freeblock.end, freeblock.device_free, freeblock.device });
                }
                if (shard->begin > freeblock.begin) {
                    // shard:                |----|
                    // freeblock:     |-----------|
                    // new freeblock: |------|
                    freespace.push_back(free_shard { freeblock.begin, shard->begin, freeblock.device_free, freeblock.device });
                }
            }
        }

        // Sort the freeruns decending by device free space, then run length
        std::sort(freespace.begin(), freespace.end(), [](const auto& a, const auto& b) {
            if (*a.device_free != *b.device_free) {
                return *a.device_free > *b.device_free;
            }
            return (a.end - a.begin) > (b.end - b.begin);
        });

        // fragment (for testing purposes)
        // if (false) {
        //     std::random_device rd;
        //     std::mt19937 mt(rd());
        //     std::uniform_int_distribution<uint64_t> dist(1, 1'000'000'000);
        //     std::vector<free_shard> fragmented;
        //     for (auto& shard : freespace) {
        //         while (shard.begin < shard.end) {
        //             auto shard_size = std::min((shard.end - shard.begin), dist(mt));
        //             auto shard_end = shard.begin + shard_size;
        //             fragmented.emplace_back(free_shard { shard.begin, shard_end, shard.device });
        //             shard.begin = shard_end;
        //         }
        //     }
        //     std::shuffle(fragmented.begin(), fragmented.end(), mt);
        //     freespace = fragmented;
        // }

        
        auto reserveSpace = [](std::vector<free_shard> freespace, std::vector<free_shard>* reserved_space, int plot_size) {
            
            auto space_needed = static_cast<uint64_t>(plot_size);
            for (const auto& shard : freespace) {
                if (space_needed == 0) {
                    break;
                }
                auto reserved_size = std::min(space_needed + recovery_point_size, shard.end - shard.begin);
                if (reserved_size <= recovery_point_size) {
                    continue;
                }
                //Quick check without loading devices
                if(reserved_space && !shard.device->isAccessible()) {
                    continue;
                }
                //Do actual reservation
                if(reserved_space) {
                    reserved_space->push_back({ shard.begin, shard.begin + reserved_size, 0, shard.device });
                }
                space_needed -= (reserved_size - recovery_point_size);
            }
            return space_needed;
        };
        
        if(reserveSpace(freespace, nullptr, plot_stat.st_size) > 0) {
            std::cerr << "not enough free space to fit plot" << std::endl;
            return false;   
        }

        std::vector<free_shard> reserved_space;
        if(reserveSpace(freespace, &reserved_space, plot_stat.st_size) > 0) {
            std::cerr << "not enough free space to fit plot" << std::endl;
            return false;   
        }

        // we are done with the freespace vector, clear it so unused file handles will be closed
        freespace.clear();

        // reserve the space in the device
        std::vector<std::unique_ptr<ShardT>> shards;
        for (const auto& reserved : reserved_space) {
            auto s = std::make_unique<ShardT>();
            s->device_id = reserved.device->id();
            s->begin = reserved.begin;
            s->end = reserved.end;
            shards.emplace_back(std::move(s));
        }

        { // scope newPlot we do dont accidentally use it after it is moved
            auto newPlot = std::make_unique<PlotT>();
            newPlot->k = plot_file->k();
            newPlot->id = plot_file->id();
            newPlot->flags = PlotFlags_Reserved;
            newPlot->shards = std::move(shards);
            geom.plots.emplace_back(std::move(newPlot));
        }
        if (!save()) {
            return false;
        }
        if (!fd->lock(LOCK_UN)) {
            removePlot(plot_file->id());
            return false;
        }

        // copy the plot to the reserved space
        std::cerr << "starting plot copy to " << reserved_space.size() << " shard(s)" << std::endl;
        off64_t off_in = 0; // position of input file
        while (!reserved_space.empty()) {
            auto device = reserved_space.front().device->loadHandle();
            auto device_offset = reserved_space.front().begin;
            auto shard_size = reserved_space.front().end - reserved_space.front().begin;
            reserved_space.erase(reserved_space.begin());
            auto next_device_id = reserved_space.empty() ? std::vector<uint8_t>() : reserved_space.front().device->id();
            auto next_shard_offset = reserved_space.empty() ? 0 : reserved_space.front().begin;
            auto recovery_point = get_recovery_point(shard_size - recovery_point_size, next_device_id, next_shard_offset);
            if (!device->seek(device_offset)) {
                removePlot(plot_file->id());
                return false;
            }
            std::cerr << "writing recovery point header" << std::endl;
            if (device->write(recovery_point.data(), recovery_point.size()) != recovery_point.size()) {
                std::cerr << "error writing recovery header" << std::endl;
                removePlot(plot_file->id());
                return false;
            }

            // fill the rest of the shard
            shard_size -= recovery_point.size();
            while (shard_size > 0) {
                // split up the writes a little bit
                uint64_t bytes_to_write = std::min(shard_size, static_cast<uint64_t>(1024 * 1024 * 1024));
                std::cerr << int(100 * off_in / plot_stat.st_size) << "% writing up to " << bytes_to_write << " bytes from offset " << off_in << " to device " << to_string(device->id()) << std::endl;
                auto bytes_written = sendfile64(device->fd(), plot_file->fd(), &off_in, bytes_to_write);
                if (bytes_written < 0) {
                    removePlot(plot_file->id());
                    std::cerr << "failed to copy plot to device " << errno << std::endl;
                    return false;
                }
                shard_size -= bytes_written;
            }
            std::cerr << int(100 * off_in / plot_stat.st_size) << "% finished writing to device " << to_string(device->id()) << std::endl;
        }

        // Finished writing, clear the reserved flag
        if (!fd->lock(LOCK_EX)) {
            removePlot(plot_file->id());
        }
        auto g = loadGeometry(fd);
        if (!g) {
            return false;
        }
        g->geom->UnPackTo(&geom);
        return clearPlotFlags(plot_file->id(), PlotFlags_Reserved);
    }
};
