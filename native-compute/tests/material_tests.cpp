#include <terraflux/assets/material.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
void require(bool c,const char* s){if(!c)throw std::runtime_error(s);}
template <typename F> void rejects(F&& f,const char* s){
 bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,s);
}
void parse_image(){
 std::istringstream f(R"PPM(P3
# two rows
2 2
255
255 0 0  0 255 0
0 0 255  255 255 255
)PPM");
 auto img=terraflux::assets::read_ppm_p3(f);
 require(img.width==2&&img.height==2&&img.pixels.size()==16,"size");
 require(img.pixels[0]==0&&img.pixels[2]==255,"bottom row first");
 require(img.pixels[8]==255&&img.pixels[9]==0,"top row second");
 require(img.pixels[3]==255,"alpha");
}
void invalid(){
 auto load=[](const char* txt){std::istringstream f(txt);return terraflux::assets::read_ppm_p3(f);};
 rejects([&]{(void)load("P6 1 1 255 0 0 0");},"format");
 rejects([&]{(void)load("P3 1 1 255 0 0");},"truncated");
 rejects([&]{(void)load("P3 1 1 255 0 0 300");},"color range");
 rejects([&]{(void)load("P3 0 1 255");},"dimensions");
}
void material_library() {
  std::istringstream input(R"MTL(# Diffuse colors and textures
newmtl Grass
Kd 0.2 0.6 0.3
map_Kd grass.ppm
newmtl Stone
Kd 0.8 0.8 0.8
Ns 80
)MTL");
  const auto m=terraflux::assets::parse_mtl(input);
  require(m.size()==2 && m.at("Grass").albedo_map=="grass.ppm","MTL texture name");
  require(m.at("Grass").base_color.y==0.6 && m.at("Stone").base_color.x==0.8,
          "MTL diffuse colors");
  auto load=[](const char* txt) {
    std::istringstream in(txt); return terraflux::assets::parse_mtl(in);
  };
  rejects([&]{(void)load("Kd 1 1 1\nnewmtl X\n");},"Kd must follow newmtl");
  rejects([&]{(void)load("newmtl X\nnewmtl X\n");},"duplicate material");
  rejects([&]{(void)load("newmtl X\nKd 1.5 0 0\n");},"invalid diffuse color");
  rejects([&]{(void)load("newmtl X\nmap_Kd -s 1 1 1 x.ppm\n");},
          "unimplemented MTL map flags rejected");
}
void checker(){
 auto img=terraflux::assets::checkerboard_image(8);
 require(img.pixels.size()==8*8*4,"checker image size");
 require(img.pixels[0]!=img.pixels[8],"checker tiles differ");
}
}
int main(){try{parse_image();invalid();checker();material_library();std::cout<<"Material tests: 4 passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
