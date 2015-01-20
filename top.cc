#include <geometry.hh>
#include <set>
#include <string>
#include <vector>
#include <limits>
#include <fstream>
#include <cv.h>
#include <highgui.h>
#include <inttypes.h>
#include <unistd.h>

struct Mice
{
  Point<double> p;
  Point<double> d;
  Point<double> s;
  double mjr, mnr;
  bool valid;
  
  Mice( Point<double> _p, Point<double> _d, double _mjr, double _mnr )
    : p(_p), d(_d), mjr(_mjr), mnr(_mnr), valid(not hasnan())
  {}
  
  double distance() const { return sqrt( (ep1() - ep0()).sqnorm() ); }
  
  void invalidate() { p.x = p.y = d.x = d.y = mjr = mnr = nan(""); valid = false; }
  double hasnan() const { return isnan(p.x) or isnan(p.y) or isnan(d.x) or isnan(d.y) or isnan(mjr) or isnan(mnr); }
  
  Point<double> ep0() const { return p + d*mjr; }
  Point<double> ep1() const { return p - d*mjr; }
  Point<double> mj() const { return d / mjr; }
  Point<double> mn() const { return (!d) / mnr; }
  
  double elongation() const { return mjr / mnr; }
};

struct FrameIterator
{
  CvCapture* capture;
  IplImage* frame;
  uintptr_t idx, stop;
  
  FrameIterator( std::string _fp, uintptr_t _stop )
    : capture( cvCaptureFromAVI( _fp.c_str() ) ), frame(), idx(), stop( _stop )
  {
    if (not capture) throw "Error when reading avi file";
  }
  
  ~FrameIterator()
  {
    cvReleaseCapture( &capture ); 
    cvReleaseImage( &frame );
  }
  
  bool
  next()
  {
    frame = cvQueryFrame( capture );
    ++idx;
    if (idx >= stop) { while (frame) { frame = cvQueryFrame( capture ); ++idx; } }
    return frame;
  }
};
  
template <uintptr_t SZ>
char*
argsof( char const (&prefix)[SZ], char* arg )
{
  return strncmp( &prefix[0], arg, SZ-1 ) ? 0 : &arg[SZ-1];
}

struct Analyser
{
  uintptr_t           records;
  std::vector<double> values;
  IplImage*           ref;
  std::vector<Mice>   mices;
  uint8_t             threshold;
  double              minelongation;
  uintptr_t           crop[4];
  uintptr_t           stop;
  typedef std::vector<std::string> Args;
  Args                args;
  Point<int>          lastclick;
  std::string         croparg;
  
  Analyser()
    : records(), ref(), threshold( 0x40 ), minelongation( 1.3 )
    , crop( ), stop( std::numeric_limits<uintptr_t>::max() )
    , lastclick( -1, -1 )
  { for (int idx = 0; idx < 4; ++idx) crop[idx] = 0; }
  
  void
  click( int x1, int y1 )
  {
    int x0 = this->lastclick.x, y0 = this->lastclick.y;
    this->lastclick.x = x1; this->lastclick.y = y1;
    if ((x0 < 0) or (y0 < 0)) return;
    
    if (x0 > x1) std::swap(x0,x1);
    if (y0 > y1) std::swap(y0,y1);
    
    {
      std::ostringstream oss;
      oss << "crop:" << x0 << ':' << (width() - x1) << ':' << y0 << ':' << (height() - y1);
      croparg = oss.str(); 
    }
    
    std::cerr << "==> " << croparg << std::endl;
  }
  
  void
  restart()
  {
    std::vector<char*> newargs;
    for (Args::iterator itr = args.begin(), end = args.end(); itr != end; ++itr) {
      newargs.push_back( &((*itr)[0]) );
    }

    if (croparg.size())
      newargs.push_back( &croparg[0] );
    newargs.push_back( 0 );
    
    execvp( newargs[0], &newargs[0] );
    throw 0; // should not be here
  }
  
  struct Ouch {};
  void pass0( IplImage const* img )
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
  
  uintptr_t height() const { return ref ? ref->height : 0; }
  uintptr_t width() const { return ref ? ref->width : 0; }
  
  void background()
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
  
  void pass1( IplImage const* img )
  {
    if ((img->depth != 8) or (img->origin != 0) or (img->dataOrder != 0)) throw Ouch();
    if ((ref->height != img->height) or (ref->width != img->width) or
        (ref->nChannels != img->nChannels) or (ref->widthStep != img->widthStep)) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
    uintptr_t height = img->height, width = img->width, channels = img->nChannels;
    
    uintptr_t const step = img->widthStep;
    Point<double> center(0.,0.);
    double xxv = 0.0, yyv = 0.0, xyv = 0.0, sum = 0.0;
    for (uintptr_t y = 0, ry = height; y < height; ++y, --ry) {
      if ((y < crop[2]) or (ry <= crop[3])) continue;
      for (uintptr_t x = 0, rx = width; x < width; ++x, --rx) {
        uintptr_t imgidx = y*step + x*channels;
        uint8_t* ipix = (uint8_t*)&(img->imageData[imgidx]);
        if ((x < crop[0]) or (rx <= crop[1])) continue;
        uint8_t* bpix = (uint8_t*)&(ref->imageData[imgidx]);
        uint8_t bgr[3] = {0};
        for (uintptr_t c = 0; c < channels; ++c)
          bgr[c] = abs( (int)ipix[c] - (int)bpix[c] );
        uint8_t l = (0x4c8b43*bgr[2] + 0x9645a2*bgr[1] + 0x1d2f1b*bgr[0] + 0x800000) >> 24;
        if (l < threshold) continue;
        center += Point<double>( x, y )*l;
        sum += l;
        xxv += x*x*l; yyv += y*y*l; xyv += x*y*l;
      }
    }
    center.x /= sum; center.y /= sum;
    xxv /= sum; yyv /= sum; xyv /= sum;
    xxv -= center.x*center.x; yyv -= center.y*center.y; xyv -= center.x*center.y;
    
    double dif = (yyv-xxv)/2;
    double t0 = sqrt( xyv*xyv + dif*dif );
    Point<double> direction( xyv + t0 - dif, xyv + t0 + dif );
    { double norm = 1/sqrt( direction.sqnorm() ); direction *= norm; }
    double mjr = sqrt( (xxv+yyv)/2 + t0 ), mnr = sqrt( (xxv+yyv)/2 - t0 );
    mices.push_back( Mice( center, direction, mjr, mnr ) );
  }
  
  void redraw( FrameIterator const& _fi )
  {
    IplImage const* img = _fi.frame;
    if ((img->depth != 8) or (img->origin != 0) or (img->dataOrder != 0)) throw Ouch();
    if ((ref->height != img->height) or (ref->width != img->width) or
        (ref->nChannels != img->nChannels) or (ref->widthStep != img->widthStep)) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
    // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
    uintptr_t height = img->height, width = img->width, channels = img->nChannels;
    
    uintptr_t const step = img->widthStep;
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
        if (l < threshold) { /* ipix[0] = 0x00;   ipix[1] = 0x00;   ipix[2] = 0x00; */ continue; }
        else               { /* ipix[0] = bgr[0]; ipix[1] = bgr[1]; ipix[2] = bgr[2]; */ }
        //ipix[0] = 0xff; ipix[1] = 0xff; ipix[2] = 0xff;
      }
    }
    Mice const& mice = mices[_fi.idx];
    
    uint8_t red = 0, blue = 0;
    if (mice.valid)  red = 0xff;
    else             blue = 0xff;
    
    intptr_t radius = mice.mjr + 2,
      ybeg = std::max<intptr_t>( mice.p.y - radius, 0 ), yend = std::min<intptr_t>( mice.p.y + radius, height ),
      xbeg = std::max<intptr_t>( mice.p.x - radius, 0 ), xend = std::min<intptr_t>( mice.p.x + radius, width );
    for (intptr_t y = ybeg; y < yend; ++y) {
      for (intptr_t x = xbeg; x < xend; ++x) {
        Point<double> p = Point<double>( x, y ) - mice.p;
        double mjp = p*mice.mj(), mnp = p*((!mice.d) / std::max( 4., mice.mnr ));
        uint8_t green = mjp > 0 ? 0xff : 0;
        uintptr_t imgidx = y*step + x*channels;
        if ((mjp*mjp + mnp*mnp) < 1) {
          uint8_t* pix = (uint8_t*)&(img->imageData[imgidx]); pix[0] = blue; pix[1] = green; pix[2] = red;
        }
      }
    }
    
  }
  
  void
  trajectory()
  {
    uintptr_t const micecount = mices.size();
    // Computing median
    double median = 0;
    {
      typedef std::set<double> distances_t;
      distances_t distances;
      for (std::vector<Mice>::iterator itr = mices.begin(), end = mices.end(); itr != end; ++itr) {
        if (itr->hasnan() or (itr->elongation() <= minelongation)) { itr->invalidate(); continue; }
        distances.insert( itr->distance() );
      }
      std::set<double>::const_iterator mid = distances.begin();
      for (intptr_t idx = distances.size()/2; --idx>=0;) ++mid;
      median = *mid;
    }
    std::cerr << "median: " << median << std::endl;

    // Invalidating suspicious data
    for (std::vector<Mice>::iterator itr = mices.begin(), end = mices.end(); itr != end; ++itr) {
      if (itr->hasnan()) continue;
      double d = itr->distance();
      if ((d >= median*2) or (d <= median/2)) itr->invalidate();
    }
    
    
    { // Extrapolating missing positions.
      uintptr_t idx = 0;
      while ((idx < micecount) and (mices[idx].hasnan()))
        idx += 1;
      if (idx >= micecount) throw 0;
      if (idx > 0) { mices[0] = mices[idx]; }
      idx = micecount - 1;
      while ((idx < micecount) and (mices[idx].hasnan()))
        idx -= 1;
      if (idx >= micecount) throw 0;
      if (idx < (micecount-1)) { mices[micecount-1] = mices[idx]; }
      
      for (std::vector<Mice>::iterator tail = mices.begin(), head = tail + 1, end = mices.end(); head != end; tail = head, ++head) {
        if (not head->hasnan()) continue;
        while (head->hasnan()) { if (++head == end) throw 0; }
        uintptr_t dist = head-tail;
        for (std::vector<Mice>::iterator hole = tail+1; hole != head; ++hole) {
          uintptr_t idx = hole-tail;
          double a = double(idx)/double(dist), b = 1-a;
          hole->p = head->p*a + tail->p*b;
          hole->d = Point<double>( 1, 0 );
          hole->mjr = hole->mnr = median/2;
        }
      }
    }
    // flip correction
    { // lining up locally
      Point<double> lastp = mices.front().p;
      
      for (std::vector<Mice>::iterator prev = mices.begin(), next = prev + 1, end = mices.end(); next != end; ++prev, ++next) {
        if (next->hasnan()) continue;
        if ((prev->hasnan() and ((next->d * (next->p - lastp)) < 0)) or ((not prev->hasnan()) and ((next->d * prev->d) < 0))) {
          next->d = -next->d;
        }
        lastp = next->p;
      }
    }
    
    { // computing speeds
      mices[0].s = mices[1].p - mices[0].p;
      for (uintptr_t idx = 1; idx < (micecount-1); ++idx) {
        mices[idx].s = (mices[idx+1].p - mices[idx-1].p) / 2;
      }
      mices[micecount-1].s = mices[micecount-1].p - mices[micecount-2].p;
    }
    
    { // lining up by segment
      double score = 0;
      
      for (std::vector<Mice>::iterator head = mices.begin(), end = mices.end(), tail = end; head <= end; ++head) {
        if      ((head < end) and head->valid) {
          if (tail == end) tail = head;
          score += head->s * head->d;
        }
        else if (tail != end) {
          if (score < 0) { do { tail->d = -tail->d; } while (++tail != head); }
          score = 0;
          tail = end;
        }
      }
    }
    
    { // Extrapolating missing directions.
      for (std::vector<Mice>::iterator tail = mices.begin(), head = tail + 1, end = mices.end(); head != end; tail = head, ++head) {
        if (head->valid) continue;
        while (not head->valid) { if (++head == end) throw 0; }
        uintptr_t dist = head-tail;
        for (std::vector<Mice>::iterator hole = tail+1; hole != head; ++hole) {
          uintptr_t idx = hole-tail;
          double a = double(idx)/double(dist), b = 1-a;
          hole->d = head->d*a + tail->d*b;
          hole->d /= sqrt(hole->d.sqnorm());
          hole->mjr = head->mjr*a + tail->mjr*b;
          hole->mnr = head->mnr*a + tail->mnr*b;
        }
      }
    }
    
  }
  
  void
  dumpresults( std::ostream& sink )
  {
    sink << "micetracker";
    for (Args::const_iterator itr = args.begin(), end = args.end(); (++itr) != end;) {
      sink << ' ' << *itr;
    }
    sink << '\n';
    
    uintptr_t bounds[4] = {
      crop[0], ref->width - crop[1],
      crop[2], ref->height - crop[3]
    };
    
    sink << "bounds_lrtb," << bounds[0] << ',' << bounds[1] << ",-" << bounds[2] << ",-" << bounds[3] << ','
         << "elongation," << minelongation << ','
         << "threshold," << (unsigned)threshold << '\n';
    
    sink << "elongation,Xmid,Ymid,Xhead,Yhead,Xtail,Ytail\n";
    for (std::vector<Mice>::const_iterator itr = this->mices.begin(), end = this->mices.end(); itr != end; ++itr)
      {
        sink << itr->elongation() << ',' << itr->p.x << ',' << -itr->p.y << ','
             << itr->ep0().x << ',' << -itr->ep0().y << ',' << itr->ep1().x << ',' << -itr->ep1().y << ',' << itr->mjr << ',' << itr->mnr << '\n';
      }
  }
};

std::string
stringf( char const* _fmt, ... )
{
  std::string str;
  va_list ap;

  for (intptr_t capacity = 128, size; true; capacity = (size > -1) ? size + 1 : capacity * 2) {
    /* allocation */
    char storage[capacity];
    /* Try to print in the allocated space. */
    va_start( ap, _fmt );
    size = vsnprintf( storage, capacity, _fmt, ap );
    va_end( ap );
    /* If it worked, return */
    if (size >= 0 and size < capacity) {
      str.append( storage, size );
      break;
    }
  }
  return str;
}

void
mouse_callback( int event, int x, int y, int flags, Analyser* analyser )
{
  if (event == cv::EVENT_LBUTTONDOWN)
    analyser->click( x, y );
}


int
main( int argc, char** argv )
{
  std::string filepath;
  Analyser analyser;
  analyser.args.push_back( argv[0] );
  
  for (int aidx = 1; aidx < argc; aidx += 1)
    {
      char* param;
      analyser.args.push_back( argv[aidx] );
      if ((param = argsof( "crop:", argv[aidx] ))) {
        analyser.args.pop_back();
        char const* serr = "syntax error: crop:<left>:<right>:<top>:<bottom>\n";
        char sep = ':';
        for (int idx = 0; idx < 4; ++idx) {
          if (sep != ':') { std::cerr << serr; return 1; }
          analyser.crop[idx] = strtoul( param, &param, 10 );
          sep = *param++;
        }
        if (sep != '\0') { std::cerr << serr; return 1; }
        analyser.croparg = argv[aidx];
      }
      
      else if ((param = argsof( "threshold:", argv[aidx] ))) {
        analyser.threshold = strtoul( param, &param, 0 );
        std::cerr << "Using threshold: " << unsigned( analyser.threshold ) << "\n";
      }
      
      else if ((param = argsof( "stop:", argv[aidx] ))) {
        analyser.stop = strtoul( param, &param, 0 );
        std::cerr << "Maximum frames: " << unsigned( analyser.stop ) << "\n";
      }
      
      else if ((param = argsof( "elongation:", argv[aidx] ))) {
        analyser.minelongation = strtod( param, &param );
        std::cerr << "Using minimum elongation: " << analyser.minelongation << "\n";
      }
      
      else {
        if (filepath.size()) { std::cerr << "one video at a time please...\n"; return 1; }
        filepath = argv[aidx];
      }
    }

  if (filepath.size() == 0) { std::cerr << "no video given...\n"; return 1; }
  cvNamedWindow( "w", 1 );
  
  std::string prefix( filepath );
  
  {
    uintptr_t idx = prefix.rfind('.');
    if (idx < prefix.size())
      prefix = prefix.substr(0,idx);
  }
  
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    analyser.pass0( itr.frame );
  
  analyser.background();
  
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    analyser.pass1( itr.frame );
  
  analyser.trajectory();
  
  cv::setMouseCallback( "w", (cv::MouseCallback)mouse_callback, &analyser );
  int kwait = 0;
  CvVideoWriter* writer = 0;
  
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    {
      analyser.redraw( itr );
      cvShowImage( "w", itr.frame );
      char k = cvWaitKey(kwait);
      if (k == '\n')
        kwait = 1;
      else if (k == 'r') {
        writer = cvCreateVideoWriter( (prefix + "_rec.avi").c_str(), CV_FOURCC('M','J','P','G'), 25, cvSize( analyser.width(), analyser.height() ) );
        kwait = 1;
      }
      else if (k == '\b')
        analyser.restart();
      else if (kwait == 0) {
        std::cerr << "KeyCode: " << int(k) << "\n";
      }
      
      if (writer)
        cvWriteFrame( writer, itr.frame );
    }
  
  if (writer)
    cvReleaseVideoWriter( &writer );
  
  cvDestroyWindow( "w" );
  
  std::ofstream sink( (prefix + ".csv").c_str() );
  analyser.dumpresults( sink );
  
  return 0;
}
