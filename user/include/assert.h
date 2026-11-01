#pragma once

#ifndef _ASSERT_H
#define _ASSERT_H


#ifdef __cplusplus
extern "C" {
#endif

void __rk_assert_fail(const char *expr, const char *file, int line);

#ifdef __cplusplus
}
#endif

#ifdef NDEBUG
#define assert(...) ((void)0)
#else
#define assert(...) \
    ((__VA_ARGS__) ? (void)0 : __rk_assert_fail(#__VA_ARGS__, __FILE__, __LINE__))
#endif

#endif /* _ASSERT_H */
