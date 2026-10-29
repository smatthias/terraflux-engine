#include <terraflux/assets/material.hpp>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sstream>
#include <cmath>
namespace terraflux::assets {
namespace {
std::string token(std::istream& in) {
  std::string t;
  while (in >> t) {
    if (t.starts_with('#')) {
      in.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
      continue;
    }
    return t;
  }
  throw std::invalid_argument("truncated PPM P3");
}
std::uint32_t number(const std::string& s,std::uint32_t maximum) {
  unsigned v=0;
  auto r=std::from_chars(s.data(),s.data()+s.size(),v);
  if (r.ec!=std::errc{} || r.ptr!=s.data()+s.size() || v>maximum)
    throw std::invalid_argument("invalid PPM integer");
  return v;
}
void check_size(std::uint32_t w,std::uint32_t h) {
  if (!w || !h || w>4096 || h>4096 ||
      static_cast<std::uint64_t>(w)*h>4'000'000)
    throw std::invalid_argument("PPM dimensions exceed limits");
}
}
ImageRgba8 read_ppm_p3(std::istream& in) {
  if (token(in)!="P3") throw std::invalid_argument("expected ASCII PPM P3");
  const auto w=number(token(in),4096),h=number(token(in),4096);
  check_size(w,h);
  if (number(token(in),255)!=255)
    throw std::invalid_argument("PPM maxval must equal 255");
  ImageRgba8 out{w,h,{}};
  out.pixels.resize(static_cast<std::size_t>(w)*h*4);
  for (std::uint32_t y=0;y<h;++y) for(std::uint32_t x=0;x<w;++x) {
    const auto i=(static_cast<std::size_t>(h-1-y)*w+x)*4;
    for (int c=0;c<3;++c) out.pixels[i+c]=static_cast<std::uint8_t>(number(token(in),255));
    out.pixels[i+3]=255;
  }
  std::string extra;
  while(in>>extra) {
    if (extra.starts_with('#')) {
      in.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
      continue;
    }
    throw std::invalid_argument("PPM has trailing pixel values");
  }
  if (in.bad()) throw std::runtime_error("PPM read error");
  return out;
}

namespace {
std::string trim(const std::string& input) {
  auto first=input.find_first_not_of(" \t\r\n");
  if (first==std::string::npos) return {};
  auto last=input.find_last_not_of(" \t\r\n");
  return input.substr(first,last-first+1);
}
double component(const std::string& s) {
  std::size_t end=0;
  double v=0;
  try { v=std::stod(s,&end); }
  catch (const std::exception&) { throw std::invalid_argument("invalid MTL Kd component"); }
  if (end!=s.size() || !std::isfinite(v) || v<0.0 || v>1.0)
    throw std::invalid_argument("MTL Kd color must be finite and within [0,1]");
  return v;
}
} // namespace

MaterialLibrary parse_mtl(std::istream& input) {
  MaterialLibrary materials;
  std::string line;
  std::string active;
  std::size_t line_count=0;
  while (std::getline(input,line)) {
    ++line_count;
    if (line_count>100'000 || line.size()>8192)
      throw std::length_error("MTL file exceeds size limit");
    auto clean=trim(line.substr(0,line.find('#')));
    if (clean.empty()) continue;
    std::istringstream row(clean);
    std::string keyword;
    row>>keyword;
    if (keyword=="newmtl") {
      std::string name;
      row>>name;
      if (name.empty() || !materials.emplace(name,Material{}).second)
        throw std::invalid_argument("duplicate or empty MTL material name");
      if (materials.size()>4096) throw std::length_error("too many MTL materials");
      active=name;
    } else if (keyword=="Kd") {
      if (active.empty()) throw std::invalid_argument("MTL Kd before newmtl");
      std::string r,g,b,extra;
      if (!(row>>r>>g>>b) || (row>>extra))
        throw std::invalid_argument("MTL Kd expects exactly RGB");
      materials.at(active).base_color = {component(r),component(g),component(b)};
    } else if (keyword=="map_Kd") {
      if (active.empty()) throw std::invalid_argument("MTL texture before newmtl");
      std::string file;
      std::getline(row,file);
      file=trim(file);
      if (file.empty() || file.starts_with('-'))
        throw std::invalid_argument("MTL map_Kd requires plain relative filename");
      materials.at(active).albedo_map=file;
    } else {
      // Ignore MTL metadata unsupported by this initial diffuse-material
      // pipeline (Ns, Ka, Ks, illum, d, Tr, bump etc.).
    }
  }
  if (input.bad()) throw std::runtime_error("error reading MTL stream");
  if (materials.empty()) throw std::invalid_argument("MTL contains no materials");
  return materials;
}

ImageRgba8 checkerboard_image(std::uint32_t size) {
  check_size(size,size);
  ImageRgba8 out{size,size,{}};
  out.pixels.reserve(static_cast<std::size_t>(size)*size*4);
  for(std::uint32_t y=0;y<size;++y) for(std::uint32_t x=0;x<size;++x) {
    bool high=((x/std::max(1u,size/4)+y/std::max(1u,size/4))%2)==0;
    std::uint8_t a=high?245:100;
    out.pixels.insert(out.pixels.end(),{a,a,static_cast<std::uint8_t>(high?255:220),255});
  }
  return out;
}
} // namespace terraflux::assets
