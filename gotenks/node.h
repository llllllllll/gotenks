#pragma once

#include <Python.h>

namespace gotenks {

/** The kind of operation associated with this function.
 */
enum class node_kind {
    map = 0,
    filter = 1,
};

/** An operation in a fused iterator.
 */
struct node final {
private:
    /** The function to call on the elements.
     */
    PyObject* m_function;

    /** How to interpret the function's result.
     */
    node_kind m_kind;

public:
    /** Construct a new node from a function, a kind, and optionally a tail
        of operations.

        @param function The function to perform at this step.
        @param kind The kind of node this is.
        @param tail The chain of operations which follow this.
     */
    node(PyObject* function, node_kind kind)
        : m_function(function),
          m_kind(kind) {
        Py_INCREF(function);
    }

    node(const node& cpfrom)
        : m_function(cpfrom.m_function),
          m_kind(cpfrom.m_kind) {
        Py_INCREF(m_function);
    }

    node(node&& mvfrom) noexcept
        : m_function(mvfrom.m_function),
          m_kind(mvfrom.m_kind) {
        mvfrom.m_function = nullptr;
    }

    node& operator=(const node& cpfrom) {
        m_function = cpfrom.m_function;
        Py_INCREF(m_function);
        m_kind = cpfrom.m_kind;

        return *this;
    }

    node& operator=(node&& mvfrom) noexcept {
        m_function = mvfrom.m_function;
        mvfrom.m_function = nullptr;
        m_kind = mvfrom.m_kind;

        return *this;
    }

    ~node() {
        Py_XDECREF(m_function);
    }

    inline PyObject* function() {
        return m_function;
    }

    inline void function(PyObject* new_function) {
        Py_XDECREF(m_function);
        Py_INCREF(new_function);
        m_function = new_function;
    }

    inline PyObject* function() const {
        return m_function;
    }

    inline node_kind kind() {
        return m_kind;
    }

    inline void kind(node_kind new_kind) {
        m_kind = new_kind;
    }

    inline node_kind kind() const {
        return m_kind;
    }
};
}  // namespace gotenks
