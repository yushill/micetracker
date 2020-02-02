#include <analysis.hh>
#include <geometry.hh>
#include <fstream>
#include <iostream>
#include <string>
#include <map>
#include <limits>
#include <cmath>
#include <cstdarg>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/opencv.hpp>
#include <inttypes.h>

template <uintptr_t SZ>
char const*
argsof( char const (&prefix)[SZ], char const* arg )
{
  return strncmp( &prefix[0], arg, SZ-1 ) ? 0 : &arg[SZ-1];
}

void
mouse_callback( int event, int x, int y, int flags, Analyser* analyser )
{
  if (event == cv::EVENT_LBUTTONDOWN)
    analyser->click( x, y );
}

struct VideoFrameIterator : public FrameIterator
{
  VideoFrameIterator( std::string path, uintptr_t _stop ) 
    : FrameIterator()
    , capture( path.c_str() )
    , stop( _stop )
    , fps(capture.get(CV_CAP_PROP_FPS))
  {
    if (not capture.isOpened()) throw "Error when reading avi file";
  }

  double sec() const { return double(idx) / fps; }

  void progress( std::ostream& term ) const
  {
    uintptr_t ufps = fps;
    if (idx % ufps)
      return;
    term << "\e[G\e[KDone: " << (idx / ufps) << "s ";
    term.flush();
  }

  virtual bool next() override
  {
     capture >> frame;
     if (++idx >= stop) { /* drain video */ while (not frame.empty()) { capture >> frame; } }
     return not frame.empty();
  }

  cv::VideoCapture capture;
  uintptr_t stop;
  double fps;
};

struct RangeBGSel : public Analyser::BGSel
{
  RangeBGSel( uintptr_t _lower, uintptr_t _upper ) : lower(_lower), upper(_upper) {}
  virtual bool accept( uintptr_t frame ) override { return (frame >= lower and frame < upper); }
  
  uintptr_t lower;
  uintptr_t upper;
};

struct RatioBGSel : public Analyser::BGSel
{
  RatioBGSel( uintptr_t _first, uintptr_t _second ) : first(_first), period(_first+_second) {}
  virtual bool accept( uintptr_t frame ) override { return (frame % period) < first; }
  
  uintptr_t first;
  uintptr_t period;
};

struct Operands
{
  std::string video;
  uintptr_t framestop;
  double keylogspeed;

  Operands()
    : video()
    , framestop(std::numeric_limits<uintptr_t>::max())
    , keylogspeed(0.0)
  {}
};

struct Params
{
  struct Param
  {
    Param(char const* _name, char const* _args, char const* _desc)
      : name(_name), args_help(_args), desc_help(_desc), args_start(), args()
    {}
    char const* name;
    char const* args_help;
    char const* desc_help;
    char const* args_start;
    char const* args;
    void set_args(char const*_args) { args = args_start = _args; }

    Param& operator >> ( unsigned& value ) { value = strtoul(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( unsigned long& value ) { value = strtoul(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( unsigned long long& value ) { value = strtoull(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( int& value ) { value = strtol(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( long& value ) { value = strtol(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( long long& value ) { value = strtoll(args,const_cast<char**>(&args),0); return *this; }
    Param& operator >> ( char& ch ) { ch = *args++; return *this; }
    Param& operator >> ( bool& ch )
    {
      switch (*args)
        {
        case '0': ch = false; ++args; break;
        case '1': ch = true; ++args; break;
        case 'y': case 'Y':
          ch = true;
          args += (tolower(args[1]) == 'e' and tolower(args[2]) == 's' ? 3 : 1);
          break;
        case 't': case 'T':
          ch = true;
          args += (tolower(args[1]) == 'r' and tolower(args[2]) == 'u' and tolower(args[3]) == 'e' ? 4 : 1);
          break;
        case 'n': case 'N':
          ch = false;
          args += (tolower(args[1]) == 'o' ? 2 : 1);
          break;
        case 'f': case 'F':
          ch = false;
          args += (tolower(args[1]) == 'a' and tolower(args[2]) == 'l' and tolower(args[3]) == 's' and tolower(args[4]) == 's' ? 5 : 1);
          break;
        default: throw *this;
        }
      return *this;
    }
    Param& operator >> ( double& value ) { value = strtod(args,const_cast<char**>(&args)); return *this; }
    std::ostream& usage(std::ostream& sink, uintptr_t spacing) const
    {
      std::ostringstream buf;
      buf << name << ':' << args_help << ' ';
      std::string && head = buf.str();
      head.insert(head.end(), (spacing > head.size() ? spacing - head.size() : 0), ' ');
      sink << head << desc_help << std::endl;
      return sink;
    }
  };
  
  struct Ouch {};
  
  bool all()
  {
    for (Param _("crop", "<left>:<right>:<top>:<bottom>", "Narrows studied region by given margins."); match(_);)
      {
        char sep = ':';
        for (int idx = 0; idx < 4; ++idx) {
          if (sep != ':') throw _;
          _ >> ancfg().crop[idx] >> sep;
        }
        if (sep != '\0') throw _;
        return true;
      }

    for (Param _("hilite", "[y/n]", "Hilite mice location."); match(_);)
      {
        _ >> ancfg().hilite;
        return true;
      }

    for (Param _("bgframes", "<arg1>[[-/]<arg2>]", "Frames considered for background computation; either: a count from start, a range ('-') or a ratio ('/')"); match(_);)
      {
        delete ancfg().bgframes;
        uintptr_t first; char mode;
        _ >> first >> mode;
        if (mode)
          {
            uintptr_t second; _ >> second;
            switch (mode)
              {
              case '-': ancfg().bgframes = new RangeBGSel(first, second); break;
              case '/': ancfg().bgframes = new RatioBGSel(first, second); break;
              default: std::cerr << _.name << ": unexpected separator: " << mode << '\n'; throw _;
              }
          }
        else
          {
            ancfg().bgframes = new RangeBGSel(0, first);
          }
        return true;
      }
      
    for (Param _("threshold", "<value>", "Threshold value for detection."); match(_);)
      {
        _ >> ancfg().threshold;
        return true;
      }
      
    for (Param _("stop", "<bound>", "Maximum frames considered."); match(_);)
      {
        _ >> opcfg().framestop;
        return true;
      }

    for (Param _("keylog", "<speed>", "Play video at <speed> and activate key logger (e.g. keylog:0.5 plays video at 2x slower)."); match(_);)
      {
        _ >> opcfg().keylogspeed;
        return true;
      }
    
    for (Param _("elongation", "<ratio>", "Minimum mice body elongation considered for orientation"); match(_);)
      {
        _ >> ancfg().minelongation;
        return true;
      }
    
    return false;
  }
  
  virtual bool match(Param& _) = 0;
  virtual Analyser& ancfg() { throw 0; return *(Analyser*)0; }
  virtual Operands& opcfg() { throw 0; return *(Operands*)0; }
};

void help(char const* appname, std::ostream& sink)
{
  int spacing = 0;
  struct GetSpacing : public Params
  {
    GetSpacing(int& _s) : s(_s) { all(); } int& s;
    virtual bool match( Param& param ) override { int l = strlen(param.name) + strlen(param.args_help); if (s < l) s = l; return false; }
  } gs(spacing);

  sink << "Usage: " << appname << " [<param1>:<config1> <paramN>:<configN>] <video>\n\nParameters:\n";
  struct PrintParams : public Params
  {
    PrintParams(std::ostream& _sink, int _spc) : sink(_sink), spc(_spc) { all(); } std::ostream& sink; int spc;
    virtual bool match( Param& param ) override { param.usage( sink << "  ", spc+3 ); return false; }
  } pp(sink, spacing);
}

int
main( int argc, char** argv )
{
  Analyser analyser;
  Operands operands;

  try
    {
      struct GetParams : Params
      {
        GetParams( char const* _self, Analyser& _analyser, Operands& _operands )
          : self(_self), arg(), analyser(_analyser), operands(_operands), verbose(true)
        {}
    
        void parse( char** args )
        {
          analyser.args.push_back( self );
          while (char const* ap = arg = *++args)
            {
              for (char const* h; (((h = argsof("help",ap)) and not *h) or ((h = argsof("--help",ap)) and not *h) or ((h = argsof("-h",ap)) and not *h));)
                {
                  help(self, std::cout);
                  exit(0);
                }
          
              analyser.args.push_back( ap );
          
              if (all())
                continue;
              
              if (operands.video.size())
                { throw Param("error", " one video at a time please...", ""); }
              operands.video = ap;
            }
          
          if (not operands.video.size())
            { throw Param("error", " no video given...", ""); }
        }
        virtual Analyser& ancfg() override { return analyser; }
        virtual Operands& opcfg() override { return operands; }
        virtual bool match( Param& param ) override
        {
          char const* a = arg;
          for (char const *b = param.name; *b; ++a, ++b)
            { if (*a != *b) return false; }
          if (*a++ != ':') return false;
          param.set_args(a);
          if (verbose)
            { std::cerr << "[" << param.name << "] " << param.desc_help << "\n  " << a << " (" << param.args_help << ")\n"; }
          return true;
        }
    
        char const* self;
        char const* arg;
        Analyser& analyser;
        Operands& operands;
        bool verbose;
      } params(argv[0], analyser, operands);

      assert( argv[argc] == 0 );
      params.parse( argv );
    }
  catch (Params::Param const& param)
    {
      if (param.args)
        {
          std::cerr << "---\nParameter read error:\n  " << param.args_start << "\n  ";
          for (char const* cp = param.args_start; cp < param.args; ++cp)
            std::cerr << (isspace(*cp) ? *cp : ' '); /* XXX: unicode ? */
          std::cerr << "^\n";
        }
      param.usage( std::cerr, 0 );
      return 1;
    }
  
  std::string prefix( operands.video );
  
  {
    uintptr_t idx = prefix.rfind('.');
    if (idx < prefix.size())
      prefix = prefix.substr(0,idx);
  }
  
  std::cerr << "Pass #0\n";
  for (VideoFrameIterator itr( operands.video, operands.framestop ); itr.next(); )
    {
      itr.progress(std::cerr);
      analyser.pass0( itr );
    }
  std::cerr << std::endl;

  analyser.background();
  
  std::cerr << "Pass #1\n";
  for (VideoFrameIterator itr( operands.video, operands.framestop ); itr.next(); )
    {
      itr.progress(std::cerr);
      analyser.pass1( itr.frame );
    }
  std::cerr << std::endl;
  
  analyser.trajectory();
  
  cv::namedWindow( "w", cv::WINDOW_AUTOSIZE );
  
  cv::setMouseCallback( "w", (cv::MouseCallback)mouse_callback, &analyser );
  bool keylogger = operands.keylogspeed;
  int kwait = 0;
  cv::VideoWriter writer;
  typedef std::map<double,char> KeyLog;
  KeyLog keylog;
    
  for (VideoFrameIterator itr( operands.video, operands.framestop ); itr.next();)
    {
      analyser.redraw( itr );
      imshow( "w", itr.frame );
      int k = cv::waitKey(kwait);

      if (writer.isOpened())
        writer << itr.frame;
      
      if (k == -1)
        continue;
      
      if (kwait)
        {
          // Play mode, log key if necessary
          if (keylogger)
            keylog.insert(KeyLog::value_type(itr.sec(), k));
          continue;
        }

      switch (k)
        {
        case 'r':
          writer.open( (prefix + "_rec.avi").c_str(), CV_FOURCC('M','J','P','G'), 25, cv::Size( analyser.width(), analyser.height() ) );
          /* move on to set kwait */
        case '\n': case '\r':
          kwait = keylogger ? std::max<int>(1000./(itr.fps*operands.keylogspeed), 1) : 1;
          break;
        case '\b': 
          analyser.restart();
          break;
        default:
          std::cerr << "KeyCode: " << k << "\n";
          break;
        }
    }
  
  if (writer.isOpened())
    writer.release();
  
  cv::destroyWindow( "w" );
  
  std::ofstream sink( (prefix + ".csv").c_str() );
  analyser.dumpresults( sink );

  if (keylog.size())
    {
      std::ofstream sink( (prefix + "_keys.csv").c_str() );
      for (KeyLog::value_type const& key : keylog)
	sink << key.first << "," << key.second << "\n";
    }
  
  return 0;
}
