// Noto Sans CJK (SIL OFL 1.1), subset to the launcher's Japanese and Korean
// strings by tools/gen_cjk_font.py, as bytes for AddFontFromMemoryTTF with
// FontDataOwnedByAtlas off.
#pragma once

namespace notocjk {

extern const unsigned char kSubset[];
extern const unsigned int kSubsetSize;
// Every character in the subset, UTF-8, NUL-terminated: what to ask the
// atlas for.
extern const char kSubsetChars[];

}  // namespace notocjk
