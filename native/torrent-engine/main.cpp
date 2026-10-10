#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace lt = libtorrent;
using json = nlohmann::json;

static std::string state_name(lt::torrent_status::state_t state) {
    switch (state) {
        case lt::torrent_status::checking_files: return "checking";
        case lt::torrent_status::downloading_metadata: return "metadata";
        case lt::torrent_status::downloading: return "downloading";
        case lt::torrent_status::finished: return "finished";
        case lt::torrent_status::seeding: return "seeding";
        case lt::torrent_status::checking_resume_data: return "checking";
        default: return "queued";
    }
}

static json snapshot(const lt::torrent_handle& handle) {
    if (!handle.is_valid()) return json{{"valid", false}};
    const auto s = handle.status();
    const auto hashes = handle.info_hashes();
    return json{
        {"id", hashes.get_best().to_string()},
        {"name", s.name},
        {"savePath", s.save_path},
        {"progress", static_cast<double>(s.progress) * 100.0},
        {"downloaded", s.total_done},
        {"totalWanted", s.total_wanted},
        {"downloadRate", s.download_rate},
        {"uploadRate", s.upload_rate},
        {"numSeeds", s.num_seeds},
        {"numPeers", s.num_peers},
        {"state", state_name(s.state)},
        {"paused", s.paused},
        {"isFinished", s.is_finished},
        {"isSeeding", s.is_seeding}
    };
}

int main() {
    try {
        lt::settings_pack settings;
        settings.set_bool(lt::settings_pack::enable_dht, true);
        settings.set_bool(lt::settings_pack::enable_lsd, true);
        settings.set_bool(lt::settings_pack::enable_upnp, true);
        settings.set_bool(lt::settings_pack::enable_natpmp, true);
        settings.set_int(lt::settings_pack::alert_mask,
            lt::alert_category::error | lt::alert_category::status);
        lt::session session(settings);
        std::string line;

        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            json request;
            try {
                request = json::parse(line);
            } catch (const std::exception& e) {
                std::cout << json{{"ok", false}, {"error", std::string("Invalid JSON: ") + e.what()}}.dump() << std::endl;
                continue;
            }

            const std::string command = request.value("command", "");
            try {
                if (command == "ping") {
                    std::cout << json{{"ok", true}, {"engine", "libtorrent"}, {"version", LIBTORRENT_VERSION}}.dump() << std::endl;
                } else if (command == "add_magnet") {
                    const auto uri = request.value("uri", "");
                    const auto save_path = request.value("savePath", "");
                    if (uri.rfind("magnet:?", 0) != 0 || save_path.empty())
                        throw std::runtime_error("A magnet URI and destination folder are required.");
                    lt::error_code ec;
                    lt::add_torrent_params params = lt::parse_magnet_uri(uri, ec);
                    if (ec) throw std::runtime_error(ec.message());
                    params.save_path = save_path;
                    if (request.contains("paused") && request["paused"].get<bool>())
                        params.flags |= lt::torrent_flags::paused;
                    auto handle = session.add_torrent(std::move(params), ec);
                    if (ec) throw std::runtime_error(ec.message());
                    std::cout << json{{"ok", true}, {"torrent", snapshot(handle)}}.dump() << std::endl;
                } else if (command == "add_torrent") {
                    const auto file = request.value("file", "");
                    const auto save_path = request.value("savePath", "");
                    if (file.empty() || save_path.empty())
                        throw std::runtime_error("A .torrent file and destination folder are required.");
                    lt::error_code ec;
                    auto params = lt::load_torrent_file(file, ec);
                    if (ec) throw std::runtime_error(ec.message());
                    params.save_path = save_path;
                    if (request.contains("paused") && request["paused"].get<bool>())
                        params.flags |= lt::torrent_flags::paused;
                    auto handle = session.add_torrent(std::move(params), ec);
                    if (ec) throw std::runtime_error(ec.message());
                    std::cout << json{{"ok", true}, {"torrent", snapshot(handle)}}.dump() << std::endl;
                } else if (command == "list") {
                    std::vector<lt::torrent_handle> handles = session.get_torrents();
                    json items = json::array();
                    for (const auto& handle : handles) items.push_back(snapshot(handle));
                    std::cout << json{{"ok", true}, {"torrents", items}}.dump() << std::endl;
                } else if (command == "pause" || command == "resume" || command == "remove") {
                    const auto id = request.value("id", "");
                    bool found = false;
                    for (const auto& handle : session.get_torrents()) {
                        if (handle.info_hashes().get_best().to_string() != id) continue;
                        found = true;
                        if (command == "pause") handle.pause();
                        else if (command == "resume") handle.resume();
                        else session.remove_torrent(handle);
                        break;
                    }
                    if (!found) throw std::runtime_error("Torrent not found.");
                    std::cout << json{{"ok", true}, {"id", id}, {"command", command}}.dump() << std::endl;
                } else if (command == "shutdown") {
                    std::cout << json{{"ok", true}}.dump() << std::endl;
                    break;
                } else {
                    throw std::runtime_error("Unknown torrent command.");
                }
            } catch (const std::exception& e) {
                std::cout << json{{"ok", false}, {"error", e.what()}}.dump() << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "QDM Pro torrent engine failed: " << e.what() << std::endl;
        return 2;
    }
    return 0;
}
