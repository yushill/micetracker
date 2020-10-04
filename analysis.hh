#ifndef __ANALYSIS_HH__
#define __ANALYSIS_HH__

#include <opencv2/core/mat.hpp>
#include <geometry.hh>
#include <vector>
#include <inttypes.h>

struct FrameIterator
{
  FrameIterator() : frame(), idx() {}
  virtual ~FrameIterator() {}
  
  virtual bool next() = 0;

  cv::Mat frame;
  uintptr_t idx;
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
    virtual ~BGSel() {}
  };

  struct Pass0
  {
    Pass0() : values(), records(0) {}
    bool step(uintptr_t);
    double avg(uintptr_t idx) { return (values[idx] / records) + .5; }
    void add(uintptr_t idx, double val) { values[idx] += val; }
    std::vector<double> values;
    uintptr_t           records;
  };

  cv::Mat             bg;
  std::vector<Mice>   mices;
  double              minelongation;
  uintptr_t           crop[4];
  typedef std::vector<std::string> Args;
  Args                args;
  Point<int>          lastclick;
  std::string         croparg;
  BGSel*              bgframes;
  unsigned            fps;
  unsigned            threshold;
  bool                hilite;
  bool                soundsize;
  
  Analyser();
  
  void click( int x1, int y1 );
  
  
  void restart();
  
  struct Ouch {};
  
  void step( FrameIterator const& fi, Pass0& );
  void finish( Pass0& );
  
  uintptr_t height() const { return bg.empty() ? 0 : bg.rows; }
  uintptr_t width() const { return bg.empty() ? 0 : bg.cols; }
  
  
  void pass1( cv::Mat const& img );
  
  void redraw( FrameIterator& _fi );
  
  void trajectory();
  
  void dumpresults( std::ostream& sink );
};

#endif /* __ANALYSIS_HH__ */
