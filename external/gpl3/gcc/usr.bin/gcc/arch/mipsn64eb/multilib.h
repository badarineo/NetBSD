/* This file is automatically generated.  DO NOT EDIT! */
/* Generated from: NetBSD: mknative-gcc,v 1.114 2021/04/11 07:35:45 mrg Exp  */
/* Generated from: NetBSD: mknative.common,v 1.16 2018/04/15 15:13:37 christos Exp  */

static const char *const multilib_raw[] = {
". !mabi=n32 !mabi=64 !mabi=32;",
".:../lib/n32 mabi=n32 !mabi=64 !mabi=32;",
".:. !mabi=n32 mabi=64 !mabi=32;",
".:../lib/o32 !mabi=n32 !mabi=64 mabi=32;",
NULL
};

static const char *const multilib_reuse_raw[] = {
NULL
};

static const char *const multilib_matches_raw[] = {
"mabi=n32 mabi=n32;",
"mabi=64 mabi=64;",
"mabi=32 mabi=32;",
NULL
};

static const char *multilib_extra = "";

static const char *const multilib_exclusions_raw[] = {
NULL
};

static const char *multilib_options = "mabi=n32/mabi=64/mabi=32";
