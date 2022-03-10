#include "plotfs.hpp"

#include "CLI11.hpp"

std::vector<uint8_t> to_vector(const std::string& id)
{
    std::vector<uint8_t> id_;
    for (auto i = 0; i < id.length(); i += 2) {
        std::string byteString = id.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        id_.push_back(byte);
    }
    return id_;
}

int main(int argc, char** argv)
{
    CLI::App app { "PlotFS" };

    std::string config_path = default_config_path;
    app.add_option("-c,--config", config_path, "Path to configuration json file");

    bool init = false;
    auto init_opt = app.add_flag("--init", init, "Initlize a new plotfs.bin file");

    std::vector<std::string> add_plot;
    std::string add_device, remove_device, remove_plot;
    auto add_device_opt = app.add_option("--add_device", add_device, "Add a device or partition");
    auto remove_device_opt = app.add_option("--remove_device", remove_device, "Rempove a device or partition");
    auto add_plot_opt = app.add_option("--add_plot", add_plot, "Add a plot");
    auto remove_plot_opt = app.add_option("--remove_plot", remove_plot, "Remove a plot");

    bool list_plots = false, list_devices = false;
    auto list_plots_opt = app.add_flag("--list_plots", list_plots, "List all plots");
    auto list_devices_opt = app.add_flag("--list_devices", list_devices, "List all devices");

    bool force = false, remove_source = false;
    bool force_opt = app.add_flag("--force", force, "Force operation");
    auto remove_source_opt = app.add_flag("--remove_source", remove_source, "Removes source plot file after adding");

    init_opt->excludes(add_device_opt)->excludes(remove_device_opt)->excludes(add_plot_opt)->excludes(remove_plot_opt)->excludes(list_plots_opt)->excludes(list_devices_opt);

    add_device_opt->excludes(remove_device_opt)->excludes(add_plot_opt)->excludes(remove_plot_opt)->excludes(list_plots_opt)->excludes(list_devices_opt)->excludes(init_opt);
    remove_device_opt->excludes(add_device_opt)->excludes(add_plot_opt)->excludes(remove_plot_opt)->excludes(list_plots_opt)->excludes(list_devices_opt)->excludes(init_opt);
    add_plot_opt->excludes(remove_plot_opt)->excludes(add_device_opt)->excludes(remove_device_opt)->excludes(list_plots_opt)->excludes(list_devices_opt)->excludes(init_opt);
    remove_plot_opt->excludes(add_plot_opt)->excludes(add_device_opt)->excludes(remove_device_opt)->excludes(list_plots_opt)->excludes(list_devices_opt)->excludes(init_opt);

    list_plots_opt->excludes(add_device_opt)->excludes(remove_device_opt)->excludes(add_plot_opt)->excludes(remove_plot_opt)->excludes(list_devices_opt)->excludes(init_opt); //->excludes(force_opt);
    list_devices_opt->excludes(add_device_opt)->excludes(remove_device_opt)->excludes(add_plot_opt)->excludes(remove_plot_opt)->excludes(list_plots_opt)->excludes(init_opt); //->excludes(force_opt);
    CLI11_PARSE(app, argc, argv);

    if (init) {
        if (!PlotFS::init(config_path, force)) {
            std::cerr << "init failed" << std::endl;
            return EXIT_FAILURE;
        }
        std::cerr << "initialized config at" << config_path << std::endl;
        return EXIT_SUCCESS;
    }

    if (list_devices) {
        auto g = PlotFS::loadGeometry(config_path);
        if (!g) {
            std::cerr << "Failed to load geometry" << std::endl;
            return EXIT_FAILURE;
        }

        if (g->geom->devices()) {
            for (const auto device : *g->geom->devices()) {
                auto space = device->end() - device->begin();
                if (g->geom->plots()) {
                    for (const auto plot : *g->geom->plots()) {
                        if (plot->shards()) { }
                        for (const auto shard : *plot->shards()) {
                            if (*shard->device_id() == *device->id()) {
                                space -= shard->end() - shard->begin();
                            }
                        }
                    }
                }
                auto size = device->end() - device->begin();
                std::cout << to_string(*device->id()) << " " << space << "/" << size << " " << 100 - (space * 100 / size) << "% " << device->path()->c_str() << std::endl;
            }
        }
        return EXIT_SUCCESS;
    }

    if (list_plots) {
        auto g = PlotFS::loadGeometry(config_path);
        if (!g) {
            std::cerr << "Failed to load geometry" << std::endl;
            return EXIT_FAILURE;
        }

        if (g->geom->plots()) {
            for (const auto plot : *g->geom->plots()) {
                uint64_t size = 0, shards = 0;
                if (plot->shards()) {
                    shards = plot->shards()->size();
                    for (const auto shard : *plot->shards()) {
                        size += shard->end() - shard->begin();
                    }
                }
                std::cout << to_string(*plot->id()) << " " << size << " " << shards << std::endl;
            }
        }

        return EXIT_SUCCESS;
    }

    if (!add_device.empty()) {
        PlotFS plotfs(config_path);
        if (!plotfs.isOpen()) {
            std::cerr << "Could not open plotfs" << std::endl;
            return EXIT_FAILURE;
        }
        return plotfs.addDevice(add_device, force) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (!remove_device.empty()) {
        auto device_id = to_vector(remove_device);
        PlotFS plotfs(config_path);
        if (!plotfs.isOpen()) {
            std::cerr << "Could not open plotfs" << std::endl;
            return EXIT_FAILURE;
        }
        return plotfs.removeDevice(device_id) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (!add_plot.empty()) {
        PlotFS plotfs(config_path);
        if (!plotfs.isOpen()) {
            std::cerr << "Could not open plotfs" << std::endl;
            return EXIT_FAILURE;
        }
        for (const auto& plot_pah : add_plot) {
            if (!plotfs.addPlot(plot_pah)) {
                return EXIT_FAILURE;
            }

            if (remove_source) {
                try {
                    std::error_code errc;
                    if (!std::filesystem::remove(plot_pah, errc)) {
                        std::cerr << "Could not remove source: " << errc.message() << std::endl;
                    } else {
                        std::cerr << "Removed " << plot_pah << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Could not remove source: " << e.what() << std::endl;
                }
            }
        }
        return EXIT_SUCCESS;
    }

    if (!remove_plot.empty()) {
        auto plot_id = to_vector(remove_plot);
        PlotFS plotfs(config_path);
        if (!plotfs.isOpen()) {
            std::cerr << "Could not open plotfs" << std::endl;
            return EXIT_FAILURE;
        }
        return plotfs.removePlot(plot_id) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}
