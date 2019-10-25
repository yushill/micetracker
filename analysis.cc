#include <analysis.hh>
#include <iostream>
#include <set>
#include <unistd.h>

Analyser::Analyser()
  : records(), ref(), minelongation( 1.3 )
  , crop(), stop( std::numeric_limits<uintptr_t>::max() )
  , lastclick( -1, -1 ), bgframes(0), fps(1), threshold( 0x40 ), hilite(false)
{
  for (int idx = 0; idx < 4; ++idx) crop[idx] = 0;
}
  
void
Analyser::click( int x1, int y1 )
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
Analyser::restart()
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
  
void
Analyser::pass0( FrameIterator const& fi )
{
  cv::Mat const& img = fi.frame;
  if (img.depth() != CV_8U) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
  uintptr_t height = img.rows, width = img.cols, channels = img.channels(), compcount = width * height * channels;
  if (values.size() == 0)
    {
      values.resize( compcount );
      ref = img.clone();
    }
  if ((values.size() != compcount) or ref.empty()) throw Ouch();

  if (not bgframes->accept( fi.idx ))
    return;
    
  for (uintptr_t y = 0; y < height; ++y)
    {
      uint8_t const* row = img.ptr<uint8_t>(y);
      for (uintptr_t x = 0; x < width; ++x)
        for (uintptr_t c = 0; c < channels; ++c)
          {
            uintptr_t imgidx = x*channels + c;
            uintptr_t bgdidx = (y*width + x)*channels + c;
            values[bgdidx] += (double)(unsigned)(row[imgidx]);
          }
    }
    
  records += 1;
}
  
void
Analyser::background()
{
  uintptr_t height = ref.rows, width = ref.cols, channels = ref.channels();
    
  for (uintptr_t y = 0; y < height; ++y)
    {
      uint8_t* row = ref.ptr<uint8_t>(y);
      for (uintptr_t x = 0; x < width; ++x)
        for (uintptr_t c = 0; c < channels; ++c)
          {
            uintptr_t imgidx = x*channels + c;
            uintptr_t bgdidx = (y*width + x)*channels + c;
            row[imgidx] = uint8_t(int((values[bgdidx] / records) + .5));
          }
    }
    
}
  
void
Analyser::pass1( cv::Mat const& img )
{
  if (img.depth() != CV_8U) throw Ouch();
  if ((ref.rows != img.rows) or (ref.cols != img.cols) or
      (ref.channels() != img.channels()) or (ref.step != img.step)) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
  uintptr_t height = img.rows, width = img.cols, channels = img.channels();
    
  Point<double> center(0.,0.);
  double xxv = 0.0, yyv = 0.0, xyv = 0.0, sum = 0.0;
  for (uintptr_t y = 0, ry = height; y < height; ++y, --ry) {
    if ((y < crop[2]) or (ry <= crop[3])) continue;
    uint8_t const* irow = img.ptr<uint8_t>(y);
    uint8_t* brow = ref.ptr<uint8_t>(y);
    for (uintptr_t x = 0, rx = width; x < width; ++x, --rx) {
      uintptr_t imgidx = x*channels;
      uint8_t const* ipix = &irow[imgidx];
      if ((x < crop[0]) or (rx <= crop[1])) continue;
      uint8_t* bpix = &brow[imgidx];
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
  
void
Analyser::redraw( FrameIterator& _fi )
{
  cv::Mat& img = _fi.frame;
  if (img.depth() != CV_8U) throw Ouch();
  if ((ref.rows != img.rows) or (ref.cols != img.cols) or
      (ref.channels() != img.channels()) or (ref.step != img.step)) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->colorModel[idx] == "RGB"[3]) throw Ouch();
  // for (int idx = 0; idx < 3; ++idx) if (img->channelSeq[idx] == "RGB"[3]) throw Ouch();
    
  uintptr_t height = img.rows, width = img.cols, channels = img.channels();
    
  for (uintptr_t y = 0, ry = height; y < height; ++y, --ry) {
    bool docrop = (y < crop[2]) or (ry <= crop[3]);
    uint8_t* irow = img.ptr<uint8_t>(y);
    uint8_t* brow = ref.ptr<uint8_t>(y);
    for (uintptr_t x = 0, rx = width; x < width; ++x, --rx) {
      uintptr_t imgidx = x*channels;
      uint8_t* ipix = &irow[imgidx];
      if (docrop or (x < crop[0]) or (rx <= crop[1]))
        { ipix[0] ^= 0xff; ipix[1] ^= 0xff; ipix[2] ^= 0xff; continue; }
      uint8_t* bpix = &brow[imgidx];
      uint8_t bgr[3] = {0};
      for (uintptr_t c = 0; c < channels; ++c)
        bgr[c] = abs( (int)ipix[c] - (int)bpix[c] );
      uint8_t l = (0x4c8b43*bgr[2] + 0x9645a2*bgr[1] + 0x1d2f1b*bgr[0] + 0x800000) >> 24;
      if (hilite and (l >= threshold))
        { ipix[0] ^= 0xff; ipix[1] ^= 0x00; ipix[2] ^= 0xff; continue; }
      // if () { /* ipix[0] = 0x00;   ipix[1] = 0x00;   ipix[2] = 0x00; */ continue; }
      // else               { /* ipix[0] = bgr[0]; ipix[1] = bgr[1]; ipix[2] = bgr[2]; */ }
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
    uint8_t* irow = img.ptr<uint8_t>(y);
    for (intptr_t x = xbeg; x < xend; ++x) {
      Point<double> p = Point<double>( x, y ) - mice.p;
      double mjp = p*mice.mj(), mnp = p*((!mice.d) / std::max( 4., mice.mnr ));
      uint8_t green = mjp > 0 ? 0xff : 0;
      if ((mjp*mjp + mnp*mnp) < 1) {
        uint8_t* pix = &irow[x*channels];
        pix[0] = blue; pix[1] = green; pix[2] = red;
      }
    }
  }
    
}
  
void
Analyser::trajectory()
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
      
    for (std::vector<Mice>::iterator head = mices.begin(), end = mices.end(), tail = end; head != end; ++head) {
      if      ((head != end) and head->valid) {
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
Analyser::dumpresults( std::ostream& sink )
{
  sink << "micetracker";
  for (Args::const_iterator itr = args.begin(), end = args.end(); (++itr) != end;) {
    sink << ' ' << *itr;
  }
  sink << '\n';
    
  uintptr_t bounds[4] = {
                         crop[0], ref.cols - crop[1],
                         crop[2], ref.rows - crop[3]
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