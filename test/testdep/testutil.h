#pragma once

template <typename T, size_t N> char (&_ArraySize( T (&a)[N]))[N];
template<size_t n> struct _NotZero { static const size_t value = n; };
template<>         struct _NotZero<0> {};
#define Countof(a) (_NotZero<(sizeof(_ArraySize(a)))>::value)
