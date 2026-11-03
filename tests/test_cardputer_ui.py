import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def tool(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CardputerUiTests(unittest.TestCase):
    def test_real_ui_navigation_bounds_and_secrets(self):
        module = tool("check_cardputer_ui")
        logos = tool("generate_cardputer_logos")
        with tempfile.TemporaryDirectory(prefix="cardputer-ui-check-") as directory:
            self.assertIn("checks passed", module.check(Path(directory)))
            commands = json.loads((Path(directory) / "splash.json").read_text(encoding="utf-8"))
            preview = module.render_commands(commands, bytes(256 * 5))
            # Replay the production logo drawing commands and compare every
            # pixel with the original-derived assets; no host PNG placeholder.
            for spec, position in zip(logos.SPECS, ((83, 25), (220, 0))):
                _, filename, width, height, background = spec
                left, top = position
                actual = [None] * (width * height)
                for command in commands:
                    if command["kind"] != "rect":
                        continue
                    for y in range(max(top, command["y"]), min(top + height, command["y"] + command["h"])):
                        for x in range(max(left, command["x"]), min(left + width, command["x"] + command["w"])):
                            actual[(y - top) * width + x - left] = command["color"]
                expected = logos.logo_pixels(filename, width, height, background)
                self.assertEqual(actual, expected)
                # Also verify the exported preview: single-pixel logo runs
                # must survive replay just as they do in the display canvas.
                expected_rgb332 = [(((value >> 13) & 7) * 255 // 7,
                                    ((value >> 8) & 7) * 255 // 7,
                                    ((value >> 3) & 3) * 255 // 3) for value in expected]
                rendered = preview.crop((left, top, left + width, top + height))
                self.assertEqual(list(rendered.getdata()), expected_rgb332)

    def test_compiled_logo_assets_match_original_sources(self):
        logos = tool("generate_cardputer_logos")
        self.assertEqual(logos.OUTPUT.read_text(encoding="utf-8"), logos.generated_header())
        self.assertEqual(sum(width * height * 2 for _, _, width, height, _ in logos.SPECS), 11712)


if __name__ == "__main__":
    unittest.main()
