#include <vector>

#include <Python.h>

namespace gotenks {

struct module_state {
    /** A reference to `gotenks.compose.compose`.
     */
    PyObject* compose;
};

/** Retrieve the module state.
 */
inline module_state* get_state(PyObject* m) {
    return reinterpret_cast<module_state*>(PyModule_GetState(m));
}

/** The kind of operation associated with this function.
 */
enum class node_kind {
    map = 0,
    filter = 1,
};

namespace detail {
template<typename T>
inline T& identity(T& a) {
    return a;
}
}  // namespace detail

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
    if constexpr (sizeof...(Args) == 1) {
        // optimization: when we have exactly one argument, don't push it onto
        // the stack, just pass the address directly to _PyObject_FastCallDict
        // with nargs=1
        return _PyObject_FastCallDict(function,
                                      &detail::identity(args...),
                                      1,
                                      nullptr);
    }
    else {
        PyObject* arg_array[] = {args...};
        return _PyObject_FastCallDict(function,
                                      arg_array,
                                      sizeof...(args),
                                      nullptr);
    }
#endif
}

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

    /** Apply the function to a given element.
     */
    PyObject* apply(PyObject* element) const {
        return call_function(m_function, element);
    }
};

/** The fused iterator C++ class.
 */
struct fused final {
private:
    /** The functions to apply in application order.

        Application order is visually reversed from how the code is written,
        for example:

        ```
        map(f, map(g, ...))
        ```

        would result in steps: `{g, f}` because `g` is applied first.
     */
    std::vector<node> m_steps;
    /** The underlying Python iterator to draw from.
     */
    PyObject* m_iter;

public:
    /** Construct a fused iterator of one step over a Python iterator.

        @param function The function to apply.
        @param kind The step find.
        @param iter The Python iterator to draw from.
     */
    fused(PyObject* function, node_kind kind, PyObject* iter)
        : m_steps({node(function, kind)}),
          m_iter(iter) {

        Py_INCREF(iter);
    }

    /** Construct a fused iterator which adds a new step to an existing fused
        iterator.

        @param function The function to apply.
        @param kind The step find.
        @param tail The fused iterator to build on.
        @param compose A reference to the `gotenks.compose.compose` function.
     */
    fused(PyObject* function,
          node_kind kind,
          const fused& tail,
          PyObject* compose)
        : m_steps(tail.m_steps),
          m_iter(tail.m_iter) {

        Py_INCREF(m_iter);

        node& last = m_steps.back();
        if (kind == node_kind::map && last.kind() == node_kind::map) {
            // This is the `map(f, map(g, ...))` case. here we fuse our new
            // map with the old map by constructing a `map(compose(f, g), ...)`.
            PyObject* composed = call_function(compose,
                                               function,
                                               last.function());
            if (composed) {
                // we successfully composed the old function and the new
                // function; update the old node in place and exit.
                last.function(composed);
                Py_DECREF(composed);
                return;
            }

            // if we can't compose these functions, clear the exception
            // and just create a new map node
            PyErr_Clear();
            // explicitly fall through to the emplace_back
        }

        m_steps.emplace_back(function, kind);
    }

    ~fused() {
        Py_DECREF(m_iter);
    }

    /** Interpreted `__next__` function.

        # Notes

        This is called 'interpreted' because we may want to try jit-compiling
        the body of this for faster iteration.
     */
    PyObject* interpreted_next() {
        static void* labels[] = {&&map_label, &&filter_label};

        bool filtered = false;
        PyObject* element;

        while ((element = PyIter_Next(m_iter))) {
            for (const node& step : m_steps) {
                PyObject* applied = step.apply(element);
                if (!applied) {
                    Py_DECREF(element);
                    return nullptr;
                }

                goto *labels[static_cast<std::size_t>(step.kind())];

            map_label:
                element = applied;
                continue;
            filter_label:
                int should_mask = PyObject_Not(applied);
                Py_DECREF(applied);
                if (should_mask < 0) {
                    Py_DECREF(element);
                    return nullptr;
                }
                filtered = should_mask;
                if (filtered) {
                    Py_DECREF(element);
                    break;
                }
            }

            if (!filtered) {
                break;
            }
        }

        return element;
    }

    /** Interpreted to_list.

        # Notes

        This is the same as `list(fused_iterator)`; however, we avoid all of the
        indirect calls in the `list` constructor and just iterate internally.
     */
    PyObject* interpreted_to_list() {
        static void* labels[] = {&&map_label, &&filter_label};

        PyObject* out = PyList_New(0);
        if (!out) {
            return nullptr;
        }

        bool filtered = false;
        PyObject* element;

        while ((element = PyIter_Next(m_iter))) {
            for (const node& step : m_steps) {
                PyObject* applied = step.apply(element);
                if (!applied) {
                    Py_DECREF(element);
                    Py_DECREF(out);
                    return nullptr;
                }

                goto *labels[static_cast<std::size_t>(step.kind())];

            map_label:
                element = applied;
                continue;
            filter_label:
                int should_mask = PyObject_Not(applied);
                Py_DECREF(applied);
                if (should_mask < 0) {
                    Py_DECREF(element);
                    Py_DECREF(out);
                    return nullptr;
                }
                filtered = should_mask;
                if (filtered) {
                    Py_DECREF(element);
                    break;
                }
            }

            if (!filtered) {
                PyList_Append(out, element);
            }
        }

        return out;
    }


    inline std::size_t size() {
        return m_steps.size();
    }

    auto begin() {
        return m_steps.begin();
    }

    auto end() {
        return m_steps.end();
    }
};

/** Python box for our fused iterator object.
 */
struct object {
    PyObject_HEAD
    fused m_fused;
};

/** Implementations of the methods for our Python fused iterator type.
 */
namespace methods {
void deallocate(object* self) {
    self->m_fused.~fused();
    PyObject_Del(self);
}

PyObject* iternext(object* self) {
    return self->m_fused.interpreted_next();
}

PyDoc_STRVAR(to_list_doc,
             "Force the iterator into a list.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "values : list\n"
             "    The computed values for the iterator\n");


PyObject* to_list(object* self) {
    return self->m_fused.interpreted_to_list();
}

PyDoc_STRVAR(steps_doc,
             "The list of the operations to apply to the stream.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "steps : list[(str, callable)]\n"
             "    The sequence of steps as pairs of operation kind and\n"
             "    function. The operations kinds are 'map' or 'filter'.\n");

PyObject* steps(object* self) {
    PyObject* map_string = PyUnicode_FromString("map");
    if (!map_string) {
        return nullptr;
    }

    PyObject* filter_string = PyUnicode_FromString("filter");
    if (!filter_string) {
        Py_DECREF(map_string);
        return nullptr;
    }

    PyObject* names[] = {map_string, filter_string};

    PyObject* steps = PyList_New(self->m_fused.size());
    if (!steps) {
        Py_DECREF(map_string);
        Py_DECREF(filter_string);
        return nullptr;
    }
    std::size_t ix = 0;

    for (const node& step : self->m_fused) {
        PyObject* name = names[static_cast<std::size_t>(step.kind())];
        PyObject* step_object = PyTuple_Pack(2, name, step.function());
        if (!step_object) {
            Py_DECREF(steps);
            return nullptr;
        }

        PyList_SET_ITEM(steps, ix++, step_object);
    }

    return steps;
}

PyMethodDef methods[] = {
    {"to_list", reinterpret_cast<PyCFunction>(to_list), METH_NOARGS, to_list_doc},
    {"steps", reinterpret_cast<PyCFunction>(steps), METH_NOARGS, steps_doc},
    {nullptr},
};
}  // namespace methods

PyDoc_STRVAR(type_doc,
             "An iterator which fuses chains of maps and filters.\n");

PyTypeObject type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "gotenks.fused.iterator",                           // tp_name
    sizeof(object),                                     // tp_basicsize
    0,                                                  // tp_itemsize
    reinterpret_cast<destructor>(methods::deallocate),  // tp_dealloc
    0,                                                  // tp_print
    0,                                                  // tp_getattr
    0,                                                  // tp_setattr
    0,                                                  // tp_reserved
    0,                                                  // tp_repr
    0,                                                  // tp_as_number
    0,                                                  // tp_as_sequence
    0,                                                  // tp_as_mapping
    0,                                                  // tp_hash
    0,                                                  // tp_call
    0,                                                  // tp_str
    PyObject_GenericGetAttr,                            // tp_getattro
    0,                                                  // tp_setattro
    0,                                                  // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                                 // tp_flags
    type_doc,                                           // tp_doc
    0,                                                  // tp_traverse
    0,                                                  // tp_clear
    0,                                                  // tp_richcompare
    0,                                                  // tp_weaklistoffset
    PyObject_SelfIter,                                  // tp_iter
    reinterpret_cast<iternextfunc>(methods::iternext),  // tp_iternext
    methods::methods,                                   // tp_methods,
};

/** Construct a new Python boxed fused iterator object.

    @tparam kind The kind of step to build.
    @param compose A reference to `gotenks.compose.compose`.
    @param function The function to apply.
    @param tail The iterator to apply to. This should either be a fused
                iterator to build on or a normal Python iterator.
 */
template<node_kind kind>
inline PyObject* new_fused(PyObject* compose,
                           PyObject* function,
                           PyObject* tail) {
    object* ret;

    if (Py_TYPE(tail) == &type) {
        ret = PyObject_New(object, &type);
        const fused& fused_tail = reinterpret_cast<object*>(tail)->m_fused;
        new(&ret->m_fused) fused(function,
                                 kind,
                                 fused_tail,
                                 compose);
    }
    else {
        PyObject* iter = PyObject_GetIter(tail);
        if (!iter) {
            return nullptr;
        }
        ret = PyObject_New(object, &type);
        new(&ret->m_fused) fused(function,
                                 kind,
                                 iter);
        Py_DECREF(iter);
    }

    return reinterpret_cast<PyObject*>(ret);
}

PyDoc_STRVAR(map_doc,
             "Lazily apply a function to every element of an iterator.\n"
             "\n"
             "Parameters\n"
             "----------\n"
             "function : callable[any, any]\n"
             "    The function to apply to each element of ``iterable``.\n"
             "iterable : iterable\n"
             "    The sequence to map over.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "it : fused.iterator\n"
             "    An iterator which will apply ``function`` to every element\n"
             "    of ``iterable``.\n");

PyObject* map(PyObject* m, PyObject* args) {
    if (PyTuple_Size(args) != 2) {
        PyErr_Format(PyExc_TypeError,
                     "fused.map() expects 2 arguments, got: %ld",
                     PyTuple_Size(args));
        return nullptr;
    }

    return new_fused<node_kind::map>(get_state(m)->compose,
                                     PyTuple_GET_ITEM(args, 0),
                                     PyTuple_GET_ITEM(args, 1));
}

PyDoc_STRVAR(filter_doc,
             "Lazily filter an iterator with a predicate function.\n"
             "\n"
             "Parameters\n"
             "----------\n"
             "predicate : callable[any, bool]\n"
             "    The predicate to apply to every element of ``iterable``.\n"
             "    If ``predicate(element)`` returns ``True``, the element\n"
             "    will be yielded, otherwise it will be skipped.\n"
             "iterable : iterable\n"
             "    The sequence to filter.\n"
             "\n"
             "Returns\n"
             "-------\n"
             "it : fused.iterator\n"
             "    An iterator which will filter the elements of ``iterable``\n"
             "    based on the results of ``predicate(element)``.\n");

PyObject* filter(PyObject* m, PyObject* args) {
    if (PyTuple_Size(args) != 2) {
        PyErr_Format(PyExc_TypeError,
                     "fused.filter() expects 2 arguments, got: %ld",
                     PyTuple_Size(args));
        return nullptr;
    }

    return new_fused<node_kind::filter>(get_state(m)->compose,
                                        PyTuple_GET_ITEM(args, 0),
                                        PyTuple_GET_ITEM(args, 1));
}

PyMethodDef free_functions[] = {
    {"map", map, METH_VARARGS, map_doc},
    {"filter", filter, METH_VARARGS, filter_doc},
    {nullptr},
};

void module_dealloc(PyObject* m) {
    module_state* st = get_state(m);
    if (st) {
        Py_DECREF(st->compose);
    }
}

PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "gotenks.fused",
    nullptr,
    sizeof(module_state),
    free_functions,
    nullptr,
    nullptr,
    nullptr,
    reinterpret_cast<freefunc>(module_dealloc),
};
}  // namespace gotenks

PyMODINIT_FUNC PyInit_fused() {
    if (PyType_Ready(&gotenks::type) < 0) {
        return nullptr;
    }

    PyObject *m = PyModule_Create(&gotenks::module);

    if (!m) {
        return nullptr;
    }

    // Grab a reference to the `gotenks.compose.compose` function used to
    // compose mapped functions together. This is written in Python because
    // it depends on codetransformer and is less performance critical than
    // the underlying iteration.
    PyObject* module_name = PyUnicode_FromString("gotenks.compose");
    PyObject* compose_module = PyImport_Import(module_name);
    Py_DECREF(module_name);
    if (!compose_module) {
        Py_DECREF(m);
        return nullptr;
    }

    gotenks::get_state(m)->compose  = PyObject_GetAttrString(compose_module,
                                                             "compose");
    Py_DECREF(compose_module);
    if (!gotenks::get_state(m)->compose) {
        Py_DECREF(m);
        return nullptr;
    }

    if (PyObject_SetAttrString(m,
                               "iterator",
                               reinterpret_cast<PyObject*>(&gotenks::type))) {
        Py_DECREF(m);
        return nullptr;
    }

    return m;
}
