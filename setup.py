#!/usr/bin/env python
from setuptools import setup, Extension


with open('README.rst') as f:
    long_description = f.read()


cflags = [
    '-Wall',
    '-Wextra',
    '-Wno-missing-field-initializers',
    '-std=gnu++17',
]

setup(
    name='gotenks',
    version='0.1.0',
    description='Stream fusion for Python.',
    author='Joe Jevnik',
    author_email='joejev@gmail.com',
    packages=['gotenks'],
    include_package_data=True,
    long_description=long_description,
    license='LGPL-3',
    classifiers=[
        'Development Status :: 3 - Alpha',
        'License :: OSI Approved :: Lesser GNU General Public License v3 (LGPLv3)',  # noqa
        'Intended Audience :: Developers',
        'Natural Language :: English',
        'Programming Language :: Python :: 3 :: Only',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: Implementation :: CPython',
        'Topic :: Software Development',
        'Topic :: Utilities',
    ],
    url='https://github.com/llllllllll/gotenks',
    ext_modules=[
        Extension(
            'gotenks.fused',
            ['gotenks/fused.cc'],
            language='c++',
            extra_compile_args=cflags,
        ),
    ],
    install_requires=[
        'codetransformer',
    ],
    extras_require={
        'dev': [
            'flake8==3.4.1',
            'pytest==3.1.3',
        ],
    },
)
