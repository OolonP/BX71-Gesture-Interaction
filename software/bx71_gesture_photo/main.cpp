// BX71 single-photo palm and hand landmark verification.
// Model preprocessing follows OpenCV Zoo MPPalmDet / MPHandPose conventions.
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "gesture.hpp"

using Clock = std::chrono::steady_clock;
struct Palm { cv::Point2f lo, hi; std::array<cv::Point2f,7> points; float score; };
struct Crop { cv::Mat image; cv::Point2f lo, hi, bias; };

static cv::Point2f transform(const cv::Mat& m, cv::Point2f p) {
    return cv::Point2f(float(m.at<double>(0,0)*p.x+m.at<double>(0,1)*p.y+m.at<double>(0,2)),
                       float(m.at<double>(1,0)*p.x+m.at<double>(1,1)*p.y+m.at<double>(1,2)));
}
static cv::Mat inputNHWC(const cv::Mat& rgb, int side) {
    cv::Mat resized, f;
    cv::resize(rgb,resized,cv::Size(side,side),0,0,cv::INTER_AREA);
    resized.convertTo(f,CV_32FC3,1.0/255.0);
    int dims[] = {1,side,side,3};
    return cv::Mat(4,dims,CV_32F,f.ptr<float>()).clone();
}
static cv::dnn::Net load(const std::string& path) {
    cv::dnn::Net n=cv::dnn::readNetFromONNX(path);
    n.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    n.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    return n;
}
static bool detect(cv::dnn::Net& net,const cv::Mat& image,Palm& palm) {
    const float ratio=192.0f/std::max(image.cols,image.rows);
    int w=std::max(1,int(image.cols*ratio)),h=std::max(1,int(image.rows*ratio));
    int left=(192-w)/2,top=(192-h)/2;
    cv::Mat resized,padded,rgb;
    cv::resize(image,resized,cv::Size(w,h));
    cv::copyMakeBorder(resized,padded,top,192-h-top,left,192-w-left,cv::BORDER_CONSTANT);
    cv::cvtColor(padded,rgb,cv::COLOR_BGR2RGB);
    net.setInput(inputNHWC(rgb,192));
    std::vector<cv::Mat> out;
    net.forward(out,net.getUnconnectedOutLayersNames());
    if(out.size()!=2 || out[0].total()!=2016*18 || out[1].total()!=2016)
        throw std::runtime_error("Unexpected palm model output shape");
    std::vector<cv::Point2f> anchors;
    for(int grid : {24,12}) {
        int repeats=grid==24?2:6;
        for(int y=0;y<grid;++y) for(int x=0;x<grid;++x)
            for(int r=0;r<repeats;++r)
                anchors.emplace_back((x+0.5f)/grid,(y+0.5f)/grid);
    }
    const float* scores=out[1].ptr<float>();
    int best=-1; float bestScore=0;
    for(int i=0;i<2016;++i) {
        float s=1.0f/(1.0f+std::exp(-std::max(-80.0f,std::min(80.0f,scores[i]))));
        if(s>bestScore) {bestScore=s;best=i;}
    }
    std::cout<<"PALM_SCORE="<<bestScore<<std::endl;
    if(best<0 || bestScore<0.5f) return false;
    const float* d=out[0].ptr<float>()+best*18;
    float scale=float(std::max(image.cols,image.rows));
    cv::Point2f bias(float(int(left/ratio)),float(int(top/ratio)));
    cv::Point2f center=(anchors[best]+cv::Point2f(d[0]/192,d[1]/192))*scale-bias;
    cv::Point2f half(d[2]*scale/384,d[3]*scale/384);
    palm.lo=center-half;palm.hi=center+half;palm.score=bestScore;
    if(half.x<=0 || half.y<=0) return false;
    for(int i=0;i<7;++i)
        palm.points[i]=(anchors[best]+cv::Point2f(d[4+2*i]/192,d[5+2*i]/192))*scale-bias;
    return true; // Single-hand diagnostic: use highest-scoring palm only.
}
static Crop cropPad(const cv::Mat& image,cv::Point2f lo,cv::Point2f hi,bool rotate) {
    cv::Point2f wh=hi-lo,center=(lo+hi)*0.5f;
    if(!rotate) center.y-=0.4f*wh.y;
    float factor=rotate?4.0f:3.0f;
    cv::Point2f half=wh*(factor/2);
    int x0=std::max(0,std::min(image.cols,int(center.x-half.x)));
    int y0=std::max(0,std::min(image.rows,int(center.y-half.y)));
    int x1=std::max(0,std::min(image.cols,int(center.x+half.x)));
    int y1=std::max(0,std::min(image.rows,int(center.y+half.y)));
    if(x1<=x0 || y1<=y0) throw std::runtime_error("Palm crop is outside image");
    cv::Mat roi=image(cv::Rect(x0,y0,x1-x0,y1-y0));
    int side=rotate?int(std::hypot(roi.rows,roi.cols)):std::max(roi.rows,roi.cols);
    int left=(side-roi.cols)/2,top=(side-roi.rows)/2;
    Crop c;
    cv::copyMakeBorder(roi,c.image,top,side-roi.rows-top,left,side-roi.cols-left,cv::BORDER_CONSTANT);
    c.lo=cv::Point2f(float(x0),float(y0));c.hi=cv::Point2f(float(x1),float(y1));
    c.bias=cv::Point2f(float(x0-left),float(y0-top));
    return c;
}
static bool landmarks(cv::dnn::Net& net,const cv::Mat& image,const Palm& palm,
                      std::array<cv::Point2f,21>& points) {
    Crop pre=cropPad(image,palm.lo,palm.hi,true);
    cv::Point2f p1=palm.points[0]-pre.bias,p2=palm.points[2]-pre.bias;
    const double pi=std::acos(-1.0);
    double radians=pi/2-std::atan2(-(p2.y-p1.y),p2.x-p1.x);
    radians-=2*pi*std::floor((radians+pi)/(2*pi));
    cv::Point2f center=(pre.lo+pre.hi)*0.5f-pre.bias;
    cv::Mat rotation=cv::getRotationMatrix2D(center,radians*180/pi,1);
    cv::Mat rgb,rotated;
    cv::cvtColor(pre.image,rgb,cv::COLOR_BGR2RGB);
    cv::warpAffine(rgb,rotated,rotation,rgb.size());
    cv::Point2f lo(1e20f,1e20f),hi(-1e20f,-1e20f);
    for(auto p:palm.points) {
        p=transform(rotation,p-pre.bias);
        lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);
    }
    Crop hand=cropPad(rotated,lo,hi,false);
    net.setInput(inputNHWC(hand.image,224));
    std::vector<cv::Mat> out;
    net.forward(out,net.getUnconnectedOutLayersNames());
    if(out.size()!=4 || out[0].total()!=63 || out[1].total()!=1)
        throw std::runtime_error("Unexpected landmark model output shape");
    float confidence=out[1].ptr<float>()[0];
    std::cout<<"HAND_SCORE="<<confidence<<std::endl;
    if(!std::isfinite(confidence) || confidence<0.8f) return false;
    cv::Mat inverse;
    cv::invertAffineTransform(rotation,inverse);
    const float* xy=out[0].ptr<float>();
    // Invert resize, square padding, rotation, and first crop padding.
    for(int i=0;i<21;++i) {
        cv::Point2f p(xy[3*i]*hand.image.cols/224.0f,
                      xy[3*i+1]*hand.image.rows/224.0f);
        if(!std::isfinite(p.x)||!std::isfinite(p.y))
            throw std::runtime_error("Non-finite landmark coordinates");
        points[i]=transform(inverse,p+hand.bias)+pre.bias;
    }
    return true;
}
int main(int argc,char** argv) {
    if(argc!=5) {
        std::cerr<<"Usage: bx71_gesture_photo palm.onnx hand.onnx input.jpg output.jpg\n";
        return 1;
    }
    try {
        cv::setNumThreads(2);
        cv::Mat image=cv::imread(argv[3]);
        if(image.empty()) throw std::runtime_error("Cannot read input photo");
        cv::Mat result=image.clone();
        auto palmNet=load(argv[1]),handNet=load(argv[2]);
        auto start=Clock::now();
        Palm palm;std::array<cv::Point2f,21> pts;
        bool found=detect(palmNet,image,palm),valid=false;
        if(found) {
            cv::rectangle(result,palm.lo,palm.hi,cv::Scalar(0,180,255),2);
            for(auto p:palm.points) cv::circle(result,p,3,cv::Scalar(255,0,0),-1);
            valid=landmarks(handNet,image,palm,pts);
            if(valid) {
                const int edges[][2]={{0,1},{1,2},{2,3},{3,4},{0,5},{5,6},{6,7},{7,8},
                    {5,9},{9,10},{10,11},{11,12},{9,13},{13,14},{14,15},{15,16},
                    {13,17},{0,17},{17,18},{18,19},{19,20}};
                for(const auto& e:edges) cv::line(result,pts[e[0]],pts[e[1]],cv::Scalar(0,220,0),2);
                for(int i=0;i<21;++i) {
                    cv::circle(result,pts[i],3,cv::Scalar(0,0,255),-1);
                    cv::putText(result,std::to_string(i),pts[i]+cv::Point2f(4,-4),
                                cv::FONT_HERSHEY_SIMPLEX,0.35,cv::Scalar(255,100,0),1);
                }
            }
        }
        std::string status=valid?"LANDMARKS_OK":(found?"HAND_LOW_CONFIDENCE":"NO_PALM");
        if(valid) {
            auto g=gesture::classify(pts);
            std::cout<<"GESTURE="<<g.name<<" COMMAND="<<g.command<<" FINGERS=";
            for(auto s:g.fingers) std::cout<<(s<0?'?':char('0'+s));
            std::cout<<std::endl;
            cv::putText(result,g.name,cv::Point(8,48),cv::FONT_HERSHEY_SIMPLEX,
                        0.65,cv::Scalar(0,0,255),2);
        } else std::cout<<"GESTURE=NONE COMMAND=-"<<std::endl;
        cv::putText(result,status,cv::Point(8,24),cv::FONT_HERSHEY_SIMPLEX,0.55,cv::Scalar(0,0,255),2);
        if(!cv::imwrite(argv[4],result)) throw std::runtime_error("Cannot save result photo");
        std::cout<<status<<" seconds="<<std::chrono::duration<double>(Clock::now()-start).count()
                 <<" saved="<<argv[4]<<std::endl;
        return valid?0:2;
    } catch(const std::exception& e) {std::cerr<<"ERROR: "<<e.what()<<std::endl;return 1;}
}
