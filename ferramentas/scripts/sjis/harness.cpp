// reads lines of hex-encoded Shift-JIS, writes lines of hex-encoded UTF-8 produced by the port's converter
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "Ps2ShiftJis.hpp"
int main()
{
    char line[4096];
    while (fgets(line, sizeof line, stdin))
    {
        std::string in;
        for (char *p = line; p[0] && p[1] && p[0] != '\n'; p += 2)
        {
            unsigned v; sscanf(p, "%2x", &v); in.push_back((char)v);
        }
        char *out = Ps2SjisToUtf8(in.c_str());
        for (const char *q = out; *q; q++) printf("%02x", (unsigned char)*q);
        printf("\n");
        free(out);
    }
    return 0;
}
