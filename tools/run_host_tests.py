"""Build and run hardware-independent production C tests on Windows/Linux."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='gcc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / '.host-build'
    build.mkdir(exist_ok=True)
    cases = (
        ('protocol', ['tests/test_command_protocol.c', 'firmware/System/CommandProtocol.c']),
        ('config', ['tests/test_app_config.c', 'firmware/System/AppConfig.c']),
        ('storage', ['tests/test_config_store.c', 'firmware/System/ConfigStore.c', 'firmware/System/AppConfig.c']),
    )
    for name, sources in cases:
        binary = build / (name + ('.exe' if sys.platform == 'win32' else ''))
        subprocess.run([args.compiler, '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-Ifirmware/System', *sources, '-o', str(binary)], cwd=root, check=True)
        subprocess.run([str(binary)], cwd=root, check=True)


if __name__ == '__main__':
    main()
