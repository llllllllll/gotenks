from itertools import chain
from types import FunctionType, BuiltinMethodType, BuiltinFunctionType

from codetransformer import Code, CodeTransformer, pattern
from codetransformer.instructions import (
    CALL_FUNCTION,
    DELETE_DEREF,
    DELETE_GLOBAL,
    DELETE_NAME,
    JUMP_ABSOLUTE,
    LOAD_CLOSURE,
    LOAD_CONST,
    LOAD_DEREF,
    LOAD_FAST,
    LOAD_GLOBAL,
    LOAD_NAME,
    RETURN_VALUE,
    ROT_TWO,
    STORE_DEREF,
    STORE_FAST,
    STORE_GLOBAL,
    STORE_NAME,
)


class InlineTransformer(CodeTransformer):
    def __init__(self, next_instr, argname, first):
        super().__init__()
        self._next_instr = next_instr
        self._argname = argname
        self._first = first

    @pattern(LOAD_FAST)
    def _loadfast(self, instr):
        if (not self._first and
                instr.arg == self._argname
                and instr is self.code.instrs[0]):
            # If this is is not the first code object and
            # the first instruction is LOAD_FAST, just
            # ignore it.
            return

        yield instr

    @pattern(RETURN_VALUE)
    def _return(self, instr):
        if self._next_instr is None:
            # Actually just return if this is the last code object.
            yield instr
            return

        stolen = False
        next_instr = self._next_instr
        if not (isinstance(next_instr, LOAD_FAST) and
                next_instr.arg == self._argname):
            # Only store if we are not going to loading
            # the value immediately.
            stolen = True
            yield STORE_FAST(self._argname).steal(instr)

        if instr is not self.code.instrs[-1]:
            # Only jump if we are not the last instruction.
            jmp = JUMP_ABSOLUTE(self._next_instr)
            if not stolen:
                jmp.steal(instr)
            yield jmp

    def transform_varnames(self, varnames):
        return (self._argname,) + varnames[1:]


def can_inline(code):
    """Checks if we can inline the given function.

    Parameters
    ----------
    code : Code
        The code object.

    Returns
    -------
    g : bool
        Can code be inlined?
    """
    for c in code.instrs:
        # These are closures or interact with the globals.
        if isinstance(c, (LOAD_GLOBAL,
                          STORE_GLOBAL,
                          DELETE_GLOBAL,
                          DELETE_NAME,
                          LOAD_NAME,
                          STORE_NAME,
                          LOAD_DEREF,
                          STORE_DEREF,
                          LOAD_CLOSURE,
                          DELETE_DEREF)):
            return False

    return True


def call_function(fn):
    """Return the instructions needed to call fn.

    Parameters
    ----------
    fn : function
        The function to call.

    Returns
    -------
    instrs : tuple
        The instructions to use.
    """
    return LOAD_CONST(fn), ROT_TWO(), CALL_FUNCTION(1)


def extract_code(n, *, _tried_call=False):
    """Extract a Code object from a callable.

    Parameters
    ----------
    n : callable
        The callable to extract code from.

    Returns
    code : Code
        The code object.
    """
    if isinstance(n, FunctionType):
        return Code.from_pycode(n.__code__)
    if isinstance(n, (BuiltinFunctionType, BuiltinMethodType)):
        return None

    if _tried_call:
        # Use this because the `__call__` attribute will probable
        # also have a `__call__` that might be the same.
        return None

    try:
        call = n.__call__
    except AttributeError:
        raise TypeError('{n} is not callable'.format(n=n))

    return extract_code(call, _tried_call=True)


def compose(*fs):
    """Compose functions together.

    Parameters
    ----------
    fs: *functions
        The functions to compose.


    Returns
    -------
    composed : function
        The compositions of all of the functions.
    """
    if not fs:
        return lambda n: n

    if len(fs) == 1:
        return fs[0]

    try:
        name = '_of_'.join(f.__name__ for f in fs)
    except AttributeError:
        name = 'composed'

    fs = tuple(reversed(fs))
    cs = tuple(map(extract_code, fs))
    argname = cs[0].argnames[0] if cs[0] is not None else 'n'
    new_instrs = []
    append_instrs = new_instrs.append
    first_func = fs[0]
    last_func = fs[-1]
    first_code = cs[0]
    next_instr = None
    for f, c in zip(fs[::-1], cs[::-1]):
        if c is not None and can_inline(c):
            instrs = InlineTransformer(
                next_instr,
                argname=argname,
                first=c is first_code,
            ).transform(c).instrs
            next_instr = c.instrs[0]
        else:
            instrs = call_function(f)
            if f is first_func:
                instrs = (LOAD_FAST(argname),) + instrs
            elif f is last_func:
                instrs += (RETURN_VALUE(),)
            next_instr = LOAD_FAST(argname)

        append_instrs(instrs)

    try:
        defaults = fs[0].__defaults__
    except AttributeError:
        defaults = ()

    return FunctionType(
        Code(
            chain.from_iterable(reversed(new_instrs)),
            first_code.argnames if first_code is not None else ('n',),
            name=name,
        ).to_pycode(),
        {},
        name,
        defaults,
        sum((getattr(f, '__closure__', None) or () for f in fs), ()),
    )
