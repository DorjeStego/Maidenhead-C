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

simdjson_root = os.environ.get("SIMDJSON_DIR")
if simdjson_root:
    include_dirs.append(os.path.join(simdjson_root, "include"))
    library_dirs.append(os.path.join(simdjson_root, "lib"))

sleef_root = os.environ.get("SLEEF_DIR")
if sleef_root:
    include_dirs.append(os.path.join(sleef_root, "include"))
    library_dirs.append(os.path.join(sleef_root, "lib"))

conda_prefix = os.environ.get("CONDA_PREFIX")
if conda_prefix:
    include_dirs.append(os.path.join(conda_prefix, "include"))
    library_dirs.append(os.path.join(conda_prefix, "lib"))

# Fallback to the active Python prefix for build-isolation cases.
include_dirs.append(os.path.join(sys.prefix, "include"))
library_dirs.append(os.path.join(sys.prefix, "lib"))

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

lib_sleef = find_library("sleef")
sleef_header = None
for inc in include_dirs + ["/usr/include", "/usr/local/include"]:
    candidate = os.path.join(inc, "sleef.h")
    if os.path.exists(candidate):
        sleef_header = candidate
        break
sleef_available = bool(lib_sleef and sleef_header)
if sleef_available:
    libraries.append("sleef")
    define_macros.append(("MH_HAVE_SIMD_MATH", "1"))
    # SLEEF is a SIMD math backend; the geodesic SIMD hooks remain opt-in at runtime.

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
        if os.path.isdir(build_dir):
            shutil.rmtree(build_dir, ignore_errors=True)
        os.makedirs(build_dir, exist_ok=True)
        subprocess.check_call([cmake, "-S", ROOT, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release"])
        subprocess.check_call([cmake, "--build", build_dir, "--target", "mh_cli", "-j", "2", "--clean-first"])
        exe_name = "mh_cli.exe" if os.name == "nt" else "mh_cli"
        src_bin = os.path.join(build_dir, exe_name)
        if not os.path.exists(src_bin):
            print("mh_cli not produced; skipping copy")
            return
        dest_dir = os.path.join(self.build_lib, "maidenhead", "bin")
        os.makedirs(dest_dir, exist_ok=True)
        dest_bin = os.path.join(dest_dir, exe_name)
        if os.path.exists(dest_bin):
            os.remove(dest_bin)
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
        script = os.path.join(ROOT, "scripts", "build_native_ext.sh")
        preferred_python = "/home/dorje/miniforge3/envs/WSPR/bin/python"
        python_bin = preferred_python if os.path.exists(preferred_python) else sys.executable
        env = os.environ.copy()
        env["PYTHON_BIN"] = python_bin
        if sleef_available:
            env.setdefault("WITH_SLEEF_SIMD", "1")
        subprocess.check_call([script], env=env, cwd=ROOT)

        # Stage the CMake-built extension into the build output directory.
        import sysconfig

        ext_suffix = sysconfig.get_config_var("EXT_SUFFIX") or ""
        if not ext_suffix:
            raise RuntimeError("Could not determine Python extension suffix for build")
        src_so = os.path.join(ROOT, "src", "maidenhead", f"_native{ext_suffix}")
        if not os.path.exists(src_so):
            raise RuntimeError(f"Native extension not found at {src_so}")
        dest_dir = os.path.join(self.build_lib, "maidenhead")
        os.makedirs(dest_dir, exist_ok=True)
        shutil.copy2(src_so, os.path.join(dest_dir, os.path.basename(src_so)))

setup(ext_modules=ext_modules, cmdclass={"build_py": build_py, "build_ext": build_ext})
