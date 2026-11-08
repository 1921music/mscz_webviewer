#pragma once
#define MAKE_LIB_VERSION(a,b,c,d) (((((((EAS_U32)a << 8) | b) << 8) | c) << 8) | d)
#define LIB_VERSION MAKE_LIB_VERSION(4,0,2,0)
