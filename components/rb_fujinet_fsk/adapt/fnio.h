// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): the file calls the owner's FSK
// code makes (fseek, fread, ftell), over a RetroBridge TapeSource (the .cas on the microSD, or in
// memory on the host). One fnFile has one position; it is used by one task at a time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace rb::cassette {
class TapeSource;
}

struct fnFile {
    rb::cassette::TapeSource* source = nullptr;
    std::size_t position = 0;
};

namespace fnio {
int fseek(fnFile* file, long offset, int whence);
std::size_t fread(void* dst, std::size_t size, std::size_t count, fnFile* file);
long ftell(fnFile* file);
}  // namespace fnio
