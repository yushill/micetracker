#ifndef __GEOMETRY_HH__
#define __GEOMETRY_HH__

#include <inttypes.h>

template <typename dist_type>
struct Point
{
  Point() : x( 0 ), y( 0 ) {}
  Point( dist_type _x, dist_type _y ) : x( _x ), y( _y ) {}
  
  Point operator+( Point const& rpt ) const { return Point( x+rpt.x, y+rpt.y ); }
  Point operator-( Point const& rpt ) const { return Point( x-rpt.x, y-rpt.y ); }
  Point operator-() const { return Point( -x, -y ); }
  Point operator*( dist_type rsc ) const { return Point( x*rsc, y*rsc ); }
  Point operator/( dist_type rsc ) const { return Point( x/rsc, y/rsc ); }
  dist_type operator*( Point const& rpt ) const { return (x*rpt.x + y*rpt.y); }
  Point operator!() const { return Point( -y, x ); }
  bool operator!=( Point const& rpt ) const { return (x != rpt.x) or (y != rpt.y); }
  bool operator==( Point const& rpt ) const { return (x == rpt.x) and (y == rpt.y); }
  template <typename T> void pull( T& rect ) const { rect.x = x; rect.y = y; }
  template <typename T> void pull( T& x, T& y ) const { x = x; y = y; }
  dist_type sqnorm() const { return x*x + y*y; }
  template <typename otherT> Point<otherT> rebind() const { return Point<otherT>( x, y ); }

  Point& operator+=( Point const& rpt ) { x+=rpt.x; y+=rpt.y; return *this; }
  Point& operator-=( Point const& rpt ) { x-=rpt.x; y-=rpt.y; return *this; }
  Point& operator*=( dist_type rsc ) { x *= rsc; y *= rsc; return *this; }
  Point& operator/=( dist_type rsc ) { x /= rsc; y /= rsc; return *this; }
  
  dist_type x, y;
};

template <typename dist_type>
Point<dist_type> mkpoint( dist_type _x, dist_type _y ) { return Point<dist_type>( _x, _y ); }

#endif /* __GEOMETRY_HH__ */
