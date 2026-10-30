#include "icon_library.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "stb_image.h"  // stbi_zlib_decode_malloc (implemented in title_screen.cpp)

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <imstb_truetype.h>

namespace icon_library {

namespace {

constexpr std::string_view kPrefix = "mcExternalGenericIcons_";

// Flash tag codes used here.
enum : uint16_t {
  kEnd = 0,
  kShowFrame = 1,
  kDefineShape = 2,
  kDoAction = 12,
  kRemoveObject2 = 28,
  kPlaceObject2 = 26,
  kDefineShape2 = 22,
  kDefineShape3 = 32,
  kDefineText = 11,
  kDefineText2 = 33,
  kDefineSprite = 39,
  kExportAssets = 56,
  kPlaceObject3 = 70,
  kDefineShape4 = 83,
};

struct Tag {
  uint16_t code = 0;
  std::vector<uint8_t> data;
};

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

bool ParseTags(const uint8_t* p, size_t size, std::vector<Tag>* out) {
  size_t i = 0;
  while (i + 2 <= size) {
    const uint16_t head = U16(p + i);
    i += 2;
    Tag tag;
    tag.code = head >> 6;
    size_t len = head & 0x3f;
    if (len == 0x3f) {
      if (i + 4 > size) return false;
      len = U32(p + i);
      i += 4;
    }
    if (i + len > size) return false;
    tag.data.assign(p + i, p + i + len);
    i += len;
    const bool end = tag.code == kEnd;
    out->push_back(std::move(tag));
    if (end) return true;
  }
  return true;
}

void PutU16(std::vector<uint8_t>* out, uint16_t v) {
  out->push_back(static_cast<uint8_t>(v));
  out->push_back(static_cast<uint8_t>(v >> 8));
}
void PutU32(std::vector<uint8_t>* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out->push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void WriteTags(const std::vector<Tag>& tags, std::vector<uint8_t>* out) {
  for (const Tag& tag : tags) {
    const size_t len = tag.data.size();
    if (len < 0x3f) {
      PutU16(out, static_cast<uint16_t>((tag.code << 6) | len));
    } else {
      PutU16(out, static_cast<uint16_t>((tag.code << 6) | 0x3f));
      PutU32(out, static_cast<uint32_t>(len));
    }
    out->insert(out->end(), tag.data.begin(), tag.data.end());
  }
}

// Reads a RECT (twips) at the start of p.

class BitReader {
 public:
  BitReader(const uint8_t* p, size_t size) : p_(p), size_(size) {}
  uint32_t U(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
      const size_t byte = pos_ >> 3;
      const uint32_t bit = byte < size_ ? (p_[byte] >> (7 - (pos_ & 7))) & 1 : 0;
      v = (v << 1) | bit;
      ++pos_;
    }
    return v;
  }
  int32_t S(int n) {
    if (n == 0) return 0;
    const uint32_t v = U(n);
    return (v & (1u << (n - 1))) ? static_cast<int32_t>(v) - static_cast<int32_t>(1u << n)
                                 : static_cast<int32_t>(v);
  }

 private:
  const uint8_t* p_;
  size_t size_;
  size_t pos_ = 0;
};

Rect ReadRect(const uint8_t* p, size_t size) {
  BitReader r(p, size);
  const int n = static_cast<int>(r.U(5));
  Rect rect;
  rect.x0 = r.S(n);
  rect.x1 = r.S(n);
  rect.y0 = r.S(n);
  rect.y1 = r.S(n);
  return rect;
}

class BitWriter {
 public:
  void U(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) Bit((v >> i) & 1);
  }
  void S(int32_t v, int n) { U(static_cast<uint32_t>(v) & ((n >= 32) ? ~0u : ((1u << n) - 1)), n); }
  void Align() {
    while (bits_ != 0) Bit(0);
  }
  std::vector<uint8_t>& bytes() { return out_; }

 private:
  void Bit(uint32_t b) {
    if (bits_ == 0) out_.push_back(0);
    if (b) out_.back() |= static_cast<uint8_t>(0x80 >> bits_);
    bits_ = (bits_ + 1) & 7;
  }
  std::vector<uint8_t> out_;
  int bits_ = 0;
};

int SignedBits(int32_t v) {
  int n = 1;
  while (n < 32 && (v < -(1 << (n - 1)) || v > (1 << (n - 1)) - 1)) ++n;
  return n;
}

void WriteRect(BitWriter* w, const Rect& r) {
  const int n = std::max({SignedBits(r.x0), SignedBits(r.x1), SignedBits(r.y0), SignedBits(r.y1)});
  w->U(n, 5);
  w->S(r.x0, n);
  w->S(r.x1, n);
  w->S(r.y0, n);
  w->S(r.y1, n);
  w->Align();
}

// A filled outline in twips: contours of straight and quadratic edges, each with the fill on
// its left (fill0) and on its right (fill1).
struct Edge {
  bool curve = false;
  int32_t cx = 0, cy = 0;  // control point (absolute)
  int32_t x = 0, y = 0;    // end point (absolute)
};
struct Contour {
  int32_t x = 0, y = 0;  // start point
  int fill0 = 0, fill1 = 0;
  std::vector<Edge> edges;
};

constexpr int kFillBits = 2;

std::vector<uint8_t> DefineShape3(uint16_t id, const Rect& bounds,
                                  const std::vector<uint32_t>& colors,  // RGBA, fill 1..n
                                  const std::vector<Contour>& contours) {
  BitWriter w;
  w.U(id & 0xff, 8);
  w.U(id >> 8, 8);
  WriteRect(&w, bounds);
  w.U(static_cast<uint32_t>(colors.size()), 8);
  for (uint32_t c : colors) {
    w.U(0x00, 8);  // solid fill
    w.U(c >> 24, 8);
    w.U((c >> 16) & 0xff, 8);
    w.U((c >> 8) & 0xff, 8);
    w.U(c & 0xff, 8);
  }
  w.U(0, 8);  // no line styles
  w.U(kFillBits, 4);
  w.U(0, 4);  // line style bits
  for (const Contour& contour : contours) {
    // Style change: new fills and a move to the contour start.
    w.U(0, 1);
    w.U(0, 1);  // new styles
    w.U(0, 1);  // line style
    w.U(1, 1);  // fill style 1
    w.U(1, 1);  // fill style 0
    w.U(1, 1);  // move to
    const int mb = std::max(SignedBits(contour.x), SignedBits(contour.y));
    w.U(mb, 5);
    w.S(contour.x, mb);
    w.S(contour.y, mb);
    w.U(contour.fill0, kFillBits);
    w.U(contour.fill1, kFillBits);
    int32_t px = contour.x, py = contour.y;
    for (const Edge& e : contour.edges) {
      if (e.curve) {
        const int32_t cdx = e.cx - px, cdy = e.cy - py, adx = e.x - e.cx, ady = e.y - e.cy;
        const int n = std::max({SignedBits(cdx), SignedBits(cdy), SignedBits(adx), SignedBits(ady), 2});
        w.U(1, 1);
        w.U(0, 1);
        w.U(n - 2, 4);
        w.S(cdx, n);
        w.S(cdy, n);
        w.S(adx, n);
        w.S(ady, n);
      } else {
        const int32_t dx = e.x - px, dy = e.y - py;
        if (dx == 0 && dy == 0) continue;
        const int n = std::max({SignedBits(dx), SignedBits(dy), 2});
        w.U(1, 1);
        w.U(1, 1);
        w.U(n - 2, 4);
        w.U(1, 1);  // general line
        w.S(dx, n);
        w.S(dy, n);
      }
      px = e.x;
      py = e.y;
    }
  }
  w.U(0, 6);  // end of shape
  w.Align();
  return std::move(w.bytes());
}

// Rounded rectangle with its inside on the left of each edge.
Contour RoundedRect(double x0, double y0, double x1, double y1, double r, int fill0, int fill1) {
  r = std::max(0.0, std::min({r, (x1 - x0) / 2, (y1 - y0) / 2}));
  auto P = [](double v) { return static_cast<int32_t>(std::lround(v)); };
  Contour c;
  c.fill0 = fill0;
  c.fill1 = fill1;
  c.x = P(x0);
  c.y = P(y0 + r);
  auto line = [&](double x, double y) { c.edges.push_back({false, 0, 0, P(x), P(y)}); };
  auto curve = [&](double cx, double cy, double x, double y) {
    c.edges.push_back({true, P(cx), P(cy), P(x), P(y)});
  };
  // Down the left side, along the bottom, up the right side, back along the top.
  line(x0, y1 - r);
  curve(x0, y1, x0 + r, y1);
  line(x1 - r, y1);
  curve(x1, y1, x1, y1 - r);
  line(x1, y0 + r);
  curve(x1, y0, x1 - r, y0);
  line(x0 + r, y0);
  curve(x0, y0, x0, y0 + r);
  return c;
}

struct Style {
  uint32_t border, face, text;
};

Style StyleFor(std::string_view icon) {
  if (icon.find("_Grey") != std::string_view::npos) return {0x3A3A3AFF, 0x8C8C8CFF, 0x2A2A2AFF};
  if (icon.find("_Down") != std::string_view::npos) return {0x1E1E1EFF, 0xC4C4C4FF, 0x1E1E1EFF};
  if (icon.find("_Killer") != std::string_view::npos) return {0x5A1010FF, 0xFFC94AFF, 0x3A0A0AFF};
  return {0x1E1E1EFF, 0xF4F4F4FF, 0x1E1E1EFF};
}

// Glyph outlines of a label, centered in a box and as large as fits.
bool LabelContours(const stbtt_fontinfo& font, const std::string& label, double cx, double cy,
                   double max_w, double max_h, int fill0, int fill1, std::vector<Contour>* out) {
  if (label.empty()) return false;
  // Layout in font units.
  struct Placed {
    int cp;
    double x;
  };
  std::vector<Placed> placed;
  double pen = 0;
  double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
  for (size_t i = 0; i < label.size(); ++i) {
    const int cp = static_cast<unsigned char>(label[i]);
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&font, cp, &advance, &lsb);
    int bx0, by0, bx1, by1;
    if (stbtt_GetCodepointBox(&font, cp, &bx0, &by0, &bx1, &by1)) {
      minx = std::min(minx, pen + bx0);
      maxx = std::max(maxx, pen + bx1);
      miny = std::min(miny, double(by0));
      maxy = std::max(maxy, double(by1));
    }
    placed.push_back({cp, pen});
    pen += advance;
    if (i + 1 < label.size()) {
      pen += stbtt_GetCodepointKernAdvance(&font, cp, static_cast<unsigned char>(label[i + 1]));
    }
  }
  if (maxx <= minx || maxy <= miny) return false;
  const double k = std::min(max_w / (maxx - minx), max_h / (maxy - miny));
  const double ux = (minx + maxx) / 2, uy = (miny + maxy) / 2;
  auto X = [&](double v) { return static_cast<int32_t>(std::lround(cx + (v - ux) * k)); };
  auto Y = [&](double v) { return static_cast<int32_t>(std::lround(cy - (v - uy) * k)); };
  for (const Placed& g : placed) {
    stbtt_vertex* verts = nullptr;
    const int n = stbtt_GetCodepointShape(&font, g.cp, &verts);
    Contour* cur = nullptr;
    for (int i = 0; i < n; ++i) {
      const stbtt_vertex& v = verts[i];
      const int32_t x = X(g.x + v.x), y = Y(v.y);
      if (v.type == STBTT_vmove) {
        if (cur && (cur->edges.empty() || cur->edges.back().x != cur->x ||
                    cur->edges.back().y != cur->y)) {
          if (cur) cur->edges.push_back({false, 0, 0, cur->x, cur->y});
        }
        out->push_back({});
        cur = &out->back();
        cur->x = x;
        cur->y = y;
        cur->fill0 = fill0;
        cur->fill1 = fill1;
      } else if (cur && v.type == STBTT_vline) {
        cur->edges.push_back({false, 0, 0, x, y});
      } else if (cur && v.type == STBTT_vcurve) {
        cur->edges.push_back({true, X(g.x + v.cx), Y(v.cy), x, y});
      } else if (cur && v.type == STBTT_vcubic) {
        // Not in TrueType fonts; approximate with one quadratic through the midpoint.
        cur->edges.push_back({true, X(g.x + (v.cx + v.cx1) / 2.0), Y((v.cy + v.cy1) / 2.0), x, y});
      }
    }
    if (cur && !cur->edges.empty() &&
        (cur->edges.back().x != cur->x || cur->edges.back().y != cur->y)) {
      cur->edges.push_back({false, 0, 0, cur->x, cur->y});
    }
    stbtt_FreeShape(&font, verts);
  }
  return true;
}

// Bumpers and triggers are narrow slivers in the original art: give them a square cap of the
// same height or width, centred where the icon was. Wide pills (Start, Back) stay wide.
Rect KeycapBounds(const Rect& original) {
  Rect b = original;
  const int32_t w = b.x1 - b.x0, h = b.y1 - b.y0;
  const double aspect = h ? double(w) / h : 1.0;
  if (aspect > 0.4 && aspect < 2.5) {
    const int32_t side = std::max(w, h), cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
    b = {cx - side / 2, cx + side / 2, cy - side / 2, cy + side / 2};
  }
  return b;
}

// The face of the keycap inside its border, where the label goes.
Rect KeycapFace(const Rect& b) {
  const double m = std::min(b.x1 - b.x0, b.y1 - b.y0);
  const int32_t in = static_cast<int32_t>(std::lround(m * 0.04 + m * 0.08));
  return {b.x0 + in, b.x1 - in, b.y0 + in, b.y1 - in};
}

std::vector<uint8_t> KeycapShape(uint16_t id, const Rect& original, const std::string& label,
                                 const Style& style, const stbtt_fontinfo& font) {
  const Rect b = KeycapBounds(original);
  const double w = b.x1 - b.x0, h = b.y1 - b.y0, m = std::min(w, h);
  const double inset = m * 0.04, border = m * 0.08, radius = m * 0.24;
  std::vector<Contour> contours;
  // Fills: 1 border, 2 face, 3 text.
  contours.push_back(
      RoundedRect(b.x0 + inset, b.y0 + inset, b.x1 - inset, b.y1 - inset, radius, 1, 0));
  const double ix0 = b.x0 + inset + border, iy0 = b.y0 + inset + border;
  const double ix1 = b.x1 - inset - border, iy1 = b.y1 - inset - border;
  contours.push_back(RoundedRect(ix0, iy0, ix1, iy1, radius - border, 2, 1));
  const double lw = (ix1 - ix0) * 0.84, lh = (iy1 - iy0) * (label.size() == 1 ? 0.62 : 0.5);
  LabelContours(font, label, (ix0 + ix1) / 2, (iy0 + iy1) / 2, lw, lh, 3, 2, &contours);
  return DefineShape3(id, b, {style.border, style.face, style.text}, contours);
}

bool IsShapeOrText(uint16_t code) {
  return code == kDefineShape || code == kDefineShape2 || code == kDefineShape3 ||
         code == kDefineShape4 || code == kDefineText || code == kDefineText2;
}

// The size in bytes of the MATRIX at the start of p.
size_t MatrixSize(const uint8_t* p, size_t size) {
  BitReader r(p, size);
  size_t bits = 0;
  auto read = [&](int n) {
    bits += n;
    return r.U(n);
  };
  if (read(1)) {
    const int n = static_cast<int>(read(5));
    read(n);
    read(n);
  }
  if (read(1)) {
    const int n = static_cast<int>(read(5));
    read(n);
    read(n);
  }
  const int n = static_cast<int>(read(5));
  read(n);
  read(n);
  return std::min(size, (bits + 7) / 8);
}

// The character a PlaceObject2/3 places, and where its id is stored (0 if none).
size_t PlacedCharacterOffset(const Tag& t) {
  if (t.code == kPlaceObject2) {
    if (t.data.size() < 5 || !(t.data[0] & 0x02)) return 0;
    return 3;
  }
  if (t.code == kPlaceObject3) {
    if (t.data.size() < 6 || !(t.data[0] & 0x02)) return 0;
    size_t q = 4;
    if (t.data[1] & 0x08) {
      while (q < t.data.size() && t.data[q]) ++q;
      ++q;
    }
    return q + 2 <= t.data.size() ? q : 0;
  }
  return 0;
}


// Key labels are drawn letter by letter: one sprite with a frame per printable character (frame
// 1 empty, frame c - 31 for c in '!'..'~'), its glyph kGlyphHeight twips tall from ascent to
// descent, origin on the baseline at the pen.
constexpr int kFirstChar = 33, kLastChar = 126;
constexpr double kGlyphHeight = 2000.0;
constexpr uint32_t kLabelColor = 0x1E1E1EFF;

double GlyphScale(const stbtt_fontinfo& font) {
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
  return ascent > descent ? kGlyphHeight / (ascent - descent) : 1.0;
}

int CharFrame(int cp) { return cp >= kFirstChar && cp <= kLastChar ? cp - kFirstChar + 2 : 1; }

// One glyph as a shape (fill 1 on the left of every edge, as LabelContours draws it).
bool GlyphShape(uint16_t id, const stbtt_fontinfo& font, int cp, std::vector<uint8_t>* out) {
  const double g = GlyphScale(font);
  auto X = [&](double v) { return static_cast<int32_t>(std::lround(v * g)); };
  auto Y = [&](double v) { return static_cast<int32_t>(std::lround(-v * g)); };
  stbtt_vertex* verts = nullptr;
  const int n = stbtt_GetCodepointShape(&font, cp, &verts);
  std::vector<Contour> contours;
  Rect box{INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN};
  auto grow = [&](int32_t x, int32_t y) {
    box.x0 = std::min(box.x0, x);
    box.x1 = std::max(box.x1, x);
    box.y0 = std::min(box.y0, y);
    box.y1 = std::max(box.y1, y);
  };
  auto close = [&]() {
    if (!contours.empty()) {
      Contour& c = contours.back();
      if (c.edges.empty() || c.edges.back().x != c.x || c.edges.back().y != c.y)
        c.edges.push_back({false, 0, 0, c.x, c.y});
    }
  };
  for (int i = 0; i < n; ++i) {
    const stbtt_vertex& v = verts[i];
    const int32_t x = X(v.x), y = Y(v.y);
    grow(x, y);
    if (v.type == STBTT_vmove) {
      close();
      contours.push_back({});
      contours.back().x = x;
      contours.back().y = y;
      contours.back().fill0 = 1;
      contours.back().fill1 = 0;
    } else if (contours.empty()) {
      continue;
    } else if (v.type == STBTT_vline) {
      contours.back().edges.push_back({false, 0, 0, x, y});
    } else if (v.type == STBTT_vcurve) {
      contours.back().edges.push_back({true, X(v.cx), Y(v.cy), x, y});
    } else if (v.type == STBTT_vcubic) {
      contours.back().edges.push_back(
          {true, X((v.cx + v.cx1) / 2.0), Y((v.cy + v.cy1) / 2.0), x, y});
    }
  }
  close();
  stbtt_FreeShape(&font, verts);
  if (contours.empty()) return false;
  *out = DefineShape3(id, box, {kLabelColor}, contours);
  return true;
}

void PlaceNamed(std::vector<uint8_t>* out, uint16_t depth, uint16_t character,
                const std::string& name) {
  out->push_back(0x02 | 0x20);  // has character, has name
  PutU16(out, depth);
  PutU16(out, character);
  out->insert(out->end(), name.begin(), name.end());
  out->push_back(0);
}

// The sprite of the label letters, after the shapes it shows.
std::vector<Tag> GlyphSprite(uint16_t sprite_id, uint16_t* next_id, const stbtt_fontinfo& font) {
  std::vector<Tag> defs;
  std::vector<Tag> frames;
  frames.push_back({kShowFrame, {}});  // frame 1: empty
  bool placed = false;
  for (int cp = kFirstChar; cp <= kLastChar; ++cp) {
    if (placed) {
      Tag remove{kRemoveObject2, {}};
      PutU16(&remove.data, 1);
      frames.push_back(std::move(remove));
      placed = false;
    }
    std::vector<uint8_t> shape;
    const uint16_t id = *next_id;
    if (GlyphShape(id, font, cp, &shape)) {
      ++*next_id;
      defs.push_back({kDefineShape3, std::move(shape)});
      Tag place{kPlaceObject2, {0x02}};
      PutU16(&place.data, 1);
      PutU16(&place.data, id);
      frames.push_back(std::move(place));
      placed = true;
    }
    frames.push_back({kShowFrame, {}});
  }
  frames.push_back({kEnd, {}});
  Tag sprite{kDefineSprite, {}};
  PutU16(&sprite.data, sprite_id);
  PutU16(&sprite.data, static_cast<uint16_t>(kLastChar - kFirstChar + 2));
  WriteTags(frames, &sprite.data);
  defs.push_back(std::move(sprite));
  return defs;
}

// The icon's script (see icon_library.h). Positions in twips and scales in hundredths of a
// percent are divided in the script, so the written numbers stay integers.
class Script {
 public:
  explicit Script(uint16_t first_id) : first_id_(first_id) {}
  void Value(int field, int32_t value) {
    Push(kMarker | uint16_t(first_id_ + field));
    code_.push_back(0x17);  // pop
    Push(static_cast<uint32_t>(kValueBias + value));
    Push(static_cast<uint32_t>(kValueBias));
    code_.push_back(0x0B);  // subtract
  }
  void SetProperty(const std::string& target, int property, int field, int32_t value, int divisor) {
    code_.push_back(0x96);  // push target, property
    PutU16(&code_, static_cast<uint16_t>(1 + target.size() + 1 + 1 + 4));
    code_.push_back(0x00);
    code_.insert(code_.end(), target.begin(), target.end());
    code_.push_back(0);
    code_.push_back(0x07);
    PutU32(&code_, static_cast<uint32_t>(property));
    Value(field, value);
    if (divisor > 1) {
      Push(static_cast<uint32_t>(divisor));
      code_.push_back(0x0D);  // divide
    }
    code_.push_back(0x23);  // set property
  }
  void GotoAndStop(const std::string& target, int field, int32_t frame) {
    SetTarget(target);
    Value(field, frame);
    code_.push_back(0x9F);  // goto frame 2, stop
    PutU16(&code_, 1);
    code_.push_back(0x00);
    SetTarget("");
  }
  std::vector<uint8_t> Finish() {
    code_.push_back(0x00);
    return std::move(code_);
  }

 private:
  void Push(uint32_t v) {
    code_.push_back(0x96);
    PutU16(&code_, 5);
    code_.push_back(0x07);
    PutU32(&code_, v);
  }
  void SetTarget(const std::string& target) {
    code_.push_back(0x8B);
    PutU16(&code_, static_cast<uint16_t>(target.size() + 1));
    code_.insert(code_.end(), target.begin(), target.end());
    code_.push_back(0);
  }
  uint16_t first_id_;
  std::vector<uint8_t> code_;
};

constexpr int kPropX = 0, kPropY = 1, kPropXScale = 2, kPropYScale = 3, kPropVisible = 7;

std::vector<int32_t> Values(const Icon& icon, const std::string& label, bool keyboard,
                            const stbtt_fontinfo& font) {
  std::vector<int32_t> v(kFields, 0);
  v[0] = 1;
  for (int s = 0; s < kSlots; ++s) {
    v[2 + s * 5] = 1;
    v[2 + s * 5 + 3] = 10000;
  }
  if (!keyboard || label.empty()) return v;
  // Layout in font units, as LabelContours.
  std::vector<std::pair<int, double>> placed;
  double pen = 0, minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
  for (size_t i = 0; i < label.size() && placed.size() < size_t(kSlots); ++i) {
    const int cp = static_cast<unsigned char>(label[i]);
    int advance = 0, lsb = 0, bx0, by0, bx1, by1;
    stbtt_GetCodepointHMetrics(&font, cp, &advance, &lsb);
    if (stbtt_GetCodepointBox(&font, cp, &bx0, &by0, &bx1, &by1)) {
      minx = std::min(minx, pen + bx0);
      maxx = std::max(maxx, pen + bx1);
      miny = std::min(miny, double(by0));
      maxy = std::max(maxy, double(by1));
    }
    placed.push_back({cp, pen});
    pen += advance;
    if (i + 1 < label.size())
      pen += stbtt_GetCodepointKernAdvance(&font, cp, static_cast<unsigned char>(label[i + 1]));
  }
  if (maxx <= minx || maxy <= miny) return v;
  const Rect& f = icon.label_box;
  const double lw = (f.x1 - f.x0) * 0.84, lh = (f.y1 - f.y0) * (placed.size() == 1 ? 0.62 : 0.5);
  const double k = std::min(lw / (maxx - minx), lh / (maxy - miny));
  const double cx = (f.x0 + f.x1) / 2.0, cy = (f.y0 + f.y1) / 2.0;
  const double ux = (minx + maxx) / 2, uy = (miny + maxy) / 2;
  const int32_t scale = static_cast<int32_t>(std::lround(k / GlyphScale(font) * 10000.0));
  v[0] = 0;
  v[1] = 1;
  for (size_t s = 0; s < placed.size(); ++s) {
    int32_t* slot = &v[2 + s * 5];
    slot[0] = CharFrame(placed[s].first);
    slot[1] = static_cast<int32_t>(std::lround(cx + (placed[s].second - ux) * k));
    slot[2] = static_cast<int32_t>(std::lround(cy + uy * k));
    slot[3] = scale;
    slot[4] = 1;
  }
  return v;
}

// The icon's script on its first frame, with its values as they start.
std::vector<uint8_t> IconScript(const Icon& icon, const std::vector<int32_t>& v) {
  Script script(icon.first_id);
  script.SetProperty("o", kPropVisible, 0, v[0], 1);
  script.SetProperty("k", kPropVisible, 1, v[1], 1);
  for (int s = 0; s < kSlots; ++s) {
    const std::string g = "k/g" + std::to_string(s);
    const int f = 2 + s * 5;
    script.GotoAndStop(g, f, v[f]);
    script.SetProperty(g, kPropX, f + 1, v[f + 1], 20);
    script.SetProperty(g, kPropY, f + 2, v[f + 2], 20);
    script.SetProperty(g, kPropXScale, f + 3, v[f + 3], 100);
    script.SetProperty(g, kPropYScale, f + 3, v[f + 3], 100);
    script.SetProperty(g, kPropVisible, f + 4, v[f + 4], 1);
  }
  return script.Finish();
}

}  // namespace

Result RewriteIconLibrary(const uint8_t* data, size_t size, const LabelLookup& labels,
                          const uint8_t* font_data, size_t font_size, bool keyboard,
                          uint16_t first_id) {
  Result result;
  if (size < 8 || data[1] != 'F' || data[2] != 'X') {
    result.error = "not a Scaleform movie";
    return result;
  }
  const uint32_t full_size = U32(data + 4);
  std::vector<uint8_t> body;
  if (data[0] == 'C') {
    int out_len = 0;
    char* inflated = stbi_zlib_decode_malloc(reinterpret_cast<const char*>(data + 8),
                                             static_cast<int>(size - 8), &out_len);
    if (!inflated) {
      result.error = "cannot decompress the movie";
      return result;
    }
    body.assign(inflated, inflated + out_len);
    std::free(inflated);
  } else if (data[0] == 'G') {
    body.assign(data + 8, data + size);
  } else {
    result.error = "unknown movie signature";
    return result;
  }
  // The stored stream can carry padding past the size the header gives.
  if (full_size < 8 || body.size() + 8 < full_size) {
    result.error = "movie size mismatch";
    return result;
  }
  body.resize(full_size - 8);

  stbtt_fontinfo font;
  if (!font_data || !stbtt_InitFont(&font, font_data, stbtt_GetFontOffsetForIndex(font_data, 0))) {
    result.error = "cannot read the label font";
    return result;
  }
  (void)font_size;

  // Header: RECT, frame rate, frame count; then the tags.
  BitReader rect_reader(body.data(), body.size());
  const int nbits = static_cast<int>(rect_reader.U(5));
  const size_t header = (5 + 4 * nbits + 7) / 8 + 4;
  std::vector<Tag> tags;
  if (header > body.size() || !ParseTags(body.data() + header, body.size() - header, &tags)) {
    result.error = "cannot read the movie tags";
    return result;
  }

  std::map<uint16_t, Rect> bounds;  // shapes and static texts
  std::map<uint16_t, size_t> sprite_tag;
  std::map<std::string, uint16_t> exports;
  uint16_t max_id = 0;
  for (size_t i = 0; i < tags.size(); ++i) {
    const Tag& t = tags[i];
    if (t.data.size() >= 2 && (IsShapeOrText(t.code) || t.code == kDefineSprite)) {
      const uint16_t id = U16(t.data.data());
      max_id = std::max(max_id, id);
      if (t.code == kDefineSprite) {
        sprite_tag[id] = i;
      } else {
        bounds[id] = ReadRect(t.data.data() + 2, t.data.size() - 2);
      }
    } else if (t.code == kExportAssets && t.data.size() >= 2) {
      const uint16_t count = U16(t.data.data());
      size_t q = 2;
      for (uint16_t n = 0; n < count && q + 2 < t.data.size(); ++n) {
        const uint16_t id = U16(t.data.data() + q);
        q += 2;
        std::string name;
        while (q < t.data.size() && t.data[q]) name.push_back(static_cast<char>(t.data[q++]));
        ++q;
        exports[name] = id;
      }
    }
  }

  // New definitions, by the index of the sprite tag they go in front of.
  std::multimap<size_t, Tag> inserts;
  uint16_t next_id = static_cast<uint16_t>(max_id + 1);
  const uint16_t glyphs_id = next_id++;
  std::vector<Tag> glyphs = GlyphSprite(glyphs_id, &next_id, font);
  size_t first_icon = SIZE_MAX;
  for (const auto& [name, sprite_id] : exports) {
    if (name.compare(0, kPrefix.size(), kPrefix) != 0) continue;
    const std::string icon_name = name.substr(kPrefix.size());
    const std::string label = labels(icon_name);
    if (label.empty()) continue;
    auto st = sprite_tag.find(sprite_id);
    if (st == sprite_tag.end()) continue;
    Tag& sprite = tags[st->second];
    if (sprite.data.size() < 4 || U16(sprite.data.data() + 2) != 1) continue;  // still icons only
    std::vector<Tag> inner;
    if (!ParseTags(sprite.data.data() + 4, sprite.data.size() - 4, &inner)) continue;
    // The largest shape the sprite shows is the button itself.
    int64_t best = -1;
    Rect box;
    std::vector<uint8_t> matrix;  // where the sprite places that shape
    for (const Tag& t : inner) {
      const size_t off = PlacedCharacterOffset(t);
      if (!off) continue;
      auto b = bounds.find(U16(t.data.data() + off));
      if (b == bounds.end()) continue;
      const int64_t area = int64_t(b->second.x1 - b->second.x0) * (b->second.y1 - b->second.y0);
      if (area > best) {
        best = area;
        box = b->second;
        matrix.clear();
        if (t.data[0] & 0x04) {
          const size_t len = MatrixSize(t.data.data() + off + 2, t.data.size() - off - 2);
          matrix.assign(t.data.begin() + off + 2, t.data.begin() + off + 2 + len);
        }
      }
    }
    if (best < 0) continue;
    // The original look moves into a sprite of its own ("o"); the keycap without its label and
    // the letters ("g0".."g7") go on top in a sprite ("k") placed where the button was, and the
    // icon loops over two frames so its script keeps applying the values.
    const uint16_t original_id = next_id++, cap_id = next_id++, keycap_id = next_id++;
    Tag original{kDefineSprite, {}};
    PutU16(&original.data, original_id);
    PutU16(&original.data, 1);
    original.data.insert(original.data.end(), sprite.data.begin() + 4, sprite.data.end());
    inserts.emplace(st->second, std::move(original));
    inserts.emplace(st->second, Tag{kDefineShape3, KeycapShape(cap_id, box, "", StyleFor(icon_name), font)});

    std::vector<Tag> cap_frames;
    Tag cap_place{kPlaceObject2, {0x02}};
    PutU16(&cap_place.data, 1);
    PutU16(&cap_place.data, cap_id);
    cap_frames.push_back(std::move(cap_place));
    for (int s = 0; s < kSlots; ++s) {
      Tag slot{kPlaceObject2, {}};
      PlaceNamed(&slot.data, static_cast<uint16_t>(2 + s), glyphs_id, "g" + std::to_string(s));
      cap_frames.push_back(std::move(slot));
    }
    cap_frames.push_back({kShowFrame, {}});
    cap_frames.push_back({kEnd, {}});
    Tag keycap{kDefineSprite, {}};
    PutU16(&keycap.data, keycap_id);
    PutU16(&keycap.data, 1);
    WriteTags(cap_frames, &keycap.data);
    inserts.emplace(st->second, std::move(keycap));

    Icon icon;
    icon.name = icon_name;
    icon.label_box = KeycapFace(KeycapBounds(box));
    if ((result.list.size() + 1) * kFields > kIdsPerLibrary) break;
    icon.first_id = static_cast<uint16_t>(first_id + result.list.size() * kFields);
    std::vector<Tag> frames;
    Tag place{kPlaceObject2, {}};
    PlaceNamed(&place.data, 1, original_id, "o");
    frames.push_back(place);
    place.data = {static_cast<uint8_t>(0x02 | 0x20 | (matrix.empty() ? 0 : 0x04))};
    PutU16(&place.data, 2);
    PutU16(&place.data, keycap_id);
    place.data.insert(place.data.end(), matrix.begin(), matrix.end());
    for (char c : std::string("k")) place.data.push_back(static_cast<uint8_t>(c));
    place.data.push_back(0);
    frames.push_back(place);
    frames.push_back({kDoAction, IconScript(icon, Values(icon, label, keyboard, font))});
    frames.push_back({kShowFrame, {}});
    frames.push_back({kShowFrame, {}});
    frames.push_back({kEnd, {}});
    std::vector<uint8_t> sprite_data;
    PutU16(&sprite_data, sprite_id);
    PutU16(&sprite_data, 2);
    WriteTags(frames, &sprite_data);
    sprite.data = std::move(sprite_data);
    first_icon = std::min(first_icon, st->second);
    result.list.push_back(std::move(icon));
    ++result.icons;
  }
  if (result.icons == 0) {
    result.error = "no button icons found";
    return result;
  }

  std::vector<Tag> rebuilt;
  rebuilt.reserve(tags.size() + inserts.size());
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i == first_icon)
      for (Tag& t : glyphs) rebuilt.push_back(std::move(t));
    auto [lo, hi] = inserts.equal_range(i);
    for (auto it = lo; it != hi; ++it) rebuilt.push_back(std::move(it->second));
    rebuilt.push_back(std::move(tags[i]));
  }
  std::vector<uint8_t> out_body(body.begin(), body.begin() + header);
  WriteTags(rebuilt, &out_body);

  result.movie = {'G', 'F', 'X', data[3]};
  PutU32(&result.movie, static_cast<uint32_t>(out_body.size() + 8));
  result.movie.insert(result.movie.end(), out_body.begin(), out_body.end());
  return result;
}

std::vector<int32_t> IconValues(const Icon& icon, const std::string& label, bool keyboard,
                                const uint8_t* font_data) {
  stbtt_fontinfo font;
  if (!font_data || !stbtt_InitFont(&font, font_data, stbtt_GetFontOffsetForIndex(font_data, 0)))
    return {};
  return Values(icon, label, keyboard, font);
}

}  // namespace icon_library
