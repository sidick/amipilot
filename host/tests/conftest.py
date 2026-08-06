"""Enables pytest's built-in `pytester` fixture (needed by
test_pytest_plugin.py to run small pytest suites in-process against
amipilot.pytest_plugin) and puts the package on sys.path the same way
the plain-unittest test files do by hand."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

pytest_plugins = ["pytester"]
