from gotenks import fused


def test_simple_map():
    def f(a):
        return a + 1

    seq = [1, 2, 3, 4]
    fused_it = fused.map(f, seq)
    it = map(f, seq)

    assert list(fused_it) == list(it)


def test_simple_filter():
    def p(a):
        return a > 2

    seq = [1, 2, 3, 4]
    fused_it = fused.filter(p, seq)
    it = filter(p, seq)

    assert list(fused_it) == list(it)


def test_map_of_filter():
    def f(a):
        return a + 1

    def p(a):
        return a > 2

    seq = [1, 2, 3, 4]
    fused_it = fused.map(f, fused.filter(p, seq))
    it = map(f, filter(p, seq))

    assert list(fused_it) == list(it)

    assert fused_it.steps() == [
        ('filter', p),
        ('map', f),
    ]


def test_filter_of_map():
    def f(a):
        return a + 1

    def p(a):
        return a > 2

    seq = [1, 2, 3, 4]
    fused_it = fused.filter(p, fused.map(f, seq))
    it = filter(p, map(f, seq))

    assert list(fused_it) == list(it)

    assert fused_it.steps() == [
        ('map', f),
        ('filter', p),
    ]


def test_map_composes():
    def f(a):
        return a + 1

    def g(a):
        return a * 2

    seq = [1, 2, 3, 4]
    fused_it = fused.map(f, fused.map(g, seq))
    it = map(f, map(g, seq))

    assert list(fused_it) == list(it)

    steps = fused_it.steps()
    assert len(steps) == 1
    kind, function = steps[0]
    assert kind == 'map'
    assert function is not f
    assert function is not g


def test_to_list():
    def f(a):
        return a + 1

    def g(a):
        return a * 2

    def make_iterator():
        return fused.map(f, fused.map(g, [1, 2, 3, 4]))

    assert make_iterator().to_list() == list(make_iterator())
