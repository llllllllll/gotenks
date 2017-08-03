#include <optional>
#include <tuple>

#include "gotenks/jit.h"
#include "gotenks/utils.h"

namespace gotenks {
namespace jit {

namespace detail {

/** Construct a `PyObject` struct type in a given context.

    @param ctx The context to build the `PyObject` type in.
    @return A gccjit definition for `PyObject`.
 */
auto build_pyobject_type(gccjit::context& ctx) {
    auto ob_refcnt = ctx.new_field(ctx.get_int_type<Py_ssize_t>(),
                                   "ob_refcnt");
    auto ob_type = ctx.new_field(ctx.get_type(GCC_JIT_TYPE_VOID_PTR),
                                 "ob_type");

#if Py_TRACE_REFS
    // when building for a debug CPython, PyObject has two extra fields which
    // come first
    auto _ob_next = ctx.new_field(ctx.get_type(GCC_JIT_TYPE_VOID_PTR),
                                  "_ob_next");
    auto _ob_prev = ctx.new_field(ctx.get_type(GCC_JIT_TYPE_VOID_PTR),
                                  "_ob_prev");

    std::vector<gccjit::field> fields = {_ob_next,
                                         _ob_prev,
                                         ob_refcnt,
                                         ob_type};
#else
    std::vector<gccjit::field> fields = {ob_refcnt,
                                         ob_type};
#endif

    return std::make_tuple(ctx.new_struct_type("PyObject", fields), ob_refcnt);
}

/** Construct a reference to `PyObject* PyIter_Next(PyObject*)`.

    @param ctx The context to build the function in.
    @param pyobjectptr The `PyObject*` type.
    @return A gccjit definition for the `PyIter_Next` function.
 */
auto build_pyiter_next(gccjit::context& ctx, gccjit::type& pyobjectptr) {
    auto iter = ctx.new_param(pyobjectptr, "iter");
    std::vector<gccjit::param> params = {iter};
    return ctx.new_function(GCC_JIT_FUNCTION_IMPORTED,
                            pyobjectptr,
                            "PyIter_Next",
                            params,
                            false);
}

/** Construct a reference to `PyObject* PyObject_Not(PyObject*)`.

    @param ctx The context to build the function in.
    @param pyobjectptr The `PyObject*` type.
    @return A gccjit definition for the `PyObject_Not` function.
 */
auto build_pyobject_not(gccjit::context& ctx, gccjit::type& pyobjectptr) {
    auto ob = ctx.new_param(pyobjectptr, "ob");
    std::vector<gccjit::param> params = {ob};
    return ctx.new_function(GCC_JIT_FUNCTION_IMPORTED,
                            ctx.get_int_type<int>(),
                            "PyObject_Not",
                            params,
                            false);
}

/** Construct an internal `call_function_one` which calls a Python function
    with one Python argument.

    @param ctx The context to build the function in.
    @param pyobjectptr The `PyObject*` type.
    @return A gccjit definition for an internal function.
 */
auto build_gotenks_call_function_one(gccjit::context& ctx,
                                     gccjit::type& pyobjectptr) {
    auto function = ctx.new_param(pyobjectptr, "function");
    auto arg = ctx.new_param(pyobjectptr, "arg");
    std::vector<gccjit::param> params = {function, arg};
    auto call_function_one = ctx.new_function(GCC_JIT_FUNCTION_INTERNAL,
                                              pyobjectptr,
                                              "call_function_one",
                                              params,
                                              false);

    auto pyssize_t = ctx.get_int_type<Py_ssize_t>();

    auto fastcall_function = ctx.new_param(pyobjectptr, "function");
    auto args = ctx.new_param(pyobjectptr.get_pointer(), "args");
    auto nargs = ctx.new_param(pyssize_t, "nargs");
    auto dict = ctx.new_param(pyobjectptr, "dict");
    std::vector<gccjit::param> fast_call_params = {fastcall_function,
                                                   args,
                                                   nargs,
                                                   dict};
    auto fastcall = ctx.new_function(GCC_JIT_FUNCTION_IMPORTED,
                                     pyobjectptr,
                                     "_PyObject_FastCallDict",
                                     fast_call_params,
                                     false);

    auto body = call_function_one.new_block("body");
    std::vector<gccjit::rvalue> call_args = \
        {function,
         arg.get_address(),
         ctx.one(pyssize_t),
         ctx.new_rvalue(pyobjectptr, nullptr)};
    body.end_with_return(ctx.new_call(fastcall, call_args));

    return call_function_one;
}

/** Construct an inline `Py_DECREF` function.

    @param ctx The context to build the function in.
    @param pyobjectptr The `PyObject*` type.
    @return A gccjit definition for a `Py_DECREF` function.
 */
auto build_py_decref(gccjit::context& ctx,
                     gccjit::type& pyobjectptr,
                     gccjit::field& ob_refcnt) {
    auto ob = ctx.new_param(pyobjectptr, "ob");
    std::vector<gccjit::param> params = {ob};
    auto py_decref = ctx.new_function(GCC_JIT_FUNCTION_INTERNAL,
                                      ctx.get_type(GCC_JIT_TYPE_VOID),
                                      "Py_DECREF",
                                      params,
                                      false);

    auto body = py_decref.new_block("body");

    auto op = ctx.new_param(pyobjectptr, "op");
    std::vector<gccjit::param> dealloc_params = {op};
    auto py_dealloc = ctx.new_function(GCC_JIT_FUNCTION_IMPORTED,
                                       ctx.get_type(GCC_JIT_TYPE_VOID),
                                       "_Py_Dealloc",
                                       dealloc_params,
                                       false);

    auto refcnt = ob.dereference_field(ob_refcnt);
    body.add_assignment_op(refcnt,
                           GCC_JIT_BINARY_OP_MINUS,
                           ctx.one(ctx.get_int_type<Py_ssize_t>()));

    auto if_false = py_dealloc.new_block("if_false");
    if_false.add_eval(py_dealloc(ob));
    if_false.end_with_return();


#if Py_REF_DEBUG
    auto _py_reftotal = ctx.new_global(GCC_JIT_GLOBAL_IMPORTED,
                                       "_Py_RefTotal");
    body.add_assignment_op(_py_reftotal,
                           GCC_JIT_BINARY_OP_PLUS,
                           ctx.one(ctx.get_int_type<Py_ssize_t>()));

    auto file = ctx.new_param(ctx.get_type(GCC_JIT_TYPE_CHARP), "file");
    auto line = ctx.new_param(ctx.get_int_type<int>(), "line");
    auto op1 = ctx.new_param(pyobjectptr, "op");
    std::vector<gccjit::param> negative_refcount_params = {file, line, op1};
    auto py_negative_refcount = ctx.new_function(GCC_JIT_FUNCTION_IMPORTED,
                                                 ctx.get_type(GCC_JIT_TYPE_VOID),
                                                 "_Py_NegativeRefcount",
                                                 negative_refcount_params,
                                                 false);

    auto if_true = py_decref.new_block("if_true");
    auto return_ = py_decref.new_block("return_");
    return_.end_with_return();
    auto negative = py_decref.new_block("negative");
    negative.add_eval(py_negative_refcount("<jit>", 1, ob));
    if_true.end_with_conditional(refcnt < 0,
                                 negative,
                                 return_);
#else
    auto if_true = py_decref.new_block("if_true");
    if_true.end_with_return();
#endif

    body.end_with_conditional(refcnt != 0,
                              if_true,
                              if_false);

    return py_decref;
}
}  // namespace detail

std::optional<next_function> compile(const std::vector<node>& steps){
    auto ctx = gccjit::context::acquire();

    // gccjit types used in the function
    auto [pyobject, ob_refcnt] = detail::build_pyobject_type(ctx);
    auto pyobjectptr = pyobject.get_pointer();
    auto int_ = ctx.get_int_type<int>();
    auto size_t = ctx.get_int_type<std::size_t>();

    gccjit::param functions = ctx.new_param(pyobjectptr.get_pointer(),
                                            "functions");
    gccjit::param num_functions = ctx.new_param(size_t, "num_functions");
    gccjit::param iter = ctx.new_param(pyobjectptr, "iter");

    std::vector<gccjit::param> params = {functions, num_functions, iter};
    auto next = ctx.new_function(GCC_JIT_FUNCTION_EXPORTED,
                                 pyobjectptr,
                                 "next",
                                 params,
                                 false);

    // local variables
    auto element = next.new_local(pyobjectptr, "element");
    auto applied = next.new_local(pyobjectptr, "applied");
    auto should_mask = next.new_local(int_, "should_mask");

    // literal (PyObject*) NULL
    auto null = ctx.new_rvalue(pyobjectptr, nullptr);
    auto int_zero = ctx.zero(int_);
    auto int_one = ctx.one(int_);

    // external functions used
    auto pyiter_next = detail::build_pyiter_next(ctx, pyobjectptr);
    auto call_function = detail::build_gotenks_call_function_one(ctx,
                                                                 pyobjectptr);
    auto py_decref = detail::build_py_decref(ctx, pyobjectptr, ob_refcnt);
    auto pyobject_not = detail::build_pyobject_not(ctx, pyobjectptr);

    // first block
    auto get_next_element = next.new_block("next_element");

    // block that returns NULL
    auto return_null = next.new_block("return_null");
    return_null.end_with_return(null);

    // block to handle when PyIter_Next(iter) is not NULL
    auto have_element = next.new_block("have_element");

    get_next_element.add_assignment(element, pyiter_next(iter));
    get_next_element.end_with_conditional(element == null,
                                          return_null,
                                          have_element);


    // block which decrefs the element and returns NULL
    auto decref_element_return = next.new_block("decref_element_return");
    decref_element_return.add_eval(py_decref(element));
    decref_element_return.end_with_return(null);

    // setup the handlers for each block so we can jump to the next handler;
    // the indices are shifted, so index 0 is the handler for steps[1]
    std::vector<gccjit::block> handle_steps;
    for (const node& _ __attribute__((unused)) : steps) {
        handle_steps.emplace_back(next.new_block("handle_step"));
    }

    auto current_block = have_element;
    std::size_t n = 0;
    for (const node& step : steps) {
        current_block.add_assignment(applied, call_function(functions[n],
                                                            element));
        auto after_applied = next.new_block("after_applied");
        current_block.end_with_conditional(applied == null,
                                           decref_element_return,
                                           after_applied);

        auto& next_block = handle_steps[n++];
        switch (step.kind()) {
        case node_kind::map:
            after_applied.add_eval(py_decref(element));
            after_applied.add_assignment(element, applied);
            after_applied.end_with_jump(next_block);
            break;
        case node_kind::filter:
            after_applied.add_assignment(should_mask, pyobject_not(applied));
            after_applied.add_eval(py_decref(applied));

            auto decref_get_next = next.new_block("decref_get_next");
            decref_get_next.add_eval(py_decref(element));
            decref_get_next.end_with_jump(get_next_element);

            std::vector<gccjit::case_> cases = {ctx.new_case(int_zero,
                                                             int_zero,
                                                             next_block),
                                                ctx.new_case(int_one,
                                                             int_one,
                                                             decref_get_next)};
            after_applied.end_with_switch(should_mask,
                                          decref_element_return,
                                          cases);

        }
        current_block = next_block;
    }

    current_block.end_with_return(element);

    ctx.set_int_option(GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, 2);
    ctx.set_bool_allow_unreachable_blocks(true);

    // uncomment to get c-like source written for each jitted function
    // ctx.dump_to_file("jitted-src.c", true);

    gcc_jit_result* result = nullptr;
    try {
        result = ctx.compile();
    }
    catch (const gccjit::error&) {
        PyErr_Format(PyExc_ValueError,
                     "Failed to compile: %s",
                     gcc_jit_context_get_first_error(ctx.get_inner_context()));
        return std::nullopt;
    }

    return next_function(result, steps);
}

}  // namespace jit
}  // namespace gotenks
