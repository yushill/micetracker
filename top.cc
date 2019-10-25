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

template <uintptr_t SZ>
char*
argsof( char const (&prefix)[SZ], char* arg )
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
    char* args_start;
    char* args;
    void set_args(char*_args) { args = args_start = _args; }

    Param& operator >> ( unsigned& value ) { value = strtoul(args,&args,0); return *this; }
    Param& operator >> ( unsigned long& value ) { value = strtoul(args,&args,0); return *this; }
    Param& operator >> ( unsigned long long& value ) { value = strtoull(args,&args,0); return *this; }
    Param& operator >> ( int& value ) { value = strtol(args,&args,0); return *this; }
    Param& operator >> ( long& value ) { value = strtol(args,&args,0); return *this; }
    Param& operator >> ( long long& value ) { value = strtoll(args,&args,0); return *this; }
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
    Param& operator >> ( double& value ) { value = strtod(args,&args); return *this; }
  };
  
  struct Ouch {};
  void all()
  {
    for (Param _("crop", "<left>:<right>:<top>:<bottom>", "Narrows studied region by given margins."); match(_);)
      {
        analyser().args.pop_back();
        char sep = ':';
        for (int idx = 0; idx < 4; ++idx) {
          if (sep != ':') throw _;
          _ >> analyser().crop[idx] >> sep;
        }
        if (sep != '\0') throw _;
        return;
      }

    for (Param _("fps", "<fps value>", "Frame per second in continuous video reading."); match(_);)
      {
        _ >> analyser().fps;
        return;
      }
      
    for (Param _("hilite", "<is_hilite>", "Hilite mice location."); match(_);)
      {
        _ >> analyser().hilite;
        return;
      }

    for (Param _("bgframes", "<start>-<end> | <kept>/<outof>", "Frame considered for background computation."); match(_);)
      {
        delete analyser().bgframes;
        uintptr_t first; char mode;
        _ >> first >> mode;
        if (mode)
          {
            uintptr_t second; _ >> second;
            switch (mode)
              {
              case '-': analyser().bgframes = new RangeBGSel(first, second); break;
              case '/': analyser().bgframes = new RatioBGSel(first, second); break;
              default: std::cerr << "unexpected separator: " << mode << '\n'; throw _;
              }
          }
        else
          {
            analyser().bgframes = new RangeBGSel(0, first);
          }
        return;
      }
      
    for (Param _("threshold", "<value>", "Threshold value for detection."); match(_);)
      {
        _ >> analyser().threshold;
        return;
      }
      
    for (Param _("stop", "<bound>", "Maximum frames considered."); match(_);)
      {
        _ >> analyser().stop;
        return;
      }
    
    for (Param _("elongation", "<ratio>", "Minimum mice body elongation considered for orientation"); match(_);)
      {
        _ >> analyser().minelongation;
        return;
      }
  }
  virtual bool match(Param& _) = 0;
  virtual Analyser& analyser() { throw 0; return *(Analyser*)0;  }
};

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

      else if ((param = argsof( "fps:",argv[aidx] )))
        {
          analyser.fps = strtoul( param, &param, 0 );
          std::cerr << "Using FPS: " << unsigned(analyser.fps) << "\n";
        }
      
      else if ((param = argsof( "hilite:", argv[aidx] )))
        {
          analyser.hilite = true;
        }
      
      else if ((param = argsof( "bgframes:", argv[aidx] )))
	{
	  delete analyser.bgframes;
	  uintptr_t first = strtoull( param, &param, 0 );
	  char mode = *param;
	  if (mode)
	    {
	      uintptr_t second = strtoull( param+1, &param, 0 );
	      switch (mode)
		{
		case '-': analyser.bgframes = new RangeBGSel(first, second); break;
		case ':': analyser.bgframes = new RatioBGSel(first, second); break;
		default:
		  std::cerr << "unexpected char: " << mode << " in bgframes switch.";
		  return 1;
		}
	    }
	  else
	    {
	      analyser.bgframes = new RangeBGSel(0, first);
	    }
	  std::cerr << "Using bgframes: ";
	  analyser.bgframes->repr( std::cerr );
	  std::cerr << "\n";
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
