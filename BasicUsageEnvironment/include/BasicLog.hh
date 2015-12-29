#ifndef _BASIC_USAGE_ENVIRONMENT_HH
#define _BASIC_USAGE_ENVIRONMENT_HH

#include <stdarg.h>

typedef void (*log_handle_fn_t)(const char* fmt, va_list vl);

/** 
 * @brief 设置日志处理方式回调函数
 * 
 * @param fn[in] : 回调函数指针
 *
 * @note
 *        如用户不设置，则使用默认处理方式，即打印到终端
 */
void rtspLogSetCallback(log_handle_fn_t fn);

/** 
 * @brief 日志打印函数。
 * 
 * @param fmt[in]   : 可变参数
 *
 */
extern void rtspLog(const char* fmt, ...)__attribute__((format(printf,1,2)));

#endif
