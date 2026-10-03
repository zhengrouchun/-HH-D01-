"""Build isolated role deliverables; restore the user's configuration on exit.

Run with the same Python used by the SDK: python project/tools/build_dual.py [a|b|api|api_mock|all].
This does not flash hardware or run any network application.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
SDK = PROJECT.parents[3]
CONFIG = SDK / 'build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config'
OUTPUT = SDK / 'output/clearchain'
BUILD = SDK / 'output/ws63/acore/ws63-liteos-app'
TOOLCHAIN = SDK / 'tools/bin/compiler/riscv/cc_riscv32_musl_105/cc_riscv32_musl_win/bin'


def main():
    choice = sys.argv[1] if len(sys.argv) > 1 else 'all'
    if choice not in ('a', 'b', 'api', 'api_mock', 'all'):
        raise SystemExit('Expected a, b, api, api_mock, or all')
    saved = {p: p.read_bytes() if p.exists() else None for p in (CONFIG, CONFIG.with_suffix('.config.old'))}
    env = os.environ.copy()
    ccache = SDK.parents[1] / 'tools/cfbb/thirdparty/ccache'
    env['PATH'] = str(TOOLCHAIN) + os.pathsep + str(ccache) + os.pathsep + env['PATH']
    roles = ('a', 'b', 'api') if choice == 'all' else (choice,)
    try:
        for role in roles:
            dest = OUTPUT / ('board_a_' + role if role in ('api', 'api_mock') else 'board_' + role)
            dest.mkdir(parents=True, exist_ok=True)
            # A success marker belongs to this run only; never certify a stale package.
            marker = dest / 'manifest.json'
            if marker.exists():
                marker.unlink()
            profile = ('board_a_api_mock.config' if role == 'api_mock' else
                       'board_a_api.config' if role == 'api' else f'board_{role}.config')
            assignments = [line.strip() for line in (PROJECT / 'profiles' / profile).read_text().splitlines()
                           if line.strip() and not line.startswith('#')]
            with (dest / 'build.log').open('w', encoding='utf8') as log:
                for cmd in ([sys.executable, 'build/script/usr_config.py', '--command', 'setconfig',
                             '--chip', 'ws63', '--core', 'acore', '--target', 'ws63-liteos-app', *assignments],
                            [sys.executable, 'build.py', '-ninja', '-j8', 'ws63-liteos-app']):
                    print('RUN', subprocess.list2cmdline(cmd), flush=True)
                    log.write('RUN ' + subprocess.list2cmdline(cmd) + '\n'); log.flush()
                    result = subprocess.run(cmd, cwd=SDK, env=env, stdout=log, stderr=subprocess.STDOUT)
                    if result.returncode:
                        raise RuntimeError(f'Board {role} failed ({result.returncode}); see {dest / "build.log"}')
            generated = (BUILD / 'mconfig.h').read_text()
            expected = 'SERVER' if role in ('a', 'api', 'api_mock') else 'CLIENT'
            if f'#define CONFIG_CLEARCHAIN_DISPLAY_SLE_{expected} 1' not in generated:
                raise RuntimeError('Generated role does not match requested profile')
            archives = list(BUILD.rglob('libws63_liteos_app.a'))
            if len(archives) != 1:
                raise RuntimeError(f'Expected one application archive, got {archives}')
            members = subprocess.check_output([str(TOOLCHAIN / 'riscv32-linux-musl-ar.exe'), 't', str(archives[0])], env=env)
            (dest / 'archive-members.txt').write_bytes(members)
            names = members.decode()
            if role in ('a', 'api', 'api_mock'):
                assert 'clearchain_display_link.c' in names and 'r200_reader.c' in names
                assert 'clearchain_lcd.c' not in names and 'clearchain_display_client.c' not in names
                if role in ('api', 'api_mock'):
                    assert 'clearchain_device_app.c' in names and 'clearchain_device_http.c' in names
                    assert 'tcp_client_demo.c' not in names
                else:
                    assert 'tcp_client_demo.c' in names and 'clearchain_device_app.c' not in names
            else:
                assert 'clearchain_lcd.c' in names and 'clearchain_display_client.c' in names
                assert 'clearchain_ui.c' in names
                assert not any(name in names for name in ('r200_reader.c', 'clearchain_tca9555.c', 'clearchain_http.c', 'my_wifi_api.c'))
            assert 'clearchain_oled.c' not in names
            outputs = [SDK / 'output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg',
                       BUILD / 'ws63-liteos-app-sign.bin', BUILD / 'mconfig.h', BUILD / 'compile_commands.json']
            manifest = {'role': role, 'files': [], 'hardware_verified': False}
            for source in outputs:
                shutil.copy2(source, dest / source.name)
                manifest['files'].append({'file': source.name, 'size': source.stat().st_size,
                                          'sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
            shutil.copy2(CONFIG, dest / 'role.config')
            marker.write_text(json.dumps(manifest, indent=2), encoding='utf8')
            print('SUCCESS', dest, flush=True)
    finally:
        for path, content in saved.items():
            if content is not None:
                path.write_bytes(content)
            elif path.exists():
                path.unlink()


if __name__ == '__main__':
    main()
