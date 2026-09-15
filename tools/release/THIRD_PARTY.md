# Third-party code in the XenonLive Launcher

| Component | Version | Licence | Where |
|---|---|---|---|
| Dear ImGui | 1.91.9b | MIT | thirdparty/imgui (compiled in) |
| miniz | 3.0.2 | MIT | thirdparty/miniz (compiled in) |
| stb_image | 2.30 | MIT / public domain | thirdparty/stb (compiled in) |
| zstd (decoder) | 1.5.7 | BSD | thirdparty/zstd (compiled in) |
| Selawik (font) | 1.01 | SIL OFL 1.1 | thirdparty/selawik (compiled in as bytes) |
| Noto Sans CJK JP (font) | 2.004 | SIL OFL 1.1 | thirdparty/notocjk, subset to the launcher's Japanese and Korean strings (compiled in as bytes) |
| SDL2 | 2.32.10 | zlib | lib/libSDL2-2.0.so.0 (Linux), SDL2.dll (Windows) |
| libcurl | 8.14.1 | curl (MIT-like) | statically linked; HTTP only, no zlib |
| OpenSSL | 3.0 (Ubuntu 22.04) | Apache 2.0 | statically linked (Linux) |
| libstdc++, libgcc_s | GCC 12 | GPL 3 with the GCC Runtime Library Exception | lib/ (Linux) |
| libxlive | github.com/wivi514/XenonLive | see that repository | compiled in |

On Windows, TLS is the operating system's (Schannel); no OpenSSL is shipped.
The licence texts are in the components' own distributions; ImGui's, miniz's,
stb's and zstd's are beside the vendored files in the source repository;
Selawik's is LICENSE-Selawik.txt beside this file; Noto Sans CJK's is LICENSE-NotoSansCJK.txt.
