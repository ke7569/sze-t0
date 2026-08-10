#include "snapshot_legacy15_factors.h"

#include <algorithm>
#include <cmath>

namespace sze_snapshot15 {
namespace {
const double kEps = 1.0e-6;
double clamp(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }
double mid(const Snapshot& s) { return (s.bid_prices[0] + s.ask_prices[0]) * 0.5; }
double total(const std::array<std::int64_t,5>& v) { double x=0; for(auto n:v)x+=n; return x; }
double ask_dot(const Snapshot& s) { double x=0; for(int i=0;i<5;++i)x+=s.ask_prices[i]*s.ask_volumes[i]; return x; }
double bid_dot(const Snapshot& s) { double x=0; for(int i=0;i<5;++i)x+=s.bid_prices[i]*s.bid_volumes[i]; return x; }
double weighted_ask(const Snapshot& s) { const double v=total(s.ask_volumes); return v==0?0:ask_dot(s)/v-mid(s); }
double weighted_bid(const Snapshot& s) { const double v=total(s.bid_volumes); return v==0?0:mid(s)-bid_dot(s)/v; }
double classic_hermes(const Snapshot& s) {
    double sum=0, weights=0;
    for(int i=0;i<5;++i){ if(s.bid_volumes[i]==0||s.ask_volumes[i]==0)break; const double w=5-i;
        sum += (s.ask_prices[i]*s.bid_volumes[i]+s.bid_prices[i]*s.ask_volumes[i])/(s.ask_volumes[i]+s.bid_volumes[i])*w; weights+=w; }
    return weights==0?s.last_price:sum/weights;
}
double hermes(const Snapshot& s) { const double m=mid(s); return (m<0.01||m>1e6)?0:clamp((classic_hermes(s)/m-1)*1000,-5,5); }
double vol_change(const Snapshot& a,const Snapshot& b,bool ask){
    const double q=b.ask_volumes[0]+b.bid_volumes[0]; if(q==0)return 0;
    double x=0;
    if(ask){ if(b.ask_prices[0]<a.ask_prices[0]-kEps)x=(a.bid_volumes[0]+b.ask_volumes[0])/q;
        else if(b.ask_prices[0]>a.ask_prices[0]+kEps)x=-a.ask_volumes[0]/q; else x=(b.ask_volumes[0]-a.ask_volumes[0])/q; }
    else { if(b.bid_prices[0]<a.bid_prices[0]-kEps)x=-a.bid_volumes[0]/q;
        else if(b.bid_prices[0]>a.bid_prices[0]+kEps)x=(a.ask_volumes[0]+b.bid_volumes[0])/q; else x=(b.bid_volumes[0]-a.bid_volumes[0])/q; }
    return clamp(x,-5,5);
}
double price_pressure(const Snapshot&s,int l){ const double q=s.ask_volumes[l]+s.bid_volumes[l]; if(q==0)return 0; return (s.ask_prices[l]*s.bid_volumes[l]+s.bid_prices[l]*s.ask_volumes[l])/q-mid(s); }
double volume_pressure(const Snapshot&s,int l){ if(s.ask_volumes[l]==0||s.bid_volumes[l]==0)return 0; const double a=std::sqrt((double)s.ask_volumes[l]),b=std::sqrt((double)s.bid_volumes[l]); return (a-b)/(a+b); }
double weighted_price(const Snapshot&s,int levels){ double q=0,n=0; for(int i=0;i<levels;++i){q+=s.ask_volumes[i]+s.bid_volumes[i];n+=s.ask_prices[i]*s.ask_volumes[i]+s.bid_prices[i]*s.bid_volumes[i];}return q==0?mid(s):n/q;}
double order_move(const Snapshot&a,const Snapshot&b,bool ask){
    for(int i=0;i<5;++i)if((ask?a.ask_volumes[i]:a.bid_volumes[i])==0||(ask?b.ask_volumes[i]:b.bid_volumes[i])==0)return 0;
    double n=0,q=0;
    if(ask){ if(b.ask_prices[0]>a.ask_prices[0])for(int i=0;i<5&&a.ask_prices[i]<b.ask_prices[0];++i){n+=a.ask_prices[i]*a.ask_volumes[i];q+=a.ask_volumes[i];}
        else if(b.ask_prices[0]<a.ask_prices[0])for(int i=0;i<5&&b.ask_prices[i]<a.ask_prices[0];++i){n+=b.ask_prices[i]*b.ask_volumes[i];q+=b.ask_volumes[i];}
        return q==0?0:n/q-a.ask_prices[0]; }
    if(b.bid_prices[0]>a.bid_prices[0])for(int i=0;i<5&&b.bid_prices[i]>a.bid_prices[0];++i){n+=b.bid_prices[i]*b.bid_volumes[i];q+=b.bid_volumes[i];}
    else if(b.bid_prices[0]<a.bid_prices[0])for(int i=0;i<5&&a.bid_prices[i]>b.bid_prices[0];++i){n+=a.bid_prices[i]*a.bid_volumes[i];q+=a.bid_volumes[i];}
    return q==0?0:n/q-a.bid_prices[0];
}
double volume_shift(const Snapshot&a,const Snapshot&b,int level,bool ask){
    const auto& av=ask?a.ask_volumes:a.bid_volumes; const auto& bv=ask?b.ask_volumes:b.bid_volumes;
    const auto& ap=ask?a.ask_prices:a.bid_prices; const auto& bp=ask?b.ask_prices:b.bid_prices;
    const std::int64_t start=av[level]; if(start==0)return 0; std::int64_t diff=0;
    if(std::fabs(bp[level]-ap[level])<kEps)diff=bv[level]-start;
    else if((ask&&bp[level]<ap[level]-kEps)||(!ask&&bp[level]>ap[level]+kEps)){
        diff=bv[level]; for(int i=level+1;i<5;++i){ const bool beyond=ask?bp[i]<ap[level]-kEps:bp[i]>ap[level]+kEps; if(beyond){diff+=bv[i];continue;} if(std::fabs(bp[i]-ap[level])<kEps){diff+=bv[i]-start;break;} }
    }else{
        diff=-bv[level]; for(int i=level-1;i>=0;--i){ const bool beyond=ask?bp[i]>ap[level]+kEps:bp[i]<ap[level]-kEps; if(beyond){diff-=bv[i];continue;} if(std::fabs(bp[i]-ap[level])<kEps){diff-=bv[i]-start;break;} }
    }
    return clamp((double)diff/start,-5,5);
}
}

bool valid_snapshot(const Snapshot& s){
    return std::isfinite(s.ask_prices[0])&&s.ask_prices[0]>0&&std::isfinite(s.bid_prices[0])&&s.bid_prices[0]>0&&
        s.ask_volumes[0]>0&&s.bid_volumes[0]>0&&s.volume>=0&&std::isfinite(s.turnover)&&s.turnover>=0;
}

bool build_factors(const Snapshot&a,const Snapshot&b,std::array<float,36>*o){
    if(!o||!valid_snapshot(a)||!valid_snapshot(b)||b.volume<a.volume||b.turnover<a.turnover)return false;
    const double m=mid(b),sm=mid(a),dv=b.volume-a.volume,spread=a.ask_prices[0]-a.bid_prices[0];
    double atp_rev=0,sqrt_pos=0;
    if(dv!=0){const double atp=(b.turnover-a.turnover)/dv;atp_rev=atp/sm-1;if(std::fabs(spread)>kEps){const double r=clamp((atp-sm)/spread,-.5,.5);sqrt_pos=(std::sqrt(dv*(.5+r))-std::sqrt(dv*(.5-r)))/std::sqrt(dv);}}
    std::array<double,36>x{};
    x[0]=hermes(b);x[1]=hermes(b)-hermes(a);x[2]=(m-sm)/m*1000;x[3]=clamp(atp_rev*1000,-10,10);x[4]=vol_change(a,b,true);x[5]=vol_change(a,b,false);x[6]=sqrt_pos;
    x[7]=price_pressure(b,0)/m*1000;x[8]=price_pressure(b,1)/m*1000;x[9]=volume_pressure(b,0);x[10]=volume_pressure(b,1);
    const double at=total(a.ask_volumes),bt=total(b.ask_volumes),ab=total(a.bid_volumes),bb=total(b.bid_volumes);
    x[11]=(at*bt==0?0:ask_dot(b)/bt-ask_dot(a)/at)/m*1000;x[12]=(ab*bb==0?0:bid_dot(b)/bb-bid_dot(a)/ab)/m*1000;
    x[13]=order_move(a,b,true)/m*1000;x[14]=order_move(a,b,false)/m*1000;x[15]=(b.ask_prices[0]-b.bid_prices[0])/m*1000;
    for(int i=1;i<=5;++i)x[15+i]=clamp((weighted_price(b,i)-weighted_price(a,i))/m*1000,-100,100);
    x[21]=weighted_ask(b)/m*1000;x[22]=weighted_bid(b)/m*1000;x[23]=(weighted_ask(b)-weighted_ask(a))/sm*1000;x[24]=(weighted_bid(b)-weighted_bid(a))/sm*1000;
    double wa=0,wb=0;for(int i=0;i<5;++i){wa+=b.ask_volumes[i]*(5-i);wb+=b.bid_volumes[i]*(5-i);}x[25]=wa+wb==0?0:wa/(wa+wb)-.5;
    x[26]=bt+bb==0?0:bt/(bt+bb)-.5;const double quote=ask_dot(b)+bid_dot(b);x[27]=quote==0?0:(b.turnover-a.turnover)/quote;x[28]=bt==0?0:b.ask_volumes[0]/bt;x[29]=bb==0?0:b.bid_volumes[0]/bb;
    for(int i=0;i<3;++i){x[30+i]=volume_shift(a,b,i,true);x[33+i]=volume_shift(a,b,i,false);}
    for(std::size_t i=0;i<36;++i){if(!std::isfinite(x[i]))x[i]=0;(*o)[i]=(float)x[i];}
    return true;
}
}  // namespace sze_snapshot15
