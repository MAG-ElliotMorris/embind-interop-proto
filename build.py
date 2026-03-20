#!/usr/bin/env python3
"""
Build pipeline for CSP WebAssembly bindings.

Steps:
  1. Run CSP's premake + emscripten make build inside Docker (produces .o intermediaries)
  2. Archive CSP's intermediate .o files into libCSP_wasm.a via emar (inside Docker)
  3. Configure and build the embind bindings with CMake inside Docker
"""

import argparse
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
CSP_ROOT = SCRIPT_DIR / "csp"
EMSDK_VERSION_FILE = CSP_ROOT / "Tools" / "Emscripten" / "emsdk_version.txt"


def read_emsdk_version() -> str:
    return EMSDK_VERSION_FILE.read_text().strip()


def docker_run(emsdk_version: str, command: str, workdir: str = "/src"):
    """Run a command inside the emscripten Docker container with the project root mounted."""
    cmd = [
        "docker", "run",
        "-w", workdir,
        "-v", f"{SCRIPT_DIR}:/src",
        "--rm",
        f"emscripten/emsdk:{emsdk_version}",
        "bash", "-c", command,
    ]
    print(f"\n>>> docker run: {command}")
    subprocess.run(cmd, check=True)


def step_build_csp(emsdk_version: str, config: str):
    """Step 1: Run CSP's premake-generated Makefile inside Docker to compile all .o files."""
    print("\n=== Step 1: Build CSP (premake/make) ===")

    premake_bin = CSP_ROOT / "modules" / "premake" / "bin" / "release" / "premake5"
    if not premake_bin.exists():
        premake_bin = CSP_ROOT / "modules" / "premake" / "bin" / "release" / "premake5.exe"
    if not premake_bin.exists():
        sys.exit(f"ERROR: premake5 not found at {premake_bin}. Bootstrap it first.")

    makefile = CSP_ROOT / "Makefile"
    if makefile.exists():
        makefile.unlink()

    subprocess.run(
        [str(premake_bin), "gmake2", "--generate_wasm"],
        check=True,
        cwd=CSP_ROOT,
    )

    replace_script = CSP_ROOT / "Tools" / "Emscripten" / "ReplaceComSpec.py"
    subprocess.run([sys.executable, str(replace_script)], check=True, cwd=CSP_ROOT)

    docker_run(emsdk_version, f"emmake make config={config}_wasm clean", workdir="/src/csp")
    docker_run(emsdk_version, f"emmake make -j 8 config={config}_wasm", workdir="/src/csp")


def step_archive_csp(emsdk_version: str, config: str):
    """Step 2: Archive CSP's intermediate .o files into libCSP_wasm.a using emar."""
    print("\n=== Step 2: Archive .o -> libCSP_wasm.a ===")

    config_dir = "Debug" if config == "debug" else "Release"
    obj_dir = f"csp/Library/Intermediate/wasm/{config_dir}"

    docker_run(
        emsdk_version,
        f"emar rcs csp/libCSP_wasm.a {obj_dir}/*.o",
    )

    print("Created: csp/libCSP_wasm.a")


def step_build_embind(emsdk_version: str, config: str):
    """Step 3: Configure and build the embind bindings with CMake inside Docker."""
    print("\n=== Step 3: Build embind bindings (CMake) ===")

    cmake_build_type = "Debug" if config == "debug" else "Release"

    docker_run(
        emsdk_version,
        f"emcmake cmake -B build -S . "
        f"-DCMAKE_BUILD_TYPE={cmake_build_type} "
        f"-DCSP_BUILD_CONFIG={cmake_build_type}",
    )

    docker_run(
        emsdk_version,
        "emmake cmake --build build -j 8",
    )

    print("Output: build/csp_embind.js + build/csp_embind.wasm")


def main():
    parser = argparse.ArgumentParser(description="Build CSP WASM embind bindings")
    parser.add_argument(
        "config",
        nargs="?",
        default="debug",
        choices=["debug", "release"],
        help="Build configuration (default: debug)",
    )
    parser.add_argument(
        "--skip-csp",
        action="store_true",
        help="Skip the CSP library build (use existing intermediaries)",
    )
    args = parser.parse_args()

    emsdk_version = read_emsdk_version()
    print(f"Emscripten SDK version: {emsdk_version}")
    print(f"Configuration: {args.config}")

    if not args.skip_csp:
        step_build_csp(emsdk_version, args.config)

    step_archive_csp(emsdk_version, args.config)
    step_build_embind(emsdk_version, args.config)

    print("\n=== Done! ===")


if __name__ == "__main__":
    main()
