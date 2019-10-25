#ifndef __ANALYSIS_HH__
#define __ANALYSIS_HH__

#include <opencv2/videoio.hpp>
#include <opencv2/core/mat.hpp>
#include <geometry.hh>
#include <vector>
#include <inttypes.h>

struct FrameIterator
{
  cv::VideoCapture capture;
  cv::Mat frame;
  uintptr_t idx, stop;
  
  FrameIterator( std::string _fp, uintptr_t _stop )
    : capture( _fp.c_str() ), frame(), idx(), stop( _stop )
  {
    if (not capture.isOpened()) throw "Error when reading avi file";
  }
  
  ~FrameIterator()
  {}
  
  bool
  next()
  {
    capture >> frame;
    ++idx;
    if (idx >= stop) { /* drain the movie */ while (not frame.empty()) { capture >> frame; ++idx; } }
    return not frame.empty();
  }
};
  
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
  double hasnan() const { return std::isnan(p.x) or std::isnan(p.y) or std::isnan(d.x) or std::isnan(d.y) or std::isnan(mjr) or std::isnan(mnr); }
  
  Point<double> ep0() const { return p + d*mjr; }
  Point<double> ep1() const { return p - d*mjr; }
  Point<double> mj() const { return d / mjr; }
  Point<double> mn() const { return (!d) / mnr; }
  
  double elongation() const { return mjr / mnr; }
};

struct Analyser
{
  struct BGSel
  {
    virtual bool accept( uintptr_t frame ) = 0;
    virtual void repr( std::ostream& ) = 0;
    virtual ~BGSel() {}
  };

  uintptr_t           records;
  std::vector<double> values;
  cv::Mat             ref;
  std::vector<Mice>   mices;
  double              minelongation;
  uintptr_t           crop[4];
  uintptr_t           stop;
  typedef std::vector<std::string> Args;
  Args                args;
  Point<int>          lastclick;
  std::string         croparg;
  BGSel*              bgframes;
  unsigned            fps;
  unsigned            threshold;
  bool                hilite;
  
  Analyser();
  
  void click( int x1, int y1 );
  
  
  void restart();
  
  struct Ouch {};
  
  void pass0( FrameIterator const& fi );
  
  uintptr_t height() const { return ref.empty() ? 0 : ref.rows; }
  uintptr_t width() const { return ref.empty() ? 0 : ref.cols; }
  
  void background();
  
  void pass1( cv::Mat const& img );
  
  void redraw( FrameIterator& _fi );
  
  void trajectory();
  
  void dumpresults( std::ostream& sink );
};

#endif /* __ANALYSIS_HH__ */
