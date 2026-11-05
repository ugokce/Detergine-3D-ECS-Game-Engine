// Linked into every test executable (see dg_add_test in tests/CMakeLists.txt).
//
// On Windows, abort() and CRT assertions open modal "Debug Error" dialogs in Debug builds. On a CI runner nobody can
// click them, so death tests (which deliberately hit DG_ASSERT -> std::abort) would hang. Route everything to stderr.
#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>

    #include <crtdbg.h>
    #include <cstdlib>
    #include <initializer_list>

namespace {

struct NoErrorDialogs {
    NoErrorDialogs() {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
    }
};

const NoErrorDialogs gNoErrorDialogs;

} // namespace
#endif
