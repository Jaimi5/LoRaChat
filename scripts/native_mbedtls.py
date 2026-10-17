"""PlatformIO pre-script of [env:native]: builds the mbedTLS of the ESP-IDF package for the host.

lib/OtaCrypto then runs the same mbedTLS version in the native tests as on the node.
"""
import os
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO)

mbedtls = (Path(env.subst("$PROJECT_PACKAGES_DIR"))  # noqa: F821
           / "framework-espidf" / "components" / "mbedtls" / "mbedtls")
if not (mbedtls / "library").is_dir():
    print(f"Error: mbedTLS not found at {mbedtls}; build an ESP32 env once to install it")
    env.Exit(1)  # noqa: F821

env.Append(CPPPATH=[str(mbedtls / "include")])  # noqa: F821
library = env.BuildLibrary(  # noqa: F821
    os.path.join("$BUILD_DIR", "mbedtls_host"), str(mbedtls / "library"))
env.Prepend(LIBS=[library])  # noqa: F821
