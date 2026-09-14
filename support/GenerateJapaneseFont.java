import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.font.FontRenderContext;
import java.awt.font.GlyphVector;
import java.awt.geom.Rectangle2D;
import java.awt.image.BufferedImage;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;

/** Generates the TFT_eSPI Smooth Font used by the M5Stack Japanese UI. */
public final class GenerateJapaneseFont {
  private static final String FONT_REVISION =
      "f8d157532fbfaeda587e826d4cd5b21a49186f7c";
  private static final String FONT_SHA256 =
      "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5";
  private static final URI FONT_URI = URI.create(
      "https://raw.githubusercontent.com/notofonts/noto-cjk/" + FONT_REVISION
          + "/Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf");
  private static final int FONT_SIZE = 16;
  private static final Path ROOT = Path.of("").toAbsolutePath();
  private static final Path CACHE =
      ROOT.resolve("support/.font-cache/NotoSansCJKjp-Regular.otf");
  private static final Path OUTPUT = ROOT.resolve("sdcard/Japanese16.vlw");

  private record Glyph(int codePoint, int width, int height, int advance,
                       int dy, int dx, byte[] bitmap) {}

  private GenerateJapaneseFont() {}

  public static void main(String[] args) throws Exception {
    if (args.length != 0 &&
        !(args.length == 2 && args[0].equals("--font"))) {
      throw new IllegalArgumentException(
          "Usage: java support/GenerateJapaneseFont.java [--font PATH]");
    }
    Path fontPath = resolveFont(args);
    Font font = Font.createFont(Font.TRUETYPE_FONT, fontPath.toFile())
        .deriveFont(Font.PLAIN, (float) FONT_SIZE);
    FontRenderContext context = new FontRenderContext(null, true, true);
    List<Glyph> glyphs = new ArrayList<>();
    for (int codePoint : readCodePoints()) {
      if (font.canDisplay(codePoint)) {
        glyphs.add(renderGlyph(font, context, codePoint));
      }
    }

    BufferedImage metricsImage = new BufferedImage(1, 1, BufferedImage.TYPE_BYTE_GRAY);
    Graphics2D metricsGraphics = metricsImage.createGraphics();
    metricsGraphics.setFont(font);
    FontMetrics metrics = metricsGraphics.getFontMetrics();
    int ascent = metrics.getAscent();
    int descent = metrics.getDescent();
    metricsGraphics.dispose();

    Files.createDirectories(OUTPUT.getParent());
    try (DataOutputStream output = new DataOutputStream(
        new BufferedOutputStream(Files.newOutputStream(OUTPUT)))) {
      output.writeInt(glyphs.size());
      output.writeInt(11);
      output.writeInt(FONT_SIZE);
      output.writeInt(0);
      output.writeInt(ascent);
      output.writeInt(descent);
      for (Glyph glyph : glyphs) {
        output.writeInt(glyph.codePoint());
        output.writeInt(glyph.height());
        output.writeInt(glyph.width());
        output.writeInt(glyph.advance());
        output.writeInt(glyph.dy());
        output.writeInt(glyph.dx());
        output.writeInt(0);
      }
      for (Glyph glyph : glyphs) {
        output.write(glyph.bitmap());
      }
    }
    System.out.printf("Generated %s with %,d glyphs (%,d bytes)%n",
                      OUTPUT, glyphs.size(), Files.size(OUTPUT));
  }

  private static Path resolveFont(String[] args) throws Exception {
    Path path = args.length == 2 && args[0].equals("--font")
        ? Path.of(args[1]).toAbsolutePath() : CACHE;
    if (!Files.exists(path)) {
      if (path != CACHE) throw new IOException("Font file not found: " + path);
      Files.createDirectories(path.getParent());
      Path temporary = Path.of(path + ".download");
      try (InputStream input = FONT_URI.toURL().openStream()) {
        Files.copy(input, temporary, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
      }
      Files.move(temporary, path, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
    }
    if (!sha256(path).equals(FONT_SHA256)) {
      throw new IOException("Font SHA-256 does not match the pinned source: " + path);
    }
    return path;
  }

  private static String sha256(Path path) throws Exception {
    MessageDigest digest = MessageDigest.getInstance("SHA-256");
    try (InputStream input = Files.newInputStream(path)) {
      byte[] buffer = new byte[8192];
      for (int read; (read = input.read(buffer)) >= 0;) digest.update(buffer, 0, read);
    }
    StringBuilder result = new StringBuilder();
    for (byte value : digest.digest()) result.append(String.format("%02x", value));
    return result.toString();
  }

  private static LinkedHashSet<Integer> readCodePoints() throws IOException {
    LinkedHashSet<Integer> result = new LinkedHashSet<>();
    Path ranges = ROOT.resolve("support/character_sets/japanese_unicode_ranges.txt");
    for (String raw : Files.readAllLines(ranges)) {
      String value = raw.replaceFirst("#.*", "").trim();
      if (value.isEmpty()) continue;
      String[] bounds = value.split("-");
      int start = Integer.parseInt(bounds[0], 16);
      int end = bounds.length == 1 ? start : Integer.parseInt(bounds[1], 16);
      for (int codePoint = start; codePoint <= end; codePoint++) result.add(codePoint);
    }
    String jis = Files.readString(
        ROOT.resolve("support/character_sets/jis_x_0208_level1.txt"));
    jis.codePoints().filter(codePoint -> !Character.isWhitespace(codePoint))
        .forEach(result::add);
    return result;
  }

  private static Glyph renderGlyph(Font font, FontRenderContext context,
                                   int codePoint) {
    String text = new String(Character.toChars(codePoint));
    GlyphVector vector = font.createGlyphVector(context, text);
    java.awt.Rectangle bounds = vector.getPixelBounds(context, 0, 0);
    int width = Math.max(0, bounds.width);
    int height = Math.max(0, bounds.height);
    int advance = Math.max(1, (int) Math.ceil(
        vector.getGlyphMetrics(0).getAdvanceX()));
    byte[] bitmap = new byte[width * height];
    if (width > 0 && height > 0) {
      BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_BYTE_GRAY);
      Graphics2D graphics = image.createGraphics();
      graphics.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                                RenderingHints.VALUE_ANTIALIAS_ON);
      graphics.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                                RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
      graphics.setFont(font);
      graphics.setColor(java.awt.Color.WHITE);
      graphics.drawString(text, -bounds.x, -bounds.y);
      graphics.dispose();
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          bitmap[y * width + x] = (byte) image.getRaster().getSample(x, y, 0);
        }
      }
    }
    return new Glyph(codePoint, width, height, advance, -bounds.y,
                     bounds.x, bitmap);
  }
}
