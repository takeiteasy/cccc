// CCCC_FLAGS: --std=c23
int main(void) {
    int result = 0;

#if __has_feature(c99)
    result += 6;
#endif
#if __has_feature(c11)
    result += 6;
#endif
#if __has_feature(c23)
    result += 6;
#endif
#if __has_extension(c_generic_selections)
    result += 6;
#endif
#if __has_feature(c_alignas) && __has_feature(c_alignof)
    result += 6;
#endif
#if __has_feature(c_static_assert)
    result += 6;
#endif
#if __has_feature(c_atomic) || __has_feature(c_thread_local)
    return 1;
#endif
#if __has_feature(unknown_cccc_feature_280)
    return 2;
#endif

    return result + 6;
}
