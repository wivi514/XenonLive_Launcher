// Unpacking a release zip (the Windows bundle). Built on every platform so
// the code Windows runs is the code the Linux tests run.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace launcher {

// Extracts `zip` into `dir`, dropping the leading `strip_top` ("Bundle/")
// from every entry that has it. Files already there are overwritten;
// nothing is deleted, which is what lets an update leave the player's
// assets/ alone. Refuses entries that would escape `dir`.
bool ExtractZipInto(const std::filesystem::path& zip, const std::filesystem::path& dir,
                    const std::string& strip_top, std::string& error,
                    const std::function<void(uint64_t done, uint64_t total)>& progress = {});

// The same for a .tar.zst (the Linux bundle): zstd streamed through a ustar
// reader. Regular files, directories and symbolic links (the bundle's SDL2
// is one); file modes are kept, so the runtime stays executable. Progress is
// in compressed bytes. `progress` total is the file's size.
bool ExtractTarZstInto(const std::filesystem::path& archive, const std::filesystem::path& dir,
                       const std::string& strip_top, std::string& error,
                       const std::function<void(uint64_t done, uint64_t total)>& progress = {});

}  // namespace launcher
