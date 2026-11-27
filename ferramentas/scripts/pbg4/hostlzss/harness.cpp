// Host harness: decodes with the port's real Lzss::Decompress.  usage: harness <comp> <decompSize> <out>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "pbg4/Lzss.hpp"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    std::vector<unsigned char> src;
    int c;
    while ((c = fgetc(f)) != EOF) src.push_back((unsigned char)c);
    fclose(f);
    unsigned dsz = (unsigned)strtoul(argv[2], nullptr, 10);
    unsigned char *dst = Lzss::Decompress(src.data(), (i32)src.size(), NULL, dsz);
    if (!dst) { fprintf(stderr, "decompress returned NULL\n"); return 3; }
    FILE *o = fopen(argv[3], "wb");
    fwrite(dst, 1, dsz, o);
    fclose(o);
    free(dst);
    return 0;
}
