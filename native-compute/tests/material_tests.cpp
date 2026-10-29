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
void checker(){
 auto img=terraflux::assets::checkerboard_image(8);
 require(img.pixels.size()==8*8*4,"checker image size");
 require(img.pixels[0]!=img.pixels[8],"checker tiles differ");
}
}
int main(){try{parse_image();invalid();checker();std::cout<<"Material tests: 3 passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
