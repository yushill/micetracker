#include <cv.h>
#include <highgui.h>
#include <inttypes.h>
#include <unistd.h>

#define PRINT_ONCE( EXPR ) print_once( EXPR, #EXPR )

template <typename T>
void
print_once( T value, std::string name )
{
  typedef std::map<std::string,T> LastValues;
  static LastValues last_values;
  typename LastValues::iterator itr = last_values.lower_bound( name );
  if ((itr != last_values.end()) and (itr->first == name)) {
    if (itr->second == value) return;
    itr->second = value;
  } else {
    itr = last_values.insert( itr, std::pair<std::string,T>( name, value ) );
  }
  std::cout << name << ": " << value << std::endl;
};

struct Pixel
{
  uint8_t b,g,r,a;
  Pixel() {}
  Pixel( uint8_t _b, uint8_t _g, uint8_t _r, uint8_t _a ) : b(_b),g(_g),r(_r),a(_a) {}
  Pixel( Pixel const& _p ) : b(_p.b),g(_p.g),r(_p.r),a(_p.a) {}
  void set( uint8_t _b, uint8_t _g, uint8_t _r, uint8_t _a ) { (*this) = Pixel( _b,_g,_r,_a ); }
};

struct Background
{
  uintptr_t records;
  std::vector<double> values;
  IplImage* ref;
  
  Background() : records(), ref() {}
  struct Ouch {};
  void import( IplImage* img )
  {
    if ((img->depth != 8) or (img->origin != 0) or (img->dataOrder != 0)) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
    uintptr_t height = img->height, width = img->width, channels = img->nChannels,
      compcount = width * height * channels;
    if (values.size() == 0) { values.resize( compcount ); assert( not ref ); ref = cvCloneImage( img ); }
    if ((values.size() != compcount) or (ref == 0)) throw Ouch();
    uintptr_t const step = img->widthStep;
    
    for (uintptr_t y = 0; y < height; ++y)
      for (uintptr_t x = 0; x < width; ++x)
        for (uintptr_t c = 0; c < channels; ++c)
          {
            uintptr_t imgidx = y*step + x*channels + c;
            uintptr_t bgdidx = (y*width + x)*channels + c;
            values[bgdidx] += (double)(uint32_t)(*((uint8_t*)&(img->imageData[imgidx])));
          }
    records += 1;
  }
  
  void finalize()
  {
    uintptr_t height = ref->height, width = ref->width, channels = ref->nChannels;
    uintptr_t const step = ref->widthStep;
    
    for (uintptr_t y = 0; y < height; ++y)
      for (uintptr_t x = 0; x < width; ++x)
        for (uintptr_t c = 0; c < channels; ++c)
          {
            uintptr_t imgidx = y*step + x*channels + c;
            uintptr_t bgdidx = (y*width + x)*channels + c;
            ref->imageData[imgidx] = uint8_t(int((values[bgdidx] / records) + .5));
          }
    
  }
  
  void reveal( IplImage* img, uint8_t threshold, uintptr_t crop[4] )
  {
    if ((img->depth != 8) or (img->origin != 0) or (img->dataOrder != 0)) throw Ouch();
    if ((ref->height != img->height) or (ref->width != img->width) or
        (ref->nChannels != img->nChannels) or (ref->widthStep != img->widthStep)) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
    uintptr_t height = img->height, width = img->width, channels = img->nChannels;
    
    uintptr_t const step = img->widthStep;
    double xm = 0.0, ym = 0.0, xxv = 0.0, yyv = 0.0, xyv = 0.0, sum = 0.0;
    for (uintptr_t y = 0, ry = height; y < height; ++y, --ry) {
      bool docrop = (y < crop[2]) or (ry <= crop[3]);
      for (uintptr_t x = 0, rx = width; x < width; ++x, --rx) {
        uintptr_t imgidx = y*step + x*channels;
        uint8_t* ipix = (uint8_t*)&(img->imageData[imgidx]);
        if (docrop or (x < crop[0]) or (rx <= crop[1]))
          { ipix[0] ^= 0xff; ipix[1] ^= 0xff; ipix[2] ^= 0xff; continue; }
        uint8_t* bpix = (uint8_t*)&(ref->imageData[imgidx]);
        uint8_t bgr[3] = {0};
        for (uintptr_t c = 0; c < channels; ++c)
          bgr[c] = abs( (int)ipix[c] - (int)bpix[c] );
        uint8_t l = (0x4c8b43*bgr[2] + 0x9645a2*bgr[1] + 0x1d2f1b*bgr[0] + 0x800000) >> 24;
        if (l < threshold) { // ipix[0] = 0x00;   ipix[1] = 0x00;   ipix[2] = 0x00; 
          continue; }
        else               { // ipix[0] = bgr[0]; ipix[1] = bgr[1]; ipix[2] = bgr[2];
        }
        //ipix[0] = 0xff; ipix[1] = 0xff; ipix[2] = 0xff;
        xm += x*l; ym += y*l; sum += l;
        xxv += x*x*l; yyv += y*y*l; xyv += x*y*l;
      }
    }
    xm /= sum; ym /= sum;
    xxv /= sum; yyv /= sum; xyv /= sum;
    xxv -= xm*xm; yyv -= ym*ym; xyv -= xm*ym;
    
    double dif = (yyv-xxv)/2;
    double t0 = sqrt( xyv*xyv + dif*dif );
    double xd = xyv + t0 - dif;
    double yd = xyv + t0 + dif;
    { double norm = 1/sqrt( xd*xd + yd*yd ); xd *= norm; yd *= norm; }
    double major = sqrt( (xxv+yyv)/2 + t0 ), minor = sqrt( (xxv+yyv)/2 - t0 );
    double mjx = xd / major, mjy = yd / major, mnx = yd / minor, mny = -xd / minor;
    
    uintptr_t radius = major + 2,
      ybeg = std::max<uintptr_t>( ym - radius, 0 ), yend = std::min<uintptr_t>( ym + radius, height ),
      xbeg = std::max<uintptr_t>( xm - radius, 0 ), xend = std::min<uintptr_t>( xm + radius, width );
    for (uintptr_t y = ybeg; y < yend; ++y) {
      for (uintptr_t x = xbeg; x < xend; ++x) {
        double yp = y-ym, xp = x-xm, mjp = yp*mjy + xp*mjx, mnp = yp*mny + xp*mnx;
        uintptr_t imgidx = y*step + x*channels;
        if ((mjp*mjp + mnp*mnp) < 1) { uint8_t* pix = (uint8_t*)&(img->imageData[imgidx]); pix[0] = pix[1] = 0; pix[2] = 0xff; }
      }
    }
    
  }
};


template <typename T>
T thediff( T& , T const& b )
{
  
}

template <uintptr_t SZ>
char*
argsof( char const (&prefix)[SZ], char* arg )
{
  return strncmp( &prefix[0], arg, SZ-1 ) ? 0 : &arg[SZ-1];
}


int
main (int argc, char** argv)
{
  uint8_t threshold = 0x40;
  uintptr_t crop[] = {0,0,0,0};
  std::string filepath;
  for (int aidx = 1; aidx < argc; aidx += 1)
    {
      char* params;
      if ((params = argsof( "crop:", argv[aidx] ))) {
        char const* serr = "syntax error: crop:<left>:<right>:<top>:<bottom>\n";
        char sep = ':';
        for (int idx = 0; idx < 4; ++idx) {
          if (sep != ':') { std::cerr << serr; return 1; }
          crop[idx] = strtoul( params, &params, 10 );
          sep = *params++;
        }
        if (sep != '\0') { std::cerr << serr; return 1; }
      }
      
      else if ((params = argsof( "threshold:", argv[aidx] ))) {
        threshold = strtoul( params, &params, 0 );
        std::cerr << "Using threshold: " << unsigned( threshold ) << "\n";
      }
      
      else {
        if (filepath.size()) { std::cerr << "one video at a time please...\n"; return 1; }
        filepath = argv[aidx];
      }
    }
  
  if (filepath.size() == 0) { std::cerr << "no video given...\n"; return 1; }
  cvNamedWindow( "w", 1 );
  
  CvCapture* capture=0;
  IplImage* frame=0;
  
  capture = cvCaptureFromAVI( filepath.c_str() ); // read AVI video
  if (not capture) throw "Error when reading steam_avi";
  
  Background bg;
  
  for (;;)
    {
      frame = cvQueryFrame( capture );
      if (not frame) break;
      
      bg.import( frame );
    }
  cvReleaseCapture( &capture ); 
  
  bg.finalize();
  
  capture = cvCaptureFromAVI( filepath.c_str() ); // read AVI video
  if (not capture) throw "Error when reading steam_avi";

  for (;;)
    {
      frame = cvQueryFrame( capture );
      if (not frame) break;
      
      bg.reveal( frame, threshold, crop );
      
      cvShowImage( "w", frame );
      cvWaitKey(0); // key press to step
      //sleep(1);
    }
  cvReleaseCapture( &capture ); 

  cvWaitKey(0); // key press to close window
  cvDestroyWindow( "w" );
  cvReleaseImage( &frame );
}



