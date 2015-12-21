// 临时放到这里
//////////////////////////////
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h> // MAX_INT
#include <stdarg.h>

#include <BasicLog.hh>

// from ffmpeg
static int avpriv_vsnprintf(char *s, size_t n, const char *fmt,
                     va_list ap)
{
    int ret;
    va_list ap_copy;

    if (n == 0)
#ifdef WIN32
        return _vscprintf(fmt, ap);
#else
        return vsnprintf(NULL, 0, fmt, ap);
#endif

    else if (n > INT_MAX)
        //return AVERROR(EOVERFLOW);
        return -1;

    /* we use n - 1 here because if the buffer is not big enough, the MS
     * runtime libraries don't add a terminating zero at the end. MSDN
     * recommends to provide _snprintf/_vsnprintf() a buffer size that
     * is one less than the actual buffer, and zero it before calling
     * _snprintf/_vsnprintf() to workaround this problem.
     * See http://msdn.microsoft.com/en-us/library/1kt27hek(v=vs.80).aspx */
    memset(s, 0, n);
    //va_copy(ap_copy, ap);
    ap_copy = ap;
#ifdef WIN32
    ret = _vsnprintf(s, n - 1, fmt, ap_copy);
#else
    ret = vsnprintf(s, n - 1, fmt, ap_copy);
#endif
    va_end(ap_copy);
    if (ret == -1)
#ifdef WIN32
        ret = _vscprintf(fmt, ap);
#else
        ret = vsnprintf(NULL, 0, fmt, ap);
#endif
    return ret;
}
// from ffmpeg
static int avpriv_snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = avpriv_vsnprintf(s, n, fmt, ap);
    va_end(ap);

    return ret;
}

// 默认输出函数，打印到终端
static void logDefaultCallback(const char* fmt, va_list vl)
{
    char buffer[1024] = {0};
    char line[1024] = {0};
    
    vsprintf(buffer, fmt, vl);

    avpriv_snprintf(line, sizeof(line), "%s", buffer);

    fputs(line, stdout);
}

log_handle_fn_t log_callback = logDefaultCallback;

void rtspLogSetCallback(log_handle_fn_t fn)
{
    log_callback = fn;
}

void rtspLog(const char* fmt, ...)
{
    va_list vl;

    va_start(vl, fmt);

    if (log_callback)
    {
        log_callback(fmt, vl);
    }

    va_end(vl);
}