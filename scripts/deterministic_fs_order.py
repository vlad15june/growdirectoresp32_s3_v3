# Pre-script of the source-built environment: pin filesystem enumeration order.
#
# The espidf builder assembles the final link line by iterating CMake's
# file-API reply directory and the framework component list with os.listdir(),
# whose order the OS does not define. On APFS two identical clean builds can
# enumerate the same files differently; the component libraries then change
# places on the link line and the same code lands at different addresses -
# equal builds, unequal bytes. Sorting every listing makes enumeration, and
# with it the link line, a function of the file set alone. A sorted listing is
# still a correct listing: this pins the order things are assembled in, not
# what is assembled.
#
# Runs as a `pre:` script so the patch is in place before the framework
# builder module executes its module-level os.listdir() calls.

import os

_original_listdir = os.listdir


def _sorted_listdir(path="."):
    return sorted(_original_listdir(path))


if os.listdir is not _sorted_listdir:
    os.listdir = _sorted_listdir
