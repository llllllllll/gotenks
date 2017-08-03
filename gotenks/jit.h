#pragma once

#include <optional>
#include <vector>

#include <libgccjit++.h>
#include <Python.h>

#include "gotenks/node.h"

namespace gotenks {
namespace jit {
struct next_function;

std::optional<next_function> compile(const std::vector<node>& steps);

/** Result closure type.
 */
struct next_function final {
private:
    typedef PyObject* (*lifted_function)(PyObject**, std::size_t, PyObject*);

    gcc_jit_result* m_gcc_jit_result;
    lifted_function m_lifted_function;
    std::vector<PyObject*> m_functions;

    inline static auto get_func(gcc_jit_result* result) {
        void* code = gcc_jit_result_get_code(result, "next");
        return reinterpret_cast<lifted_function>(code);
    }

protected:
    friend std::optional<next_function> compile(const std::vector<node>& steps);

    inline next_function(gcc_jit_result* result, const std::vector<node>& steps)
        : m_gcc_jit_result(result),
          m_lifted_function(get_func(result)) {

        for (const node& step : steps) {
            PyObject* ob = step.function();
            Py_INCREF(ob);
            m_functions.emplace_back(ob);
        }
    }

public:
    inline next_function()
        : m_gcc_jit_result(nullptr),
          m_lifted_function(nullptr),
          m_functions() {}

    inline next_function(next_function&& mvfrom) noexcept
        : m_gcc_jit_result(mvfrom.m_gcc_jit_result),
          m_lifted_function(mvfrom.m_lifted_function),
          m_functions(std::move(mvfrom.m_functions)) {

        mvfrom.m_gcc_jit_result = nullptr;
    }

    inline next_function& operator=(next_function&& mvfrom) noexcept {
        m_gcc_jit_result = mvfrom.m_gcc_jit_result;
        m_lifted_function = mvfrom.m_lifted_function;
        m_functions = std::move(mvfrom.m_functions);

        mvfrom.m_gcc_jit_result = nullptr;

        return *this;
    }

    inline ~next_function() {
        if (!m_gcc_jit_result) {
            return;
        }

        gcc_jit_result_release(m_gcc_jit_result);
        for (PyObject* ob : m_functions) {
            Py_DECREF(ob);
        }
    }

    inline PyObject* operator()(PyObject* iter) {
        return m_lifted_function(m_functions.data(),
                                 m_functions.size(),
                                 iter);
    }
};
}  // namespace jit
}  // namespace gotenks
