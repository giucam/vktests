
#pragma once

#define FLAGS(flags) \
inline int operator&(flags a, flags b) { return (int)a & (int)b; } \
inline flags operator|(flags a, flags b) { return (flags)((int)a | (int)b); } \
inline flags &operator|=(flags &a, flags b) { a = a | b; return a; }
