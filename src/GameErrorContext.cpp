#include "GameErrorContext.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef __PS2__
void Ps2LogAppend(const char *text);
#endif

GameErrorContext g_GameErrorContext;

const char *GameErrorContext::Log(const char *fmt, ...)
{
    char tmp[8192];
    size_t tmpSize;
    va_list args;

    va_start(args, fmt);
    vsprintf(tmp, fmt, args);
    tmpSize = strlen(tmp);
#ifdef __PS2__
    Ps2LogAppend(tmp);
#ifdef TH07_PS2_VERBOSE
    printf("LOG: %s", tmp);
    fflush(stdout);
#endif
#endif
    if (this->m_BufferEnd + tmpSize < this->m_Buffer + 0x1fff)
    {
        strcpy(this->m_BufferEnd, tmp);

        this->m_BufferEnd += tmpSize;
        *this->m_BufferEnd = '\0';
    }
    va_end(args);
    return fmt;
}

const char *GameErrorContext::Fatal(const char *fmt, ...)
{
    char tmp[512];
    size_t tmpSize;
    va_list args;

    va_start(args, fmt);
    vsprintf(tmp, fmt, args);
    tmpSize = strlen(tmp);
#ifdef __PS2__
    Ps2LogAppend("FATAL: ");
    Ps2LogAppend(tmp);
#ifdef TH07_PS2_VERBOSE
    printf("FATAL: %s", tmp);
    fflush(stdout);
#endif
#endif
    if (this->m_BufferEnd + tmpSize < this->m_Buffer + 0x1fff)
    {
        strcpy(this->m_BufferEnd, tmp);
        this->m_BufferEnd += tmpSize;
        *this->m_BufferEnd = '\0';
    }
    va_end(args);
    this->m_ShowMessageBox = true;
    return fmt;
}
