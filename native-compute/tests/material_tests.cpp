#include <terraflux/assets/material.hpp>
#include <terraflux/assets/resource_resolution.hpp>
#include <chrono>
#include <fstream>
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
void resource_resolution(){
 using namespace terraflux::assets;
 const auto root=std::filesystem::temp_directory_path()/
     ("terraflux-materials-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code e;std::filesystem::remove_all(path,e);}} cleanup{root};
 std::filesystem::create_directories(root/"maps");
 std::ofstream(root/"a.mtl")<<"newmtl Stone\nKd 0.2 0.4 0.6\nmap_Kd maps/tile.ppm\n";
 std::ofstream(root/"maps/tile.ppm")<<"P3 1 1 255 10 20 30\n";
 Mesh mesh;mesh.material_libraries={"a.mtl"};mesh.sections.push_back({0,3,"Stone"});
 const auto library=load_obj_materials(mesh,root,"model/",true);
 require(mesh.sections[0].material_name=="model/Stone" &&
         library.at("model/Stone").material.metallic_factor==0,"scoped OBJ material resolution");
 const auto texture=resolve_asset_path(library.at("model/Stone").folder,
                                      library.at("model/Stone").material.albedo_map);
 const auto image=resolve_texture_image(texture,false);
 require(image.width==1 && image.pixels[0]==10 && image.pixels[2]==30,"material-relative PPM decode");
 auto duplicate=mesh;duplicate.sections[0].material_name="Stone";duplicate.material_libraries.push_back("a.mtl");
 rejects([&]{(void)load_obj_materials(duplicate,root,"changed/");},"duplicate library material rejected");
 require(duplicate.sections[0].material_name=="Stone","failed library load preserves section names");
 rejects([&]{(void)resolve_asset_path(root,"../outside.ppm");},"parent escape rejected");
 rejects([&]{(void)resolve_asset_path(root,"missing.ppm");},"missing material resource rejected");
 rejects([&]{(void)resolve_asset_path(root,"maps/tile.ppm",1);},"resource size bound");
 auto sampling=terraflux::TextureSampling{};
 const auto first=texture_cache_key("maps/./tile.ppm",sampling,true);
 require(first==texture_cache_key("maps/tile.ppm",sampling,true),"cache key normalizes paths");
 sampling.wrap_s=terraflux::TextureWrap::ClampToEdge;
 require(first!=texture_cache_key("maps/tile.ppm",sampling,true),"sampler state prevents incompatible sharing");
 require(texture_cache_key({},sampling,true)!=texture_cache_key({},sampling,false),"distinct default image identities");
 require(resolve_texture_image({},true).pixels[0]==255,"PBR white fallback");
 require(resolve_texture_image("__default_normal__",true).pixels[2]==255,"neutral normal fallback");
 const std::map<std::string,ImageRgba8> embedded{{"model/image",{1,1,{1,2,3,255}}}};
 require(resolve_texture_image("model/image",true,embedded).pixels[0]==1,"embedded namespaced texture resolution");
 rejects([&]{(void)resolve_texture_image(root/"missing.png",true);},"missing texture rejected");
}

}
int main(){try{parse_image();invalid();checker();material_library();resource_resolution();std::cout<<"Material tests: 5 passed\n";}
catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
