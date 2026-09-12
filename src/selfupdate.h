// Replacing the running launcher with a newer release the installer has
// already downloaded, verified and unpacked into a staging directory.
//
// Three flavours, three swaps. An AppImage is one file: the new one is
// renamed over $APPIMAGE. A tarball install is a directory: each staged
// file is renamed over its old self (a rename replaces a running
// executable where a write would not). A Windows zip cannot touch a
// running .exe at all, so a script waits for this process to end, moves
// the staged files over, and starts the new launcher. Every path ends the
// same way: the caller quits and the new launcher comes up in its place.
#pragma once

#include <filesystem>
#include <string>

namespace launcher {

// The directory the installer should unpack the new release into: beside
// the running launcher, on the same filesystem, so the renames are atomic.
std::filesystem::path SelfUpdateStagingDir();

// Whether this build can be replaced at all: a "dev" build cannot (no
// version to compare), and a launcher whose own directory is read-only
// cannot either. `why` says which.
bool SelfUpdatePossible(std::string& why);

// Puts the staged release in place and arranges for the new launcher to
// start once this process exits. True means: quit now. On failure nothing
// has been replaced and `error` says why.
bool ApplySelfUpdate(const std::filesystem::path& staging, std::string& error);

// This executable's path: $APPIMAGE for an AppImage, else the binary.
std::filesystem::path SelfPath();

}  // namespace launcher
