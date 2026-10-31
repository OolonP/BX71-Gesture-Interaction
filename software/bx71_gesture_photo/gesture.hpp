#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <string>

// Experimental 2D geometric rules. Oblique/ambiguous poses may be UNKNOWN.
namespace gesture {
struct Point { double x, y; };
inline double distance(Point a, Point b) { return std::hypot(a.x-b.x,a.y-b.y); }
inline double angle(Point a, Point b, Point c) {
    double ux=a.x-b.x, uy=a.y-b.y, vx=c.x-b.x, vy=c.y-b.y;
    double n=std::hypot(ux,uy)*std::hypot(vx,vy);
    if(n<1e-8) return 0;
    return std::acos(std::max(-1.0,std::min(1.0,(ux*vx+uy*vy)/n)))*180/std::acos(-1.0);
}
struct Result { std::string name="UNKNOWN"; char command='-'; std::array<int,4> fingers{{-1,-1,-1,-1}}; };
template<class Points> Result classify(const Points& input) {
    Result r;
    std::array<Point,21> p;
    for(int i=0;i<21;++i) {
        p[i]={double(input[i].x),double(input[i].y)};
        if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)) return r;
    }
    double width=distance(p[5],p[17]);
    if(width<1e-5 || distance(p[0],p[9])<1e-5) return r;
    for(int f=0;f<4;++f) {
        int b=5+4*f;
        double a=angle(p[b],p[b+1],p[b+2]);
        double d=angle(p[b+1],p[b+2],p[b+3]);
        double base=distance(p[b+1],p[0]);
        double tip=distance(p[b+3],p[0]);
        if(a>155 && d>150 && tip>1.15*base) r.fingers[f]=1;
        else if(a<130 || tip<0.9*base) r.fingers[f]=0;
    }
    const auto& s=r.fingers;
    // Thumb is ignored for fist/index/V; this accepts natural thumb positions.
    if(s==std::array<int,4>{{0,0,0,0}}) {r.name="FIST";r.command='1';}
    else if(s==std::array<int,4>{{1,0,0,0}}) {r.name="INDEX";r.command='2';}
    else if(s==std::array<int,4>{{1,1,0,0}}) {r.name="V_SIGN";r.command='3';}
    else if(s==std::array<int,4>{{1,1,1,1}}) {
        bool thumb=angle(p[1],p[2],p[3])>145 && angle(p[2],p[3],p[4])>145
            && distance(p[4],p[5])>0.55*width
            && distance(p[4],p[5])>1.1*distance(p[3],p[5]);
        if(thumb) {r.name="OPEN_PALM";r.command='0';}
    }
    return r;
}
}
