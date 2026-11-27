// In-process benchmark: decodes every archive stream REPS times with the linked Lzss::Decompress.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "pbg4/Lzss.hpp"
int main(int argc, char **argv)
{
    FILE *m = fopen("manifest.txt", "r");
    std::vector<std::vector<unsigned char>> comps;
    std::vector<unsigned> sizes;
    char path[512];
    unsigned dsz;
    while (fscanf(m, "%511s %u", path, &dsz) == 2)
    {
        FILE *f = fopen(path, "rb");
        std::vector<unsigned char> v;
        int c;
        while ((c = fgetc(f)) != EOF) v.push_back((unsigned char)c);
        fclose(f);
        comps.push_back(v);
        sizes.push_back(dsz);
    }
    fclose(m);
    const int REPS = 10;
    unsigned long long total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int r = 0; r < REPS; r++)
    {
        for (size_t i = 0; i < comps.size(); i++)
        {
            unsigned char *d = Lzss::Decompress(comps[i].data(), (i32)comps[i].size(), NULL, sizes[i]);
            if (!d) { fprintf(stderr, "fail %zu\n", i); return 1; }
            total += d[0];
            free(d);
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    printf("%d passes over %zu streams: %.1f ms (checksum %llu)\n", REPS, comps.size(), ms, total);
    return 0;
}
