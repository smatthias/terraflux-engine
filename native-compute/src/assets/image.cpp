#include <terraflux/assets/material.hpp>

#include <png.h>
#include <jpeglib.h>

#include <algorithm>
#include <array>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace terraflux::assets {
namespace {
constexpr std::size_t kMaxEncoded=32'000'000;
void dimensions(std::uint32_t w,std::uint32_t h){
  if(!w || !h || w>4096 || h>4096 || static_cast<std::uint64_t>(w)*h>4'000'000)
    throw std::invalid_argument("decoded image dimensions exceed safety limits");
}
ImageRgba8 flipped(std::uint32_t w,std::uint32_t h,
                   const std::uint8_t* top_first,std::size_t stride){
  dimensions(w,h);
  ImageRgba8 result{w,h,{}};
  const auto row=static_cast<std::size_t>(w)*4;
  result.pixels.resize(row*h);
  for(std::size_t y=0;y<h;++y)
    std::copy_n(top_first+y*stride,row,result.pixels.data()+(h-1-y)*row);
  return result;
}
ImageRgba8 decode_png(std::span<const std::uint8_t> encoded){
  png_image image{};
  image.version=PNG_IMAGE_VERSION;
  if(!png_image_begin_read_from_memory(&image,encoded.data(),encoded.size()))
    throw std::invalid_argument("invalid PNG image data");
  try{
    dimensions(image.width,image.height);
    image.format=PNG_FORMAT_RGBA;
    std::vector<std::uint8_t> pixels(PNG_IMAGE_SIZE(image));
    if(!png_image_finish_read(&image,nullptr,pixels.data(),0,nullptr))
      throw std::invalid_argument("PNG pixels could not be decoded");
    auto result=flipped(image.width,image.height,pixels.data(),
                        static_cast<std::size_t>(image.width)*4);
    png_image_free(&image);
    return result;
  }catch(...){
    png_image_free(&image);
    throw;
  }
}
struct JpegError {
  jpeg_error_mgr api;
  jmp_buf jump;
  char message[JMSG_LENGTH_MAX]{};
  unsigned char* pixels=nullptr;
};
extern "C" void jpeg_failure(j_common_ptr common){
  auto* error=reinterpret_cast<JpegError*>(common->err);
  (*common->err->format_message)(common,error->message);
  longjmp(error->jump,1);
}
ImageRgba8 decode_jpeg(std::span<const std::uint8_t> encoded){
  if(encoded.size()>std::numeric_limits<unsigned long>::max())
    throw std::invalid_argument("JPEG source too large");
  jpeg_decompress_struct decoder{};
  JpegError error{};
  decoder.err=jpeg_std_error(&error.api);
  error.api.error_exit=jpeg_failure;
  jpeg_create_decompress(&decoder);
  // From here until destroy, use only C resources; libjpeg can longjmp.
  if(setjmp(error.jump)){
    jpeg_destroy_decompress(&decoder);
    std::free(error.pixels);
    throw std::invalid_argument(std::string("JPEG decode failed: ")+error.message);
  }
  jpeg_mem_src(&decoder,encoded.data(),static_cast<unsigned long>(encoded.size()));
  if(jpeg_read_header(&decoder,TRUE)!=JPEG_HEADER_OK)
    jpeg_failure(reinterpret_cast<j_common_ptr>(&decoder));
  dimensions(decoder.image_width,decoder.image_height);
  decoder.out_color_space=JCS_RGB;
  jpeg_start_decompress(&decoder);
  if(decoder.output_components!=3 ||
     decoder.output_width>4096 || decoder.output_height>4096)
    jpeg_failure(reinterpret_cast<j_common_ptr>(&decoder));
  const auto width=static_cast<std::size_t>(decoder.output_width);
  const auto height=static_cast<std::size_t>(decoder.output_height);
  const auto stride=width*4;
  error.pixels=static_cast<unsigned char*>(std::malloc(stride*height));
  if(!error.pixels) {
    jpeg_destroy_decompress(&decoder);
    throw std::bad_alloc();
  }
  // JPEG delivers top-first RGB. Convert each scanline directly to RGBA8.
  // Reuse a small malloc-backed scanline; no C++ stack destructors cross setjmp.
  unsigned char* scanline=static_cast<unsigned char*>(std::malloc(width*3));
  if(!scanline) {
    jpeg_destroy_decompress(&decoder);
    std::free(error.pixels);
    throw std::bad_alloc();
  }
  while(decoder.output_scanline<decoder.output_height){
    JSAMPROW rows[1]={scanline};
    if(jpeg_read_scanlines(&decoder,rows,1)!=1) {
      std::free(scanline);
      jpeg_failure(reinterpret_cast<j_common_ptr>(&decoder));
    }
    const auto y=static_cast<std::size_t>(decoder.output_scanline-1);
    for(std::size_t x=0;x<width;++x) {
      for(std::size_t k=0;k<3;++k)
        error.pixels[y*stride+x*4+k]=scanline[x*3+k];
      error.pixels[y*stride+x*4+3]=255;
    }
  }
  std::free(scanline);
  jpeg_finish_decompress(&decoder);
  jpeg_destroy_decompress(&decoder);
  auto result=flipped(static_cast<std::uint32_t>(width),
                      static_cast<std::uint32_t>(height),error.pixels,stride);
  std::free(error.pixels);
  return result;
}
} // namespace

ImageRgba8 decode_image(std::span<const std::uint8_t> encoded,
                        const std::string& mime_type) {
  if(encoded.empty() || encoded.size()>kMaxEncoded)
    throw std::invalid_argument("image byte length exceeds limits");
  const bool png=encoded.size()>=8 &&
      std::memcmp(encoded.data(),"\x89PNG\r\n\x1a\n",8)==0;
  const bool jpeg=encoded.size()>=3 &&
      encoded[0]==0xff && encoded[1]==0xd8 && encoded[2]==0xff;
  if(mime_type!="" && mime_type!="image/png" && mime_type!="image/jpeg")
    throw std::invalid_argument("unsupported glTF image MIME type");
  if(!png && !jpeg)throw std::invalid_argument("unsupported PNG/JPEG image magic");
  if((mime_type=="image/png" && !png) || (mime_type=="image/jpeg" && !jpeg))
    throw std::invalid_argument("image MIME type and file signature differ");
  return png?decode_png(encoded):decode_jpeg(encoded);
}
} // namespace terraflux::assets
