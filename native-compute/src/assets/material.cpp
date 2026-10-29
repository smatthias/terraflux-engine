#include <terraflux/assets/material.hpp>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
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
