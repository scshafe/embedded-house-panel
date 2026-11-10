"""The x86_64 verification image installs every library the host build asks CMake for.

`make verify-linux-x86_64` builds scripts/verification/linux-x86_64.Dockerfile and runs
`make verify` in it. A `find_package` or `pkg_check_modules` added to the tree without its
package in the image fails there at configure time, long after the change merged. This
test maps each dependency to the apt package that provides it, so a new dependency fails
here until the map and the image both name it.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DOCKERFILE = ROOT / "scripts/verification/linux-x86_64.Dockerfile"

#: CMake package or pkg-config module -> the apt package in the image that provides it.
PROVIDED_BY = {
    "find_package:CURL": "libcurl4-openssl-dev",
    "find_package:OpenSSL": "libssl-dev",
    "find_package:PNG": "libpng-dev",
    "find_package:PkgConfig": "pkg-config",
    "find_package:Python3": "python3",
    "find_package:Threads": "build-essential",
    "find_package:ZLIB": "zlib1g-dev",
    "find_package:nlohmann_json": "nlohmann-json3-dev",
    "pkg-config:alsa": "libasound2-dev",
    "pkg-config:libdrm": "libdrm-dev",
}
#: Dependencies the image does not install from apt, and why.
NOT_FROM_APT = {
    "pkg-config:lvgl": "built from the pinned source archive in the image itself",
    "pkg-config:sdl2": "only BHP_BUILD_PANEL_SIMULATOR (the native SDL2 window) asks for it; no verify preset sets it",
}
SKIPPED_DIRECTORIES = {".build", ".git", "output", "downloads", "buildroot", "_deps"}


def cmake_files() -> list[Path]:
    return [path for path in ROOT.rglob("*")
            if path.is_file() and (path.name == "CMakeLists.txt" or path.suffix == ".cmake")
            and not SKIPPED_DIRECTORIES.intersection(path.relative_to(ROOT).parts)]


def cmake_dependencies() -> dict[str, list[str]]:
    """Each dependency and the files that ask for it."""
    found: dict[str, list[str]] = {}
    for path in cmake_files():
        text = re.sub(r"#[^\n]*", "", path.read_text(encoding="utf-8"))
        names = [f"find_package:{name}" for name in re.findall(r"find_package\(\s*([A-Za-z0-9_]+)", text)]
        for arguments in re.findall(r"pkg_(?:check|search)_modules?\(([^)]*)\)", text):
            words = arguments.split()[1:]
            names += [f"pkg-config:{re.split(r'[<>=]', word.strip('\"'))[0]}" for word in words
                      if word not in {"REQUIRED", "QUIET", "IMPORTED_TARGET", "GLOBAL", "NO_CMAKE_PATH",
                                      "NO_CMAKE_ENVIRONMENT_PATH"}]
        for name in names:
            found.setdefault(name, []).append(str(path.relative_to(ROOT)))
    return found


def apt_packages() -> set[str]:
    text = DOCKERFILE.read_text(encoding="utf-8")
    block = re.search(r"apt-get install --yes --no-install-recommends(.*?)&& rm -rf /var/lib/apt/lists", text, re.S)
    if block is None:
        raise AssertionError("the Dockerfile's apt-get install block was not found")
    return set(re.findall(r"^\s*([a-z0-9][a-z0-9.+-]*)\s*\\?$", block[1], re.M))


class HostDependencyTest(unittest.TestCase):
    def test_every_cmake_dependency_is_known(self) -> None:
        unknown = {name: files for name, files in cmake_dependencies().items()
                   if name not in PROVIDED_BY and name not in NOT_FROM_APT}
        self.assertEqual(unknown, {}, "add each new dependency to PROVIDED_BY and the Dockerfile, "
                                      "or to NOT_FROM_APT with the reason")

    def test_the_image_installs_each_provider(self) -> None:
        installed = apt_packages()
        self.assertIn("cmake", installed, "the parser read the install block")
        missing = {name: package for name, package in PROVIDED_BY.items() if package not in installed}
        self.assertEqual(missing, {}, "linux-x86_64.Dockerfile must install these")

    def test_lvgl_is_built_in_the_image(self) -> None:
        self.assertIn("pkg-config --modversion lvgl", DOCKERFILE.read_text(encoding="utf-8"))

    def test_the_map_has_no_stale_entries(self) -> None:
        stale = set(PROVIDED_BY) | set(NOT_FROM_APT)
        self.assertEqual(stale - set(cmake_dependencies()), set(), "remove entries nothing asks for")


if __name__ == "__main__":
    unittest.main()
