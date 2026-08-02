#!/usr/bin/env python3
"""
Build manager: check prerequisites → backup src/ → install selected method headers 
→ write sleep constants → cmake build → restore src/.
"""
from __future__ import annotations
import argparse
import os
import platform
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

# ---------------------------------------------------------------------------
# Method / target maps
# ---------------------------------------------------------------------------
METHODS = {
    "bypass": {
        "cdp": "methods/bypass_methods/cdp.hpp",
        "windowpos": "methods/bypass_methods/windowpos.hpp",
        "ozone": "methods/bypass_methods/ozone.hpp",
    },
    "launcher": {
        "shellexecuteex": "methods/launchers/windows/ShellExecuteEx.hpp",
        "createprocessw": "methods/launchers/windows/CreateProcessW.hpp",
        "posix_spawn": "methods/launchers/linux/posix_spawn.hpp",
    },
    "terminator": {
        "terminateprocess": "methods/terminators/windows/TerminateProcess.hpp",
        "sigkill": "methods/terminators/linux/sigkill.hpp",
    },
    "sleep": {
        "generic_windows": "methods/sleep/windows/generic.hpp",
        "generic_linux": "methods/sleep/linux/generic.hpp",
        "timer_windows": "methods/sleep/windows/timer.hpp",
        "timer_linux": "methods/sleep/linux/timer.hpp",
    },
}

TARGETS = {
    "bypass": "src/bypass_methods/bypass.hpp",
    "launcher": "src/launchers/launcher.hpp",
    "terminator": "src/terminators/terminator.hpp",
    "sleep": "src/sleep/sleep.hpp",
}

# ---------------------------------------------------------------------------
# BuildManager
# ---------------------------------------------------------------------------
class BuildManager:
    def __init__(
        self,
        termination_method: str,
        launcher_method: str,
        bypass_method: str,
        sleep_method: str,
        sleep_ms: int = 2,
        sleep_jitter: float = 25.0,
    ) -> None:
        self.termination_method = termination_method
        self.launcher_method = launcher_method
        self.bypass_method = bypass_method
        self.sleep_method = sleep_method
        self.sleep_ms = sleep_ms
        self.sleep_jitter = sleep_jitter
        self.backup_path: Path | None = None

    # ------------------------------------------------------------------
    # Prerequisites Check
    # ------------------------------------------------------------------
    def check_prerequisites(self) -> bool:
        """Verifies required tools exist before performing backup or file ops."""
        print("[*] Checking build prerequisites...")
        cmake_path = shutil.which("cmake")
        if not cmake_path:
            print("[-] Prerequisites check failed: 'cmake' was not found on PATH.")
            print("    Please install CMake or ensure it is accessible in your environment.")
            return False
        
        print(f"[+] Found cmake: {cmake_path}")
        return True

    # ------------------------------------------------------------------
    # Backup / restore
    # ------------------------------------------------------------------
    def backup_source(self) -> bool:
        cwd = Path.cwd()
        src_dir = cwd / "src"
        if not src_dir.is_dir():
            print("[-] src/ directory not found")
            return False

        # Proactively clean any leftover from a previous crashed run
        pre = cwd / "src.pre_restore"
        if pre.exists():
            shutil.rmtree(pre, ignore_errors=True)

        backup_root = None
        try:
            backup_root = Path(tempfile.mkdtemp(prefix="backup_"))
            shutil.copytree(src_dir, backup_root / "src")
        except (OSError, shutil.Error) as e:
            print(f"[-] Backup failed: {e}")
            self.backup_path = None
            if backup_root is not None:
                shutil.rmtree(backup_root, ignore_errors=True)
            return False

        self.backup_path = backup_root
        print(f"[+] Source backed up to: {backup_root}")
        return True

    def restore_source(self) -> bool:
        if self.backup_path is None or not self.backup_path.is_dir():
            print("[-] No backup available to restore")
            return False

        backed_src = self.backup_path / "src"
        if not backed_src.is_dir():
            print("[-] Backed-up src/ is missing")
            return False

        cwd = Path.cwd()
        current_src = cwd / "src"
        displaced_src = None

        try:
            if current_src.exists():
                displaced_src = current_src.with_name(current_src.name + ".pre_restore")
                if displaced_src.exists():
                    shutil.rmtree(displaced_src, ignore_errors=True)
                current_src.rename(displaced_src)

            shutil.copytree(backed_src, current_src)
        except (OSError, shutil.Error) as e:
            print(f"[-] Restore failed: {e}")
            # Aggressive recovery: nuke any partial tree, then try to put the original back
            if current_src.exists():
                shutil.rmtree(current_src, ignore_errors=True)
            if displaced_src is not None and displaced_src.exists():
                try:
                    displaced_src.rename(current_src)
                except OSError:
                    pass  # best-effort
            return False

        # Success path
        if displaced_src is not None and displaced_src.exists():
            shutil.rmtree(displaced_src, ignore_errors=True)

        print(f"[+] Source restored from: {self.backup_path}")
        return True

    # ------------------------------------------------------------------
    # Source adjustments
    # ------------------------------------------------------------------
    def adjust_source(self) -> bool:
        try:
            cwd = Path.cwd()
            print(f"[*] Working directory: {cwd}")

            selections = {
                "bypass": self.bypass_method.lower(),
                "launcher": self.launcher_method.lower(),
                "terminator": self.termination_method.lower(),
                "sleep": self.sleep_method.lower(),
            }

            for category, method_key in selections.items():
                if category not in METHODS or method_key not in METHODS[category]:
                    print(f"[-] Unknown {category} method: {method_key}")
                    return False

                src_file = cwd / METHODS[category][method_key]
                dst_file = cwd / TARGETS[category]

                print(f"\n[*] Processing {category} ({method_key})")
                print(f" Source : {src_file}")
                print(f" Target : {dst_file}")

                if not src_file.is_file():
                    print(f"[-] Implementation file not found: {src_file}")
                    return False

                shutil.copy2(src_file, dst_file)
                if dst_file.is_file() and dst_file.stat().st_size == src_file.stat().st_size:
                    print(f"[+] Successfully overwrote → {dst_file}")
                else:
                    print(f"[-] Copy appeared to fail for {dst_file}")
                    return False

            print("\n[+] All method files installed successfully")
            return True
        except (OSError, shutil.Error) as e:
            print(f"[-] Failed to adjust source: {e}")
            return False

    def write_sleep_common(self) -> bool:
        path = Path("src/sleep/sleep_common.h")
        content = (
            "#pragma once\n"
            "\n"
            f"const int SLEEP_MS = {self.sleep_ms};\n"
            f"const float SLEEP_JITTER = {self.sleep_jitter}f;\n"
        )
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        except OSError as e:
            print(f"[-] Failed to write sleep_common.h: {e}")
            return False

        print(f"[+] Wrote {path} → SLEEP_MS={self.sleep_ms}, SLEEP_JITTER={self.sleep_jitter}")
        return True

    # ------------------------------------------------------------------
    # Build
    # ------------------------------------------------------------------
    def build(
        self,
        source_dir: Path = Path("."),
        build_dir: Path = Path("build"),
    ) -> bool:
        try:
            enable_cdp = self.bypass_method.lower() == "cdp"
            self._clean(build_dir)
            return self._run_step(
                "configure",
                [
                    "cmake",
                    "-S",
                    str(source_dir),
                    "-B",
                    str(build_dir),
                    f"-DENABLE_CDP={'ON' if enable_cdp else 'OFF'}",
                    "-DCMAKE_BUILD_TYPE=Release",
                ],
            ) and self._run_step(
                "build",
                ["cmake", "--build", str(build_dir), "--config", "Release"],
            )
        except FileNotFoundError:
            print("[-] cmake is not installed or not on PATH")
            return False

    def _clean(self, build_dir: Path) -> None:
        if build_dir.exists():
            print(f"[+] Removing previous {build_dir}/ directory...")
            try:
                shutil.rmtree(build_dir, onexc=self._clear_readonly_and_retry)
            except OSError as e:
                print(f"[-] Could not remove {build_dir}: {e}")
                print(
                    "[-] Check that no process (editor, terminal, antivirus) "
                    "has a file open in it."
                )
                raise
        build_dir.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def _clear_readonly_and_retry(func, path, exc) -> None:
        os.chmod(path, stat.S_IWRITE)
        func(path)

    def _run_step(self, label: str, cmd: list[str], timeout: int = 1800) -> bool:
        print(f"[+] Running {label}: {' '.join(cmd)}")
        result = subprocess.run(cmd, timeout=timeout)
        if result.returncode != 0:
            print(f"[-] {label} failed with exit code {result.returncode}")
            return False
        return True

    # ------------------------------------------------------------------
    # Orchestration
    # ------------------------------------------------------------------
    def start(self) -> None:
        # Check tool prerequisites before allocating temp dirs or writing headers
        if not self.check_prerequisites():
            return

        try:
            if not self.backup_source():
                print("[-] Failed to backup the original source code")
                return

            if not self.adjust_source():
                print("[-] Failed to adjust source code for user specified options")
                return

            if not self.write_sleep_common():
                print("[-] Failed to adjust sleep settings")
                return

            print("[*] Compiling source code...")
            if not self.build():
                print("[-] Failed to compile source code")
                return

            print("[+] Compiled source code")
        except KeyboardInterrupt:
            print("\n[-] CTRL+C detected; aborting...")
        finally:
            # Only attempt restore if a backup was actually generated
            if self.backup_path is not None:
                try:
                    self.restore_source()
                finally:
                    if self.backup_path.exists():
                        shutil.rmtree(self.backup_path, ignore_errors=True)
                        self.backup_path = None


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_args() -> argparse.Namespace:
    system = platform.system().lower()
    is_linux = system == "linux"

    if is_linux:
        launcher_choices = ["posix_spawn"]
        term_choices = ["sigkill"]
        sleep_choices = ["generic_linux", "timer_linux"]
        default_launcher = "posix_spawn"
        default_term = "sigkill"
        default_sleep = "generic_linux"
    else:
        launcher_choices = ["ShellExecuteEx", "CreateProcessW"]
        term_choices = ["TerminateProcess"]
        sleep_choices = ["generic_windows", "timer_windows"]
        default_launcher = "CreateProcessW"
        default_term = "TerminateProcess"
        default_sleep = "generic_windows"

    print(f"[*] Detected OS: {platform.system()} – only native methods available")

    parser = argparse.ArgumentParser(
        description="Builds and compiles with the selected runtime options"
    )
    parser.add_argument(
        "-T",
        "--termination-method",
        choices=term_choices,
        default=default_term,
        help="Method used to terminate processes (default: %(default)s)",
    )
    parser.add_argument(
        "-L",
        "--launcher-method",
        choices=launcher_choices,
        default=default_launcher,
        help="Method used to launch processes (default: %(default)s)",
    )
    parser.add_argument(
        "-S",
        "--sleep-method",
        choices=sleep_choices,
        default=default_sleep,
        help="Sleep method to use (default: %(default)s)",
    )
    parser.add_argument(
        "-B",
        "--bypass-method",
        choices=["cdp", "windowpos", "ozone"],
        default="cdp",
        help="Bypass method to use (default: %(default)s)",
    )
    parser.add_argument(
        "-M",
        "--sleep-ms",
        type=int,
        default=3300,
        help="Base sleep duration in ms written to sleep_common.h (default: %(default)s)",
    )
    parser.add_argument(
        "-J",
        "--sleep-jitter",
        type=float,
        default=25.0,
        help="Sleep jitter percentage written to sleep_common.h (default: %(default)s)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    manager = BuildManager(
        termination_method=args.termination_method,
        launcher_method=args.launcher_method,
        bypass_method=args.bypass_method,
        sleep_method=args.sleep_method,
        sleep_ms=args.sleep_ms,
        sleep_jitter=args.sleep_jitter,
    )
    manager.start()


if __name__ == "__main__":
    main()