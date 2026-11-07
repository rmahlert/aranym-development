import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('stage', Path(__file__).parents[2] / 'tools/stage-fvdi-update.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class StageTest(unittest.TestCase):
    def package(self, root, extra=None, corrupt=False):
        archive = root / 'fvdi.zip'
        files = {name: name.encode() for name in ('AUTO/FVDI.PRG', 'MINT/BIN/FVDI.PRG', 'GEMSYS/ARANYM.SYS')}
        sums = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n' for name, data in files.items())
        if extra:
            files[extra] = b'unlisted'
        files['SHA256SUMS.txt'] = sums.encode()
        if corrupt:
            files['AUTO/FVDI.PRG'] = b'wrong binary'
        with zipfile.ZipFile(archive, 'w') as out:
            for name, data in files.items():
                out.writestr('update/' + name, data)
        Path(str(archive) + '.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  fvdi.zip\n')
        return archive

    def test_verified_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage.stage(self.package(root), root / 'out')
            self.assertEqual((root / 'out/AUTO/FVDI.PRG').read_bytes(), b'AUTO/FVDI.PRG')

    def test_unlisted_and_unsafe_files(self):
        for name in ('unlisted.txt', '../escape', 'C:/escape', 'dir\\escape'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                with self.assertRaises(ValueError):
                    stage.stage(self.package(root, extra=name), root / 'out')
                self.assertFalse((root / 'out').exists())

    def test_corrupted_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                stage.stage(self.package(root, corrupt=True), root / 'out')
            self.assertFalse((root / 'out').exists())

    def test_corrupted_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.package(root)
            archive.write_bytes(archive.read_bytes() + b'changed')
            with self.assertRaises(ValueError):
                stage.stage(archive, root / 'out')


if __name__ == '__main__':
    unittest.main()
