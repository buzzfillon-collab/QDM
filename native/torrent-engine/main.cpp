#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/version.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>

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

static std::string torrent_id(const lt::torrent_handle& handle) {
    return handle.info_hashes().get_best().to_string();
}

static void persist_records(const std::string& state_file, const json& records) {
    if (state_file.empty()) return;
    const auto target = std::filesystem::u8path(state_file);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
    const auto temporary = std::filesystem::u8path(state_file + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write torrent session state.");
        out << records.dump(2);
        if (!out) throw std::runtime_error("Failed to write torrent session state.");
    }
    std::error_code ec;
    std::filesystem::remove(target, ec);
    std::filesystem::rename(temporary, target);
}

static lt::torrent_handle add_record(lt::session& session, const json& record, lt::error_code& ec) {
    const auto kind = record.value("kind", "");
    const auto save_path = record.value("savePath", "");
    if (save_path.empty()) throw std::runtime_error("A destination folder is required.");

    lt::add_torrent_params params;
    if (kind == "magnet") {
        const auto uri = record.value("uri", "");
        params = lt::parse_magnet_uri(uri, ec);
        if (ec) return {};
    } else if (kind == "file") {
        const auto file = record.value("file", "");
        if (file.empty() || !std::filesystem::exists(std::filesystem::u8path(file)))
            throw std::runtime_error("The selected .torrent file no longer exists.");
        params = lt::load_torrent_file(file, ec);
        if (ec) return {};
    } else {
        ec = lt::errors::invalid_torrent_handle;
        return {};
    }

    params.save_path = save_path;
    if (record.value("paused", false)) params.flags |= lt::torrent_flags::paused;
    return session.add_torrent(std::move(params), ec);
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
        std::string state_file;
        json records = json::array();
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
                if (command == "configure") {
                    state_file = request.value("stateFile", "");
                    if (!state_file.empty() && std::filesystem::exists(std::filesystem::u8path(state_file))) {
                        std::ifstream in(std::filesystem::u8path(state_file), std::ios::binary);
                        json loaded = json::array();
                        try { in >> loaded; } catch (const std::exception& e) {
                            std::cerr << "Ignoring corrupt torrent state: " << e.what() << std::endl;
                        }
                        if (loaded.is_array()) {
                            records = loaded;
                            for (const auto& record : records) {
                                try {
                                    lt::error_code ec;
                                    auto handle = add_record(session, record, ec);
                                    if (ec) {
                                        std::cerr << "Skipping saved torrent: " << ec.message() << std::endl;
                                    } else if (record.contains("paused") && record["paused"].is_boolean() && record["paused"].get<bool>()) {
                                        handle.pause();
                                    }
                                } catch (const std::exception& e) {
                                    std::cerr << "Skipping saved torrent: " << e.what() << std::endl;
                                }
                            }
                        }
                    }
                    std::cout << json{{"ok", true}, {"restored", session.get_torrents().size()}}.dump() << std::endl;
                } else if (command == "ping") {
                    std::cout << json{{"ok", true}, {"engine", "libtorrent"}, {"version", LIBTORRENT_VERSION}}.dump() << std::endl;
                } else if (command == "add_magnet" || command == "add_torrent") {
                    json record;
                    record["kind"] = command == "add_magnet" ? "magnet" : "file";
                    record["savePath"] = request.value("savePath", "");
                    record["paused"] = request.value("paused", false);
                    if (command == "add_magnet") {
                        record["uri"] = request.value("uri", "");
                        if (record["uri"].get<std::string>().rfind("magnet:?", 0) != 0)
                            throw std::runtime_error("A valid magnet URI is required.");
                    } else {
                        record["file"] = request.value("file", "");
                    }
                    if (record["savePath"].get<std::string>().empty())
                        throw std::runtime_error("A destination folder is required.");

                    lt::error_code ec;
                    auto handle = add_record(session, record, ec);
                    if (ec) throw std::runtime_error(ec.message());
                    const auto id = torrent_id(handle);
                    record["id"] = id;
                    auto it = std::find_if(records.begin(), records.end(), [&](const json& item) {
                        return item.value("id", "") == id;
                    });
                    if (it == records.end()) records.push_back(record);
                    else *it = record;
                    persist_records(state_file, records);
                    std::cout << json{{"ok", true}, {"torrent", snapshot(handle)}}.dump() << std::endl;
                } else if (command == "list") {
                    const auto handles = session.get_torrents();
                    json items = json::array();
                    for (const auto& handle : handles) items.push_back(snapshot(handle));
                    std::cout << json{{"ok", true}, {"torrents", items}}.dump() << std::endl;
                } else if (command == "pause" || command == "resume" || command == "remove") {
                    const auto id = request.value("id", "");
                    bool found = false;
                    for (const auto& handle : session.get_torrents()) {
                        if (torrent_id(handle) != id) continue;
                        found = true;
                        if (command == "pause") handle.pause();
                        else if (command == "resume") handle.resume();
                        else session.remove_torrent(handle);
                        break;
                    }
                    if (!found) throw std::runtime_error("Torrent not found.");
                    if (command == "remove") {
                        records.erase(std::remove_if(records.begin(), records.end(), [&](const json& item) {
                            return item.value("id", "") == id;
                        }), records.end());
                    } else {
                        for (auto& record : records) {
                            if (record.value("id", "") == id) record["paused"] = command == "pause";
                        }
                    }
                    persist_records(state_file, records);
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
