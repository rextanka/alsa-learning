#include "PatchStore.hpp"
#include <fstream>
#include <iostream>

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
    try {
        std::cout << "[PatchStore] Attempting to load: " << path << std::endl;
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "[PatchStore] Failed to open file: " << path << std::endl;
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        bool success = deserialize(patch, content);
        if (success) {
            std::cout << "[PatchStore] Successfully loaded patch: " << patch.name << std::endl;
        } else {
            std::cerr << "[PatchStore] Failed to deserialize patch from: " << path << std::endl;
        }
        return success;
    } catch (const std::exception& e) {
        std::cerr << "[PatchStore] Exception loading patch: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[PatchStore] Unknown exception loading patch" << std::endl;
        return false;
    }
}

} // namespace audio
