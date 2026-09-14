import struct
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "sdcard" / "Japanese16.vlw"
JIS = ROOT / "support" / "character_sets" / "jis_x_0208_level1.txt"


class JapaneseFontTest(unittest.TestCase):
    def test_jis_level1_character_count(self):
        chars = "".join(JIS.read_text(encoding="utf-8").split())
        self.assertEqual(len(chars), 2965)
        self.assertEqual(len(set(chars)), 2965)

    def test_generated_font_contains_representative_ui_text(self):
        data = FONT.read_bytes()
        glyph_count = struct.unpack(">I", data[:4])[0]
        self.assertEqual(glyph_count, 3417)
        glyphs = {
            chr(struct.unpack(">I", data[24 + index * 28:28 + index * 28])[0])
            for index in range(glyph_count)
        }
        samples = (
            "現在の天気気温湿度気圧時間雨量天気予報",
            "危険な暑さ高温警戒高温注意降雨注意降雨予報",
            "雷雨霧雨雪煙かすみ砂塵降灰強風竜巻晴れ薄曇り不明",
            "0123456789.%℃hPamm",
        )
        for sample in samples:
            with self.subTest(sample=sample):
                self.assertTrue(set(sample) <= glyphs)


if __name__ == "__main__":
    unittest.main()
