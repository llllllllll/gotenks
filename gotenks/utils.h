#pragma once

#include <Python.h>

namespace gotenks {

/** Call a Python function with variadic positional arguments.

    @param function The function to call.
    @param args The arguments to pass.
    @return The result of `function(*args)`.
 */
template<typename... Args>
inline PyObject* call_function(PyObject* function, Args... args) {
#if PY_MINOR_VERSION < 6
    return PyObject_CallFunctionObjArgs(function, args..., nullptr);
#else
    PyObject* arg_array[] = {args...};
    return _PyObject_FastCallDict(function,
                                  arg_array,
                                  sizeof...(args),
                                  nullptr);
#endif
}

}  // namespace gotenks
