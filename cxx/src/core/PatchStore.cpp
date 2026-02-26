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
        std::ifstream file(path);
        if (!file.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        return deserialize(patch, content);
    } catch (...) {
        return false;
    }
}

} // namespace audio
