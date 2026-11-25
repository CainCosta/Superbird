#!/usr/bin/env python3
"""Make an installed Superbird.app portable, re-sign it, and check it's complete.

Usage: macos-bundle.py <Superbird.app> <build dir> <main entitlements plist>

`cmake --install --strip` leaves absolute build-tree rpaths in the bundled dylibs and plugins, and invalidates the
build-time ad-hoc signatures. This strips the absolute rpaths, points the executables at Contents/lib, re-signs
inside-out (helpers with their own entitlements), verifies every signature, and fails if any library a binary loads
through @rpath is missing from the bundle. Used by CI and by scripts/install.sh.
"""
import subprocess, sys
from pathlib import Path

app = Path(sys.argv[1])
build_dir = Path(sys.argv[2])
main_entitlements = Path(sys.argv[3])
macho_magics = (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def is_macho(p: Path) -> bool:
    if p.is_symlink() or not p.is_file():
        return False
    with p.open("rb") as f:
        return f.read(4) in macho_magics

def rpaths(p: Path) -> list[str]:
    out = subprocess.check_output(["otool", "-l", str(p)], text=True)
    res, flag = [], False
    for line in out.splitlines():
        s = line.strip()
        if s == "cmd LC_RPATH":
            flag = True
        elif flag and s.startswith("path "):
            res.append(s[len("path "):].split(" (offset")[0])
            flag = False
    return res

contents = app / "Contents"
for subdir in ("lib", "PlugIns", "Frameworks"):
    d = contents / subdir
    if not d.is_dir():
        continue
    for p in sorted(d.rglob("*")):
        if not is_macho(p):
            continue
        p.chmod(p.stat().st_mode | 0o200)
        for rp in rpaths(p):
            if rp.startswith("/"):
                subprocess.run(["install_name_tool", "-delete_rpath", rp, str(p)], check=True)
        subprocess.run(["codesign", "--sign", "-", "--force", str(p)], check=True)

macos_dir = contents / "MacOS"
main_bin = macos_dir / "Superbird"
for exe in sorted(macos_dir.iterdir()):
    if not is_macho(exe):
        continue
    exe.chmod(exe.stat().st_mode | 0o200)
    for rp in rpaths(exe):
        if rp.startswith("/"):
            subprocess.run(["install_name_tool", "-delete_rpath", rp, str(exe)], check=True)
    if "@executable_path/../lib" not in rpaths(exe):
        subprocess.run(["install_name_tool", "-add_rpath", "@executable_path/../lib", str(exe)], check=True)
    if exe == main_bin:
        continue
    cmd = ["codesign", "--sign", "-", "--force"]
    ent = next(build_dir.glob(f"Services/**/{exe.name}.entitlements"), None)
    if ent is not None and ent.is_file():
        cmd += ["--options", "runtime", "--entitlements", str(ent)]
    subprocess.run([*cmd, str(exe)], check=True)

subprocess.run(["codesign", "--sign", "-", "--force", "--entitlements", str(main_entitlements), str(app)], check=True)

for subdir in ("MacOS", "lib", "PlugIns", "Frameworks"):
    d = contents / subdir
    if not d.is_dir():
        continue
    for p in sorted(d.rglob("*")):
        if is_macho(p):
            subprocess.run(["codesign", "--verify", str(p)], check=True)
subprocess.run(["codesign", "--verify", str(app)], check=True)
print(f"Verified portable macOS bundle signatures for {app}")

# Every library a binary loads through @rpath has to be in the bundle; an incomplete install otherwise only shows up
# when the app fails to launch.
missing = set()
for subdir in ("MacOS", "lib", "PlugIns", "Frameworks"):
    d = contents / subdir
    if not d.is_dir():
        continue
    for p in sorted(d.rglob("*")):
        if not is_macho(p):
            continue
        for line in subprocess.check_output(["otool", "-L", str(p)], text=True).splitlines()[1:]:
            name = line.strip().split(" (")[0]
            if not name.startswith("@rpath/"):
                continue
            library = name[len("@rpath/"):]
            if not (contents / "lib" / library).exists() and not (contents / "Frameworks" / library).exists():
                missing.add(library)
if missing:
    print("Error: the bundle is missing libraries it loads:", ", ".join(sorted(missing)), file=sys.stderr)
    sys.exit(1)
print(f"Every @rpath library is in {app}")
