// Sanitizer runtime options linked into every native test program. Environment variables
// (ASAN_OPTIONS, UBSAN_OPTIONS, LSAN_OPTIONS) still override them.

extern "C" const char* __asan_default_options() {
    return "detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:"
           "check_initialization_order=1:strict_init_order=1";
}

extern "C" const char* __ubsan_default_options() {
    return "halt_on_error=1:print_stacktrace=1";
}

extern "C" const char* __lsan_default_options() {
    return "report_objects=1";
}
