"""Exercise native loader lifetime without invoking SDK entry points."""

import _ctypes
import ctypes
import os
import ssl
import sys

for _ in range(10):
    plugin = ctypes.CDLL(sys.argv[1])
    assert getattr(plugin, "FourDPackex"), "Missing 4D entry point"
    try:
        getattr(plugin, "OPENSSL_init_ssl")
    except AttributeError:
        pass
    else:
        raise AssertionError("Private OpenSSL symbol was exported")
    if os.name == "nt":
        _ctypes.FreeLibrary(plugin._handle)
    else:
        _ctypes.dlclose(plugin._handle)
    del plugin
    ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)

print("PASS native load/unload and private TLS symbols")
