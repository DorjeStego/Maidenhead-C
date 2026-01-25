import os
import sys
import shutil
import subprocess
from ctypes.util import find_library

from setuptools import Extension, setup
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.build_ext import build_ext as _build_ext

ROOT = os.path.abspath(os.path.dirname(__file__))

native_sources = [
    "src/maidenhead/_native/maidenhead_native.c",
    "src/maidenhead/_native/core.c",
    "src/maidenhead/_native/geo.c",
    "src/maidenhead/_native/coverage.c",
    "src/maidenhead/_native/geojson.c",
]

include_dirs = ["src/maidenhead/_native"]
libraries = []
library_dirs = []
define_macros = []
extra_compile_args = []
language = None

geo_root = os.environ.get("GEOGRAPHICLIB_DIR")
if geo_root:
    include_dirs.append(os.path.join(geo_root, "include"))
    library_dirs.append(os.path.join(geo_root, "lib"))

simdjson_root = os.environ.get("SIMDJSON_DIR")
if simdjson_root:
    include_dirs.append(os.path.join(simdjson_root, "include"))
    library_dirs.append(os.path.join(simdjson_root, "lib"))

conda_prefix = os.environ.get("CONDA_PREFIX")
if conda_prefix:
    include_dirs.append(os.path.join(conda_prefix, "include"))
    library_dirs.append(os.path.join(conda_prefix, "lib"))

# Fallback to the active Python prefix for build-isolation cases.
include_dirs.append(os.path.join(sys.prefix, "include"))
library_dirs.append(os.path.join(sys.prefix, "lib"))

lib_geo = find_library("GeographicLib")
geo_header = None
for inc in include_dirs:
    candidate = os.path.join(inc, "GeographicLib", "Geodesic.hpp")
    if os.path.exists(candidate):
        geo_header = candidate
        break
if lib_geo and geo_header:
    native_sources.append("src/maidenhead/_native/geo_geodesic.cpp")
    libraries.append("GeographicLib")
    define_macros.append(("MH_HAVE_GEODESIC", "1"))
    language = "c++"

lib_simdjson = find_library("simdjson")
simdjson_header = None
for inc in include_dirs + ["/usr/include", "/usr/local/include"]:
    candidate = os.path.join(inc, "simdjson.h")
    if os.path.exists(candidate):
        simdjson_header = candidate
        break
simdjson_available = bool(lib_simdjson and simdjson_header)
if simdjson_available:
    native_sources.append("src/maidenhead/_native/json_simdjson.cpp")
    libraries.append("simdjson")
    define_macros.append(("MH_HAVE_SIMDJSON", "1"))
    language = "c++"

if language == "c++" and os.name != "nt":
    # Avoid passing C++ flags to C sources; rely on compiler default for C++.
    pass

ext_modules = [
    Extension(
        "maidenhead._native",
        sources=native_sources,
        include_dirs=include_dirs,
        libraries=libraries,
        library_dirs=library_dirs,
        define_macros=define_macros,
        extra_compile_args=extra_compile_args,
        language=language,
    )
]

class build_py(_build_py):
    def run(self):
        super().run()
        self._build_native_cli()

    def _build_native_cli(self):
        cmake = shutil.which("cmake")
        if not cmake:
            print("cmake not found; skipping mh_cli build")
            return
        build_cmd = self.get_finalized_command("build")
        build_temp = getattr(build_cmd, "build_temp", None)
        if not build_temp:
            print("build_temp not available; skipping mh_cli build")
            return
        build_dir = os.path.join(build_temp, "mh_cli")
        os.makedirs(build_dir, exist_ok=True)
        subprocess.check_call([cmake, "-S", ROOT, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"])
        subprocess.check_call([cmake, "--build", build_dir, "--target", "mh_cli", "-j", "2"])
        exe_name = "mh_cli.exe" if os.name == "nt" else "mh_cli"
        src_bin = os.path.join(build_dir, exe_name)
        if not os.path.exists(src_bin):
            print("mh_cli not produced; skipping copy")
            return
        dest_dir = os.path.join(self.build_lib, "maidenhead", "bin")
        os.makedirs(dest_dir, exist_ok=True)
        dest_bin = os.path.join(dest_dir, exe_name)
        shutil.copy2(src_bin, dest_bin)
        if os.name != "nt":
            os.chmod(dest_bin, 0o755)

class build_ext(_build_ext):
    def run(self):
        if not simdjson_available:
            raise RuntimeError(
                "simdjson is required to build the native extension; "
                "install system simdjson dev packages or set SIMDJSON_DIR"
            )
        super().run()

setup(ext_modules=ext_modules, cmdclass={"build_py": build_py, "build_ext": build_ext})
