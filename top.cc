#include <analysis.hh>
#include <geometry.hh>
#include <fstream>
#include <iostream>
#include <string>
#include <limits>
#include <cmath>
#include <cstdarg>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#include <inttypes.h>

template <uintptr_t SZ, typename T>
T
argsof( char const (&prefix)[SZ], T arg )
{
  return strncmp( &prefix[0], arg, SZ-1 ) ? 0 : &arg[SZ-1];
}

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

struct RangeBGSel : public Analyser::BGSel
{
  RangeBGSel( uintptr_t _lower, uintptr_t _upper ) : lower(_lower), upper(_upper) {}
  virtual bool accept( uintptr_t frame ) override { return (frame >= lower and frame < upper); }
  virtual void repr( std::ostream& sink ) override { sink << "Range( " << lower << ", " << upper << " )"; };
  
  uintptr_t lower;
  uintptr_t upper;
};

struct RatioBGSel : public Analyser::BGSel
{
  RatioBGSel( uintptr_t _first, uintptr_t _second ) : first(_first), period(_first+_second) {}
  virtual bool accept( uintptr_t frame ) override { return (frame % period) < first; }
  virtual void repr( std::ostream& sink ) override { sink << "Ratio( " << first << ", " << (period-first) << " )"; };
  
  uintptr_t first;
  uintptr_t period;
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
        cfg().args.pop_back();
        char sep = ':';
        for (int idx = 0; idx < 4; ++idx) {
          if (sep != ':') throw _;
          _ >> cfg().crop[idx] >> sep;
        }
        if (sep != '\0') throw _;
        return true;
      }

    for (Param _("fps", "<fps value>", "Frame per second in continuous video reading."); match(_);)
      {
        _ >> cfg().fps;
        return true;
      }
      
    for (Param _("hilite", "<is_hilite>", "Hilite mice location."); match(_);)
      {
        _ >> cfg().hilite;
        return true;
      }

    for (Param _("bgframes", "<arg1>[[-/]<arg2>]", "Frames considered for background computation; either: a count from start, a range ('-') or a ratio ('/')"); match(_);)
      {
        delete cfg().bgframes;
        uintptr_t first; char mode;
        _ >> first >> mode;
        if (mode)
          {
            uintptr_t second; _ >> second;
            switch (mode)
              {
              case '-': cfg().bgframes = new RangeBGSel(first, second); break;
              case '/': cfg().bgframes = new RatioBGSel(first, second); break;
              default: std::cerr << _.name << ": unexpected separator: " << mode << '\n'; throw _;
              }
          }
        else
          {
            cfg().bgframes = new RangeBGSel(0, first);
          }
        return true;
      }
      
    for (Param _("threshold", "<value>", "Threshold value for detection."); match(_);)
      {
        _ >> cfg().threshold;
        return true;
      }
      
    for (Param _("stop", "<bound>", "Maximum frames considered."); match(_);)
      {
        _ >> cfg().stop;
        return true;
      }
    
    for (Param _("elongation", "<ratio>", "Minimum mice body elongation considered for orientation"); match(_);)
      {
        _ >> cfg().minelongation;
        return true;
      }
    
    return false;
  }
  virtual bool match(Param& _) = 0;
  virtual Analyser& cfg() { throw 0; return *(Analyser*)0;  }
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
  analyser.args.push_back( argv[0] );

  std::string filepath;
  
  try
    {
      struct GetParams : Params
      {
        GetParams( int argc, char** argv, Analyser& _analyser )
          : filepath(), self(argv[0]), args(argv), analyser(_analyser), verbose(true)
        {
          assert( argv[argc] == 0 );
          while (char const* ap = *++args)
            {
              for (char const* h; (((h = argsof("help",ap)) and not *h) or ((h = argsof("--help",ap)) and not *h) or ((h = argsof("-h",ap)) and not *h));)
                {
                  help(self, std::cout);
                  exit(0);
                }
              analyser.args.push_back( ap );

              bool is_param = all();
              if (not is_param)
                {
                  if (filepath.size())
                    { throw Param("error", " one video at a time please...", ""); }
                  filepath = ap;
                }
            }
          if (not filepath.size())
            { throw Param("error", " no video given...", ""); }
        }
        virtual Analyser& cfg() override { return analyser; }
        virtual bool match( Param& param ) override
        {
          char const* a = *args;
          for (char const *b = param.name; *b; ++a, ++b)
            { if (*a != *b) return false; }
          if (*a++ != ':') return false;
          param.set_args(a);
          if (verbose)
            { std::cerr << "[" << param.name << "] " << param.desc_help << "\n  " << a << " (" << param.args_help << ")\n"; }
          return true;
        }
        std::string filepath;
        char const* self; char** args;
        Analyser& analyser;
        bool verbose;
      } source(argc, argv, analyser);
      filepath = source.filepath;
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

  //  help( argv[0], std::cout );
  return 0;
  
  cvNamedWindow( "w", 1 );
  
  std::string prefix( filepath );
  
  {
    uintptr_t idx = prefix.rfind('.');
    if (idx < prefix.size())
      prefix = prefix.substr(0,idx);
  }
  
  std::cerr << "Pass #0\n";
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    {
      std::cerr << "\e[G\e[KFrame: " << itr.idx << " ";
      std::cerr.flush();
      analyser.pass0( itr );
    }
  std::cerr << std::endl;

  analyser.background();
  
  std::cerr << "Pass #1\n";
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    {
      std::cerr << "\e[G\e[KFrame: " << itr.idx << " ";
      std::cerr.flush();
      analyser.pass1( itr.frame );
    }
  std::cerr << std::endl;
  
  analyser.trajectory();
  
  cv::setMouseCallback( "w", (cv::MouseCallback)mouse_callback, &analyser );
  int kwait = 0;
  cv::VideoWriter writer;
  
  for (FrameIterator itr( filepath, analyser.stop ); itr.next(); )
    {
      analyser.redraw( itr );
      imshow( "w", itr.frame );
      char k = cvWaitKey(kwait);
      if (k == '\n')
        kwait = analyser.fps;
      else if (k == 'r') {
        writer.open( (prefix + "_rec.avi").c_str(), CV_FOURCC('M','J','P','G'), 25, cv::Size( analyser.width(), analyser.height() ) );
        kwait = analyser.fps;
      }
      else if (k == '\b')
        analyser.restart();
      else if (kwait == 0) {
        std::cerr << "KeyCode: " << int(k) << "\n";
      }
      
      if (writer.isOpened())
        writer << itr.frame;
    }
  
  if (writer.isOpened())
    writer.release();
  
  cvDestroyWindow( "w" );
  
  std::ofstream sink( (prefix + ".csv").c_str() );
  analyser.dumpresults( sink );
  
  return 0;
}
