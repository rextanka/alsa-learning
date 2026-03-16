#include "PatchStore.hpp"
#include "Logger.hpp"
#include <fstream>

namespace audio {

bool PatchStore::save_to_file(const PatchData& patch, const std::string& path) {
    try {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << serialize(patch);
        return true;
    } catch (...) {
        return false;
    }
}

bool PatchStore::load_from_file(PatchData& patch, const std::string& path) {
    auto& log = AudioLogger::instance();
    try {
        log.log_message("PatchStore", ("Loading: " + path).c_str());
        std::ifstream file(path);
        if (!file.is_open()) {
            log.log_message("PatchStore", ("Failed to open: " + path).c_str());
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        bool success = deserialize(patch, content);
        if (success) {
            log.log_message("PatchStore", ("Loaded: " + patch.name).c_str());
        } else {
            log.log_message("PatchStore", ("Deserialize failed: " + path).c_str());
        }
        return success;
    } catch (const std::exception& e) {
        log.log_message("PatchStore", e.what());
        return false;
    } catch (...) {
        log.log_message("PatchStore", "Unknown exception loading patch");
        return false;
    }
}

} // namespace audio
