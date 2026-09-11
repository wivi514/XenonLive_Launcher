// SHA-256, for checking a downloaded release against its SHA256SUMS. Our
// own ~100 lines rather than a dependency: the launcher already refuses to
// grow a runtime dependency for anything, and a hash is not where to start.
#pragma once

#include <cstdint>
#include <string>

namespace launcher {

class Sha256 {
public:
    Sha256();
    void Update(const uint8_t* data, size_t size);
    // Lower-case hex of the digest. Finalizes; the object is done after.
    std::string Finish();

private:
    void Block(const uint8_t* block);
    uint32_t state_[8];
    uint8_t buffer_[64];
    size_t buffered_ = 0;
    uint64_t total_ = 0;
};

// Hex digest of a whole file, or empty if it cannot be read.
std::string Sha256File(const std::string& path);

}  // namespace launcher
