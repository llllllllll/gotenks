``gotenks``
===========

Stream fusion for Python.

Problem
-------

``map(f, filter(p, map(g, ...)))``
`````````````````````````

When writing Python in a functional style, we often end up chaining together
maps and filters. For example:

.. code-block:: python

   iterator = map(f, filter(p, map(g, ...)))

When we want to get any elements out of this, we need to dig through three calls
to ``__next__`` to get a single element:

- ``next(iterator)``
- calls ``next(filter(p, ...))``
- calls ``next(map(g, ...))``
- calls ``next(...)``


If we were to write this same code as a bespoke generator function, we would
write:

.. code-block:: python

   def iterator(input_iterator):
       for element in input_iterator:
           element = g(element):
           if not p(element):
               continue
           yield f(element)

In this hand-optimized code we only have a single call to
``next(input_iterator)`` per yielded element.

``map(f, map(g, ...))``
```````````````````````

When writing Python in a functional style, we often map multiple functions over
the same iterator For example:

.. code-block:: python

   iterator = map(f, map(g, ...))

In Python, if we want to get the first element out of the iterator we need to
dig through two calls to ``__next__`` just to get one element:

- ``next(iterator)``
- calls ``next(map(g, ...))``
- calls ``next(...)``

If we were to write this same code as a bespoke generator function, we would
write:

.. code-block:: python

   def iterator(input_iterator):
       for element in input_iterator:
           yield f(g(element))

In this hand-optimized code we only have a single call to
``next(input_iterator)`` per yielded element.

``gotenks.fused.iterator``
--------------------------

``gotenks`` attempts to solve this problem by implementing a new iterator type
which knows about ``map`` and ``filter`` intrinsically.

A fused iterator is a normal Python iterator paired with a sequence of "steps",
which are either functions to map over the underlying iterator or predicates to
filter with.

For example, we can replace a normal map with ``fused.map`` to create a new
fused iterator:

.. code-block:: python

   >>> def f(a):
   ...     return a + 1
   ...
   >>> iterator = fused.map(f, [1, 2, 3, 4])
   >>> iterator
   <gotenks.fused.iterator object at 0x7f98778568a0>

We can inspect the steps that this iterator will perform by calling the
``fused.iterator.steps()`` method:

.. code-block:: python

   >>> iterator.steps()
   [('map', <function f at 0x7f98778d0f28>)]

This says that on each element we will map the ``f`` function over the sequence
and yield the value. This produces the same results we would expect from the
built in map:

.. code-block:: python

   >>> list(iterator)
   [2, 3, 4, 5]


If can also use a ``filter`` instead of a map:

.. code-block:: python

   >>> def p(a):
   ...     return a > 2
   ...
   >>> iterator = fused.filter(p, [1, 2, 3, 4])
   >>> iterator.steps()
   [('filter', <function p at 0x7f98778a16a8>)]
   >>> list(iterator)
   [3, 4]

Chaining
````````

Replacing a single ``map`` or ``filter`` with ``fused.map`` or ``fused.filter``
respectively is not that interesting. Where ``gotenks`` is useful is when we
chain them, for example:

.. code-block:: python

   >>> from gotenks import fused
   >>> def f(a):
   ...     return a + 1
   ...
   >>> def p(a):
   ...     return a > 2
   ...
   >>> iterator = fused.map(f, fused.filter(p, [1, 2, 3, 4]))
   >>> iterator.steps()
   [('filter', <function p at 0x7fe8fff336a8>),
    ('map', <function f at 0x7fe8fff62f28>)]

This says that for each element, we will first filter with the predicate ``p``,
then map with the function ``f``. By not chaining through multiple iterators'
``__next__`` methods, we can better optimize this sequence and iterate over
these elements faster.

Composing
`````````

The following calls are equivalent:

.. code-block:: python

   map(f, map(g, ...))
   map(compose(f, g), ...)

We can use this property to optimize ``fused.map`` by folding functions
together. For example:

.. code-block:: python

   >>> from gotenks import fused
   >>> def f(a):
   ...     return a + 1
   ...
   >>> def g(a):
   ...     return a * 2
   ...
   >>> iterator = fused.map(f, fused.map(g, [1, 2, 3, 4]))
   >>> iterator.steps()
   [('map', <function f_of_g at 0x7f35aacb7a60>)]
   >>> list(iterator)
   [3, 5, 7, 9]

Even though we made two calls to ``fused.map``, the resulting iterator only has
a single ``map`` step which is ``f_of_g``.

We can inspect the body of our new ``f_of_g`` using the standard library ``dis``
module:

.. code-block:: python

   >>> f_of_g = iterator.steps()[0][1]
   >>> import dis
   >>> dis.dis(f_of_g)
     1           0 LOAD_FAST                0 (a)
                 2 LOAD_CONST               0 (2)
                 4 BINARY_MULTIPLY
                 6 LOAD_CONST               1 (1)
                 8 BINARY_ADD
                10 RETURN_VALUE


Here we see the instructions for ``f`` and ``g`` have been merged to create a
single function which is functionally equivalent to ``f(g(a))``.

License
-------

``gotenks`` is dual licensed under the terms of the LGPLv3 and the GPLv2. You
may choose to use ``gotenks`` under the terms of either of these two licenses.
