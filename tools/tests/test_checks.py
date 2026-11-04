"""Self-tests for the repository checks: each must catch a deliberate violation.

Run with: python -m unittest discover -s tools/tests
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_layering  # noqa: E402
import check_notices  # noqa: E402


def write(root: Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


class CheckLayeringTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def test_clean_tree_passes(self):
        write(self.root, "components/rb_domain/src/a.cpp", '#include "rb/core/x.hpp"\n#include <cstdint>\n')
        self.assertEqual(check_layering.check_repo(self.root), [])

    def test_platform_header_in_pure_layer_fails(self):
        write(self.root, "components/rb_domain/src/a.cpp", '#include "freertos/FreeRTOS.h"\n')
        write(self.root, "components/rb_sio/src/b.cpp", "#include <esp_log.h>\n")
        write(self.root, "components/rb_application/src/c.cpp", '#include "ff.h"\n')
        self.assertEqual(len(check_layering.check_repo(self.root)), 3)

    def test_upward_include_fails(self):
        write(self.root, "components/rb_domain/include/rb/domain/a.hpp", '#include "rb/app/ports/x.hpp"\n')
        write(self.root, "components/rb_core/src/b.cpp", '#include "rb/sio/y.hpp"\n')
        self.assertEqual(len(check_layering.check_repo(self.root)), 2)

    def test_hw_diag_stays_independent_of_platform_and_domain(self):
        write(self.root, "components/rb_hw_diag/src/ok.cpp", '#include "rb/core/x.hpp"\n#include "rb/hwdiag/y.hpp"\n')
        write(self.root, "components/rb_hw_diag/src/a.cpp", '#include "esp_system.h"\n')
        write(self.root, "components/rb_hw_diag/src/b.cpp", '#include "rb/domain/disk_image.hpp"\n')
        self.assertEqual(len(check_layering.check_repo(self.root)), 2)

    def test_netsio_has_no_sockets_and_does_not_depend_on_sio(self):
        write(self.root, "components/rb_netsio/src/ok.cpp",
              '#include "rb/app/ports/sio_transport.hpp"\n#include "rb/netsio/netsio_codec.hpp"\n')
        write(self.root, "components/rb_netsio/src/a.cpp", "#include <sys/socket.h>\n")
        write(self.root, "components/rb_netsio/src/b.cpp", '#include "lwip/sockets.h"\n')
        write(self.root, "components/rb_netsio/src/c.cpp", '#include "rb/sio/sio_session.hpp"\n')
        self.assertEqual(len(check_layering.check_repo(self.root)), 3)

    def test_infrastructure_components_are_not_checked(self):
        write(self.root, "components/rb_esp_sio/src/a.cpp", '#include "driver/uart.h"\n')
        self.assertEqual(check_layering.check_repo(self.root), [])


class CheckNoticesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        write(self.root, "LICENSE", "own rights\n")

    def tearDown(self):
        self._tmp.cleanup()

    def test_listed_vendored_dir_passes(self):
        write(self.root, "test/host/unity/LICENSE.txt", "MIT\n")
        write(self.root, "THIRD_PARTY_NOTICES.md", "Unity — `test/host/unity`\n")
        self.assertEqual(check_notices.main(["x", str(self.root)]), 0)

    def test_unlisted_vendored_dir_fails(self):
        write(self.root, "third_party/foo/COPYING", "GPL\n")
        write(self.root, "THIRD_PARTY_NOTICES.md", "nothing\n")
        self.assertEqual(check_notices.main(["x", str(self.root)]), 1)

    def test_unlisted_manifest_dependency_fails(self):
        write(self.root, "main/idf_component.yml", 'dependencies:\n  idf: ">=6.1"\n  espressif/foo: "1.0.0"\n')
        write(self.root, "THIRD_PARTY_NOTICES.md", "nothing\n")
        self.assertEqual(check_notices.manifest_dependencies(self.root), ["espressif/foo"])
        self.assertEqual(check_notices.main(["x", str(self.root)]), 1)


if __name__ == "__main__":
    unittest.main()
