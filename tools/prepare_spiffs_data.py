#!/usr/bin/env python3
"""Stage web assets for the SPIFFS image."""
import pathlib
import shutil
import sys


def main() -> int:
    web_source = pathlib.Path(sys.argv[1])
    image_dir = pathlib.Path(sys.argv[2])
    if not web_source.is_dir() or not (web_source / "index.html").is_file():
        raise SystemExit("Missing data/www/index.html")
    shutil.rmtree(image_dir, ignore_errors=True)
    image_dir.mkdir(parents=True, exist_ok=True)
    shutil.copytree(web_source, image_dir, dirs_exist_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
