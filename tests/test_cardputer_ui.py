import importlib.util
from pathlib import Path
import tempfile
import unittest


class CardputerUiTests(unittest.TestCase):
    def test_real_ui_navigation_bounds_and_secrets(self):
        path = Path(__file__).resolve().parents[1] / "tools/check_cardputer_ui.py"
        spec = importlib.util.spec_from_file_location("cardputer_ui_check", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory(prefix="cardputer-ui-check-") as directory:
            self.assertIn("checks passed", module.check(Path(directory)))


if __name__ == "__main__":
    unittest.main()
