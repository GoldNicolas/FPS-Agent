"""Pack a packaged Linux build into a .tar.gz with correct Unix permissions.

Windows has no execute bit, so tar tools there store executables as 0644 and the game would not start on Cradle.
This sets 0755 on the launch script, the game binary and the shared libraries, and 0644 on everything else.

Usage (from the repo root):
    conda run -n fps-agent python tools/package/make_tarball.py <build_dir> <out.tar.gz> <root_name>
"""
import os
import sys
import tarfile

EXEC_SUFFIXES = (".sh", ".so")


def is_executable(rel_path: str) -> bool:
    rel = rel_path.replace("\\", "/")
    name = rel.rsplit("/", 1)[-1]
    if name.endswith(EXEC_SUFFIXES) or ".so." in name:
        return True
    # Extension-less files in Binaries/Linux are the game / tool executables.
    return "/Binaries/Linux/" in f"/{rel}" and "." not in name


def main(build_dir: str, out_path: str, root_name: str) -> int:
    build_dir = os.path.abspath(build_dir)
    count = execs = 0

    def fix(info: tarfile.TarInfo) -> tarfile.TarInfo:
        nonlocal count, execs
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        if info.isdir():
            info.mode = 0o755
        else:
            rel = info.name.split("/", 1)[1] if "/" in info.name else info.name
            info.mode = 0o755 if is_executable(rel) else 0o644
            count += 1
            execs += info.mode == 0o755
        return info

    with tarfile.open(out_path, "w:gz", compresslevel=6) as tar:
        tar.add(build_dir, arcname=root_name, filter=fix)
    print(f"wrote {out_path}: {count} files ({execs} executable)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(*sys.argv[1:]))
