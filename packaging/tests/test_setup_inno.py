"""Silent integration tests with a unique AppId and no real user shortcuts.

Set BROKENBOND_TEST_PACKAGE to the portable staging directory to test.
Synthetic payloads are used by default; BROKENBOND_TEST_XEX enables imports
from a locally supplied supported XEX. The launcher/game is never started.
"""
import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib
import unittest
import uuid
import winreg

from test_setup_helper import EXTRACTOR, HELPER, ROOT, SUPPORTED_XEX, xex, synthetic_xex


class InnoSetupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        parent = Path(os.environ["LOCALAPPDATA"]) / "Temp/broken-bond-tests"
        parent.mkdir(parents=True, exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="broken_bond-inno-tests-", dir=parent)
        cls.root = Path(cls.temp.name)
        compiler = Path(os.environ.get("BROKENBOND_TEST_ISCC",
                        Path(os.environ["LOCALAPPDATA"]) / "Programs/Inno Setup 6/ISCC.exe"))
        package = Path(os.environ.get("BROKENBOND_TEST_PACKAGE", cls.root / "package"))
        if "BROKENBOND_TEST_PACKAGE" not in os.environ:
            package.mkdir()
            for name in ("broken_bond.exe", "broken_bond_launcher.exe", "rexruntime.dll"):
                (package / name).write_bytes(b"synthetic port payload; never execute")
            (package / "README.txt").write_text("Synthetic installer test payload")
            shutil.copyfile(ROOT / "packaging/broken_bond.toml", package / "broken_bond.toml")
            (package / "licenses").mkdir()
            shutil.copyfile(ROOT / "LICENSE", package / "licenses/project.txt")
        cls.package = package
        # Exercise the same escaped GUID AppId format as production.
        cls.app_id = "{" + str(uuid.uuid4()).upper() + "}"
        cls.current_version = "1.0.1"
        cls.uninstall_key = 'Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' + cls.app_id + '_is1'

        def compile_setup(source, version, output):
            result = subprocess.run([str(compiler), "/Q", f"/O{output}",
                                     f"/DPackageDir={source}", f"/DHelperPath={HELPER}",
                                     f"/DExtractorPath={EXTRACTOR}",
                                     f"/DMyAppVersion={version}", f"/DSetupAppId={cls.app_id.replace('{', '{{')}",
                                     f"/DDefaultInstallDir={cls.root / 'missing-default'}",
                                     str(ROOT / "packaging/installer.iss")],
                                    capture_output=True, timeout=120)
            if result.returncode:
                raise RuntimeError((result.stdout + result.stderr).decode(errors="replace"))
            return output / "BrokenBondInstaller.exe"

        cls.setup = compile_setup(package, cls.current_version, cls.root / "current")
        previous_package = cls.root / "previous-package"
        shutil.copytree(package, previous_package)
        for name in ("broken_bond.exe", "rexruntime.dll"):
            (previous_package / name).write_bytes(b"synthetic previous-release binary")
        cls.previous_setup = compile_setup(previous_package, "1.0.0", cls.root / "previous")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.uninstalled = False
        self.dest = self.root / ("instalação teste " + uuid.uuid4().hex)
        self.dest.mkdir()
        self.case = self.root / ("fixture-" + uuid.uuid4().hex)
        self.case.mkdir()

    def tearDown(self):
        if not self.uninstalled and (self.dest / "unins000.exe").exists():
            self.uninstall()

    def install(self, iso=None, language="english", mode=None, setup=None, use_dir=True):
        args = [str(setup or self.setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                "/NOICONS", "/TASKS=",
                f"/LOG={self.case / ('install-' + uuid.uuid4().hex + '.log')}"]
        if use_dir:
            args.append(f"/DIR={self.dest}")
        if mode:
            args.append(f"/MODE={mode}")
        if language is not None:
            args.append(f"/LANG={language}")
        if iso:
            args.append(f"/GAMEISO={iso}")
        return subprocess.run(args, capture_output=True, timeout=600 if os.environ.get("BROKENBOND_TEST_ISO") else 60).returncode

    def uninstall(self):
        result = subprocess.run([str(self.dest / "unins000.exe"), "/VERYSILENT",
                                 "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=60)
        self.assertEqual(result.returncode, 0)
        self.uninstalled = True

    def iso(self, title=0x55530825, executable=None):
        source = self.case / "source"
        source.mkdir()
        (source / "default.xex").write_bytes(xex(title) if executable is None else executable)
        (source / "fixture.txt").write_text("synthetic data")
        iso = self.case / "edição original.iso"
        subprocess.run([str(EXTRACTOR), "-c", str(source), str(iso)],
                       check=True, capture_output=True, timeout=30)
        return iso

    def test_without_iso_and_uninstall_preservation(self):
        self.assertEqual(self.install(), 0)
        self.assertTrue((self.dest / "broken_bond_launcher.exe").exists())
        self.assertTrue((self.dest / "assets/favicon.ico").exists())
        # Every runtime DLL staged in the package must be installed: missing
        # DLLs break loading (e.g. rexruntime.dll imports amd_fidelityfx_dx12).
        for dll in self.package.glob("*.dll"):
            self.assertTrue((self.dest / dll.name).exists(), dll.name)
        self.assertFalse((self.dest / "broken_bond_installer.exe").exists())
        self.assertFalse((self.dest / "extract-xiso.exe").exists())
        self.assertFalse((self.dest / "rexgpu-xenos.dll").exists())
        self.assertFalse((self.dest / "broken_bond_setup_helper.exe").exists())
        self.assertFalse((self.dest / "game_root").exists())
        config = self.dest / "broken_bond.toml"
        config.write_text('# user settings\nfullscreen = false\n')
        game = self.dest / "game/default.xex"
        game.write_bytes(xex())
        unrelated = self.dest / "personal.txt"
        unrelated.write_text("keep me")
        self.assertEqual(self.install(), 0)
        self.assertEqual(config.read_text(), '# user settings\nfullscreen = false\n')
        self.uninstall()
        self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())
        self.assertTrue(config.exists())
        self.assertEqual(game.read_bytes(), xex())
        self.assertEqual(unrelated.read_text(), "keep me")

    @unittest.skipUnless(SUPPORTED_XEX, "Set BROKENBOND_TEST_XEX for supported-revision imports")
    def test_iso_import_portuguese(self):
        self.assertEqual(self.install(self.iso(), "brazilianportuguese"), 0)
        self.assertEqual((self.dest / "game/default.xex").read_bytes(), xex())
        self.assertIn('launcher_language = "pt_BR"', (self.dest / "broken_bond.toml").read_text(encoding='utf-8-sig'))
        self.assertFalse(list(self.dest.glob('.broken_bond-import-*')))
        self.uninstall()
        self.assertTrue((self.dest / "game/default.xex").exists())

    def test_interface_languages_preserve_native_game_language(self):
        languages = [("english", "en"), ("brazilianportuguese", "pt_BR"),
                     ("french", "fr"), ("german", "de"), ("spanish", "es"),
                     ("italian", "it"), ("russian", "ru")]
        for language, code in languages:
            with self.subTest(language=language):
                self.dest = self.root / ("language-" + uuid.uuid4().hex)
                self.dest.mkdir()
                self.uninstalled = False
                self.assertEqual(self.install(language=language), 0)
                config = tomllib.loads((self.dest / "broken_bond.toml").read_text(encoding="utf-8-sig"))
                self.assertEqual(config["launcher_language"], code)
                self.assertEqual(config["user_language"], 1)
                self.uninstall()

    def test_default_install_language_is_english(self):
        self.assertEqual(self.install(language=None), 0)
        config = tomllib.loads((self.dest / "broken_bond.toml").read_text(encoding="utf-8-sig"))
        self.assertEqual(config["launcher_language"], "en")
        self.assertEqual(config["renderer"], "native")
        self.assertEqual(config["native_backend"], "d3d12")
        self.assertNotIn("gpu_plugin", config)

    def test_existing_data_not_overwritten(self):
        game = self.dest / "game"
        game.mkdir()
        (game / "personal.txt").write_text("keep")
        self.assertNotEqual(self.install(self.iso()), 0)
        self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())
        self.assertEqual((game / "personal.txt").read_text(), "keep")

    @unittest.skipUnless(SUPPORTED_XEX, "Set BROKENBOND_TEST_XEX for supported-revision imports")
    def test_empty_game_folder_can_be_imported(self):
        (self.dest / "game").mkdir()
        self.assertEqual(self.install(self.iso()), 0)
        self.assertTrue((self.dest / "game/default.xex").exists())

    def test_wrong_game_not_installed(self):
        self.assertNotEqual(self.install(self.iso(0x454108CF)), 0)
        self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())
        self.assertFalse((self.dest / "game/default.xex").exists())

    def test_same_title_unsupported_revision_not_installed(self):
        self.assertNotEqual(self.install(self.iso(executable=synthetic_xex())), 0)
        self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())
        self.assertFalse((self.dest / "game/default.xex").exists())
        self.assertFalse(list(self.dest.glob(".broken_bond-import-*")))

    def test_game_directory_junction_is_not_followed(self):
        target = self.case / "personal-game"
        target.mkdir()
        # Junction creation does not require developer mode or administrator rights.
        result = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command",
             "New-Item -ItemType Junction -Path $env:BB_TEST_LINK -Target $env:BB_TEST_TARGET | Out-Null"],
            env={**os.environ, "BB_TEST_LINK": str(self.dest / "game"), "BB_TEST_TARGET": str(target)},
            capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            self.assertNotEqual(self.install(self.iso()), 0)
            self.assertEqual(list(target.iterdir()), [])
            self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())
        finally:
            # Remove only the junction itself, never the target.
            (self.dest / "game").rmdir()

    def test_invalid_image_not_installed(self):
        invalid = self.case / "invalid.iso"
        invalid.write_bytes(b"invalid image")
        self.assertNotEqual(self.install(invalid), 0)
        self.assertFalse((self.dest / "broken_bond_launcher.exe").exists())

    def user_files(self):
        files = {
            "broken_bond.toml": b'# custom settings\r\nfullscreen = false\r\nlauncher_language = "ru"\r\n',
            "game/default.xex": bytes(xex()),
            "game/custom-assets.bin": b"original game data",
            "logs/session.log": b"existing log",
            "personal.txt": b"personal file",
            "mods/community/notes.txt": b"personal mod notes",
            "user/saves/save.bin": b"default save data",
            "user/cache/test.bin": b"default cache data",
            "test-user-data/saves/save.bin": b"save data",
        }
        for name, content in files.items():
            path = self.dest / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        config = self.dest / "broken_bond.toml"
        config.write_bytes(files["broken_bond.toml"] +
                           f'user_data_root = "{(self.dest / "test-user-data").as_posix()}"\r\n'.encode())
        files["broken_bond.toml"] = config.read_bytes()
        return files

    def assert_user_files(self, files):
        for name, content in files.items():
            with self.subTest(file=name):
                self.assertEqual((self.dest / name).read_bytes(), content)

    def assert_current_release(self):
        self.assertFalse((self.dest / "rexgpu-xenos.dll").exists())
        for name in ("broken_bond.exe", "broken_bond_launcher.exe", "rexruntime.dll"):
            self.assertEqual((self.dest / name).read_bytes(), (self.package / name).read_bytes(), name)
        self.assertEqual((self.dest / "broken_bond.version").read_text().strip(), self.current_version)
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self.uninstall_key, 0,
                            winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            self.assertEqual(winreg.QueryValueEx(key, "DisplayVersion")[0], self.current_version)
        self.assertEqual(len(list(self.dest.glob("unins*.exe"))), 1)

    def test_update_between_releases_preserves_user_data(self):
        self.assertEqual(self.install(setup=self.previous_setup, mode="install"), 0)
        self.assertNotEqual((self.dest / "broken_bond.exe").read_bytes(), (self.package / "broken_bond.exe").read_bytes())
        files = self.user_files()
        (self.dest / "rexgpu-xenos.dll").write_bytes(b"retired plugin")
        # Older Inno releases only recorded their version in the registry.
        (self.dest / "broken_bond.version").unlink()
        self.assertEqual(self.install(mode="update", language="brazilianportuguese", use_dir=False), 0)
        self.assert_current_release()
        self.assert_user_files(files)
        self.assertEqual(self.install(mode="update"), 0)
        self.assert_user_files(files)
        self.uninstall()
        self.assert_user_files(files)
        self.assertFalse((self.dest / "broken_bond.version").exists())

    def test_update_portable_without_version_or_registration(self):
        for name in ("broken_bond.exe", "broken_bond_launcher.exe"):
            (self.dest / name).write_bytes(b"synthetic portable binary")
        files = self.user_files()
        self.assertEqual(self.install(mode="update"), 0)
        self.assert_current_release()
        self.assert_user_files(files)

    def test_update_rejects_missing_installation(self):
        self.assertNotEqual(self.install(mode="update"), 0)
        self.assertEqual(list(self.dest.iterdir()), [])
        (self.dest / "broken_bond.exe").write_bytes(b"only one executable")
        self.assertNotEqual(self.install(mode="update"), 0)
        self.assertEqual(len(list(self.dest.iterdir())), 1)

    def test_update_rejects_iso_and_invalid_mode(self):
        self.assertEqual(self.install(setup=self.previous_setup), 0)
        previous = (self.dest / "broken_bond.exe").read_bytes()
        self.assertNotEqual(self.install(mode="update", iso=self.iso()), 0)
        self.assertNotEqual(self.install(mode="invalid"), 0)
        self.assertEqual((self.dest / "broken_bond.exe").read_bytes(), previous)
        self.assertEqual((self.dest / "broken_bond.version").read_text().strip(), "1.0.0")

    def test_update_blocks_older_installer(self):
        self.assertEqual(self.install(), 0)
        files = self.user_files()
        for mode in ("update", "install"):
            with self.subTest(mode=mode):
                self.assertNotEqual(self.install(mode=mode, setup=self.previous_setup), 0)
                self.assert_current_release()
                self.assert_user_files(files)

    @unittest.skipUnless(SUPPORTED_XEX, "Set BROKENBOND_TEST_XEX for supported-revision imports")
    def test_existing_install_can_import_iso_in_install_mode(self):
        self.assertEqual(self.install(), 0)
        self.assertEqual(self.install(mode="install", iso=self.iso()), 0)
        self.assertEqual((self.dest / "game/default.xex").read_bytes(), xex())

    @unittest.skipUnless(os.environ.get("BROKENBOND_TEST_ISO"), "Set BROKENBOND_TEST_ISO for a full local disc import")
    def test_full_local_disc_import(self):
        self.assertEqual(self.install(Path(os.environ["BROKENBOND_TEST_ISO"]), mode="install"), 0)
        executable = self.dest / "game/default.xex"
        self.assertEqual(hashlib.sha256(executable.read_bytes()).hexdigest(),
                         "8f70e79443e36b38e44b6a105dad51fb8909fb615d982753cabaac9b15f0d576")
        imported = {p.relative_to(self.dest): p.stat().st_size
                    for p in (self.dest / "game").rglob("*") if p.is_file()}
        self.assertGreater(len(imported), 1)
        self.assertFalse(list(self.dest.glob(".broken_bond-import-*")))
        self.uninstall()
        for relative, size in imported.items():
            self.assertEqual((self.dest / relative).stat().st_size, size)


if __name__ == "__main__":
    unittest.main()
