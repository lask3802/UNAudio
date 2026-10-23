#include "Clip.h"

#include <cstdlib>

namespace una {

Clip::~Clip() {
    std::free(pcm);
    pcm = nullptr;
}

void Clip::FreeSource() {
    std::vector<uint8_t>().swap(owned);
    src = nullptr;
    srcSize = 0;
}

} // namespace una
