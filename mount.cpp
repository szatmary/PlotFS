#define FUSE_USE_VERSION 31

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <map>
#include <mutex>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "plotfs.hpp"

static struct options {
    const char* config_path;
} options;

#define OPTION(t, p)                      \
    {                                     \
        t, offsetof(struct options, p), 1 \
    }
static const struct fuse_opt option_spec[] = {
    OPTION("--c=%s", config_path),
    OPTION("--config=%s", config_path),
    FUSE_OPT_END
};

auto loadGeometry(bool force)
{
    static std::mutex m;
    static std::shared_ptr<const struct PlotFS::GeometryRO> geometry;
    std::lock_guard<std::mutex> lock(m);
    if (!geometry || force) {
        geometry = PlotFS::loadGeometry(options.config_path);
    }
    return geometry;
}

std::vector<uint8_t> path_to_plot_id(const std::string& path)
{
    int k = 0;
    std::vector<uint8_t> id(32);
    if (33 != std::sscanf(path.c_str(), "/plot-k%d-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx", //
            &k, &id[0], &id[1], &id[2], &id[3], &id[4], &id[5], &id[6], &id[7], &id[8], &id[9], &id[10], &id[11], &id[12], &id[13], &id[14], &id[15], //
            &id[16], &id[17], &id[18], &id[19], &id[20], &id[21], &id[22], &id[23], &id[24], &id[25], &id[26], &id[27], &id[28], &id[29], &id[30], &id[31])) {
        return std::vector<uint8_t>();
    }
    return id;
}

std::string plot_filename(const Plot& plot)
{
    return std::string("plot-k") + std::to_string(plot.k()) + "-" + to_string(*plot.id()) + ((plot.flags() & PlotFlags_Reserved) ? std::string(".tmp") : std::string(".plot"));
}

static void* init(struct fuse_conn_info* conn, struct fuse_config* cfg)
{
    (void)conn;
    cfg->kernel_cache = 0;
    cfg->direct_io = 0;
    return nullptr;
}

struct shard_data {
    uint64_t begin;
    uint64_t end;
    std::string dev_path;
};
// TODO dont unpack the geometry to get shard_data
static const std::vector<shard_data> get_plot_data(const std::vector<uint8_t>& plot_id)
{
    auto g = loadGeometry(false);
    if (!g || !g->geom || !g->geom->plots() || !g->geom->devices()) {
        return std::vector<shard_data>();
    }

    auto devpath = [&](const flatbuffers::Vector<uint8_t>& device_id) {
        auto key = std::vector<uint8_t>(device_id.begin(), device_id.end());
        for (const auto& device : *g->geom->devices()) {
            if (*device->id() == key) {
                return device->path()->str();
            }
        }
        return std::string();
    };

    std::vector<shard_data> shards;
    for (const auto plot : *g->geom->plots()) {
        if (*plot->id() == plot_id) {
            if (!plot->shards()) {
                continue;
            }
            for (const auto shard : *plot->shards()) {
                shards.emplace_back(shard_data { shard->begin() + recovery_point_size, shard->end(), devpath(*shard->device_id()) });
            }
            return shards;
        }
    }
    return std::vector<shard_data>();
}

static int getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi)
{
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    } else {
        auto plot_id = path_to_plot_id(path);
        if (plot_id.empty()) {
            return -ENOENT;
        }
        auto shard_data = get_plot_data(plot_id);
        if (shard_data.empty()) {
            return -EIO;
        }

        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 0;
        for (const auto& shard : shard_data) {
            stbuf->st_size += shard.end - shard.begin;
        }
        return 0;
    }

    return -ENOENT;
}

static int readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;

    if (strcmp(path, "/") != 0) {
        return -ENOENT;
    }

    auto g = loadGeometry(true);
    if (!g) {
        return -EIO;
    }

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));

    if (g->geom->plots()) {
        for (const auto plot : *g->geom->plots()) {
            std::string filename = plot_filename(*plot);
            if (filename.empty()) {
                continue;
            }
            filler(buf, filename.c_str(), NULL, 0, static_cast<fuse_fill_dir_flags>(0));
        }
    }

    return 0;
}

static int open(const char* path, struct fuse_file_info* fi)
{
    auto plot_id = path_to_plot_id(path);
    if (plot_id.empty()) {
        return -ENOENT;
    }
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }
    auto data = get_plot_data(plot_id);
    auto pd_ptr = new std::vector<shard_data>();
    pd_ptr->swap(data);
    fi->fh = reinterpret_cast<uint64_t>(pd_ptr);
    return 0;
}

static int release(const char*, struct fuse_file_info* fi)
{
    auto pd_ptr = reinterpret_cast<std::vector<shard_data>*>(fi->fh);
    if (pd_ptr) {
        delete pd_ptr;
    }
    return 0;
}

static int read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    auto pd_ptr = reinterpret_cast<std::vector<shard_data>*>(fi->fh);
    if (!pd_ptr) {
        return -EIO;
    }

    auto tsize = size;
    for (auto& shard : *pd_ptr) {
        if (size == 0) {
            break;
        }
        // offset is after this shard ends
        auto shard_size = shard.end - shard.begin;
        if (offset >= shard_size) {
            offset -= shard_size;
            continue;
        }

        // offset is in this shard
        auto device = FileHandle::open(shard.dev_path);
        if (!device) {
            std::cerr << "failed to open " << shard.dev_path << std::endl;
            return -EIO;
        }

        // Seek to the right place
        if (!device->seek(shard.begin + offset)) {
            std::cerr << "seek failed" << std::endl;
            return -EIO;
        }

        // Read the data
        auto read = std::min(size, shard_size - offset);
        read = device->read(reinterpret_cast<uint8_t*>(buf), read);
        buf += read, size -= read, offset = 0;
    }

    return tsize - size;
}

static int statfs(const char*, struct statvfs* stat)
{
    auto g = loadGeometry(false);
    if (!g) {
        return -EIO;
    }

    stat->f_bsize = 1; /* file system block size */
    stat->f_frsize = 1; /* fragment size */
    stat->f_blocks = 0; /* size of fs in f_frsize units */
    stat->f_bfree = 0; /* # free blocks */
    stat->f_bavail = 0; /* # free blocks for unprivileged users */
    stat->f_files = 0; /* # inodes */
    stat->f_ffree = 0x1fffffff; /* # free inodes */
    stat->f_favail = 0x1fffffff; /* # free inodes for unprivileged users */
    stat->f_fsid = 0; /* file system ID */
    stat->f_flag = 0; /* mount flags */
    stat->f_namemax = 255; /* maximum filename length */

    if (g->geom->devices()) {
        for (const auto device : *g->geom->devices()) {
            stat->f_blocks += device->end() - device->begin();
        }
    }

    if (g->geom->plots()) {
        stat->f_bfree = stat->f_blocks;
        for (const auto plot : *g->geom->plots()) {
            for (const auto shard : *plot->shards()) {
                stat->f_bfree -= shard->end() - shard->begin();
            }
        }

        stat->f_files = g->geom->plots()->size();
        stat->f_bavail = stat->f_bfree;
    }

    return 0;
}

static const struct fuse_operations oper = {
    .getattr = getattr,
    .open = open,
    .read = read,
    .statfs = statfs,
    .release = release,
    .readdir = readdir,
    .init = init,
};

int main(int argc, char* argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
        return EXIT_FAILURE;
    }

    if (0 == options.config_path || 0 == strlen(options.config_path)) {
        options.config_path = default_config_path.c_str();
    }

    fuse_opt_add_arg(&args, "-oallow_other");
    auto ret = fuse_main(args.argc, args.argv, &oper, NULL);
    fuse_opt_free_args(&args);
    return ret;
}
