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
struct Rect {
  int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

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

std::vector<uint8_t> KeycapShape(uint16_t id, const Rect& original, const std::string& label,
                                 const Style& style, const stbtt_fontinfo& font) {
  // Bumpers and triggers are narrow slivers in the original art: give them a square cap of the
  // same height or width, centred where the icon was. Wide pills (Start, Back) stay wide.
  Rect b = original;
  {
    const int32_t w = b.x1 - b.x0, h = b.y1 - b.y0;
    const double aspect = h ? double(w) / h : 1.0;
    if (aspect > 0.4 && aspect < 2.5) {
      const int32_t side = std::max(w, h), cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2;
      b = {cx - side / 2, cx + side / 2, cy - side / 2, cy + side / 2};
    }
  }
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

}  // namespace

Result RewriteIconLibrary(const uint8_t* data, size_t size, const LabelLookup& labels,
                          const uint8_t* font_data, size_t font_size) {
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

  // New keycap shapes, by the index of the sprite tag they go in front of.
  std::multimap<size_t, Tag> inserts;
  uint16_t next_id = static_cast<uint16_t>(max_id + 1);
  for (const auto& [name, sprite_id] : exports) {
    if (name.compare(0, kPrefix.size(), kPrefix) != 0) continue;
    const std::string icon = name.substr(kPrefix.size());
    const std::string label = labels(icon);
    if (label.empty()) continue;
    auto st = sprite_tag.find(sprite_id);
    if (st == sprite_tag.end()) continue;
    Tag& sprite = tags[st->second];
    if (sprite.data.size() < 4 || U16(sprite.data.data() + 2) != 1) continue;  // still icons only
    std::vector<Tag> inner;
    if (!ParseTags(sprite.data.data() + 4, sprite.data.size() - 4, &inner)) continue;
    // The largest shape the sprite shows is the button itself.
    size_t main = SIZE_MAX;
    int64_t best = -1;
    for (size_t i = 0; i < inner.size(); ++i) {
      const size_t off = PlacedCharacterOffset(inner[i]);
      if (!off) continue;
      auto b = bounds.find(U16(inner[i].data.data() + off));
      if (b == bounds.end()) continue;
      const int64_t area = int64_t(b->second.x1 - b->second.x0) * (b->second.y1 - b->second.y0);
      if (area > best) {
        best = area;
        main = i;
      }
    }
    if (main == SIZE_MAX) continue;
    const size_t off = PlacedCharacterOffset(inner[main]);
    const Rect box = bounds[U16(inner[main].data.data() + off)];
    const uint16_t shape_id = next_id++;
    inserts.emplace(st->second, Tag{kDefineShape3, KeycapShape(shape_id, box, label,
                                                                StyleFor(icon), font)});
    inner[main].data[off] = static_cast<uint8_t>(shape_id);
    inner[main].data[off + 1] = static_cast<uint8_t>(shape_id >> 8);
    // Drop everything else the sprite placed (letters, arrows, the START/BACK text).
    std::vector<Tag> kept;
    for (size_t i = 0; i < inner.size(); ++i) {
      if (i != main && (inner[i].code == kPlaceObject2 || inner[i].code == kPlaceObject3)) continue;
      kept.push_back(std::move(inner[i]));
    }
    std::vector<uint8_t> sprite_data(sprite.data.begin(), sprite.data.begin() + 4);
    WriteTags(kept, &sprite_data);
    sprite.data = std::move(sprite_data);
    ++result.icons;
  }
  if (result.icons == 0) {
    result.error = "no button icons found";
    return result;
  }

  std::vector<Tag> rebuilt;
  rebuilt.reserve(tags.size() + inserts.size());
  for (size_t i = 0; i < tags.size(); ++i) {
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

}  // namespace icon_library
