#pragma once
#include "sdGemLoader.h"
#include <math.h>
#include <string.h>
#include <strings.h>
static_assert(sizeof(double)==8,"GEM requires IEEE-754 binary64 doubles");

// GemCAD native little-endian polygon stream. See GEM_AND_FOLDERS.md for
// format-reference attribution and supported variants. Never trust file sizes.
template<class Stream> GemSdResult readBinaryGem(Stream& file, GemSdDesign& design)
{
    auto bytes=[&](void* dst,size_t n) {return file.read(static_cast<uint8_t*>(dst),n)==int(n);};
    auto u32=[&](uint32_t& v) {uint8_t b[4];if(!bytes(b,4))return false;
        v=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;};
    auto real=[&](double& v) {uint8_t b[8];if(!bytes(b,8))return false;
        uint64_t bits=0;for(int i=0;i<8;++i)bits|=uint64_t(b[i])<<(8*i);
        memcpy(&v,&bits,8);return true;};
    auto string=[&](char* out,size_t capacity) {uint8_t n;if(!bytes(&n,1))return false;
        char buf[256]={};if(!bytes(buf,n))return false;
        snprintf(out,capacity,"%s",buf);return true;};
    if(file.size()>2*1024*1024 || file.size()<40) return GemSdResult::BAD_HEADER;
    std::vector<uint32_t> nativeTiers;
    while(file.position()+40<=file.size()) {
        const auto start=file.position();
        uint32_t zero,unknown,fold,mirror,gear;
        double ri,meridian;
        if(!u32(zero)||!u32(unknown)||!u32(fold)||!u32(mirror)||!u32(gear)||
           !real(ri)||!u32(unknown)||!real(meridian)) return GemSdResult::BAD_HEADER;
        const int32_t signedGear=int32_t(gear);
        if(zero==0 && fold>0 && fold<=400 && mirror<=1 && signedGear!=0 &&
           signedGear>=-400 && signedGear<=400 && ri>=1 && ri<=10 && fabs(meridian)<=400) {
            if(design.cuts.empty()) return GemSdResult::NO_CUTS;
            design.wheelIndex=abs(signedGear);design.designIndexSign=signedGear<0?-1:1;
            design.meridian=meridian;design.symmetry=fold;design.mirror=mirror;design.refractiveIndex=ri;
            snprintf(design.formatVersion,sizeof(design.formatVersion),"GEM binary");
            bool footnotes=false;
            while(file.position()<file.size()) {
                char text[256];if(!string(text,sizeof(text)))return GemSdResult::BAD_HEADER;
                if(!strcasecmp(text,"preform"))return GemSdResult::UNSUPPORTED_FILE;
                if(!text[0]) {footnotes=true;continue;}
                if(!footnotes && !design.title[0]) snprintf(design.title,sizeof(design.title),"%s",text);
                else if(!footnotes && !design.attribution[0]) snprintf(design.attribution,sizeof(design.attribution),"%s",text);
            }
            // During polygon reads index temporarily holds an azimuth in turns.
            for(auto& cut:design.cuts) {
                cut.index=fmod(cut.index*design.designIndexSign*design.wheelIndex+meridian,design.wheelIndex);
                if(cut.index<0)cut.index+=design.wheelIndex;
                double nearest=round(cut.index);
                if(fabs(cut.index-nearest)<1e-7)cut.index=nearest;
            }
            return GemSdResult::OK;
        }
        if(!file.seek(start))return GemSdResult::BAD_HEADER;
        double nx,ny,nz;uint32_t tier,marker;
        if(!real(nx)||!real(ny)||!real(nz)||!u32(tier)||!tier||tier>GEM_SD_MAX_CUTS)
            return GemSdResult::BAD_HEADER;
        double length=sqrt(nx*nx+ny*ny+nz*nz);
        if(!isfinite(length)||length<1e-12)return GemSdResult::BAD_NUMBER;
        nx/=length;ny/=length;nz/=length;
        char label[256];if(!string(label,sizeof(label))||!u32(marker)||marker!=1)return GemSdResult::BAD_HEADER;
        double distance=0;unsigned vertices=0;
        while(marker==1) {
            double x,y,z;if(!real(x)||!real(y)||!real(z)||!u32(marker)||marker>1)return GemSdResult::BAD_HEADER;
            if(!isfinite(x)||!isfinite(y)||!isfinite(z))return GemSdResult::BAD_NUMBER;
            double d=nx*x+ny*y+nz*z;
            if(!isfinite(d))return GemSdResult::BAD_NUMBER;
            if(!vertices)distance=d;
            else if(fabs(d-distance)>1e-6*fmax(1.0,fabs(distance)))return GemSdResult::BAD_NUMBER;
            if(++vertices>4096)return GemSdResult::BAD_HEADER;
        }
        if(vertices<3 || distance<=0)return GemSdResult::BAD_NUMBER;
        if(design.cuts.size()>=GEM_SD_MAX_CUTS)return GemSdResult::TOO_MANY_CUTS;
        // Canonical one-based cutting order, matching the PC converter even
        // when a native file uses sparse or out-of-order tier identifiers.
        size_t tierSlot=0;
        while(tierSlot<nativeTiers.size() && nativeTiers[tierSlot]!=tier)++tierSlot;
        if(tierSlot==nativeTiers.size())nativeTiers.push_back(tier);
        tier=uint32_t(tierSlot+1);
        GemCutCoordinate cut;
        cut.tier=tier;cut.facet=1;
        for(const auto& old:design.cuts)if(old.tier==tier)++cut.facet;
        cut.angleDegrees=copysign(atan2(hypot(nx,ny),fabs(nz))*180.0/3.141592653589793,nz);
        cut.gemcadDistance=distance;
        // Native GEM uses +Y at index zero; ASC solver uses +X.
        cut.index=atan2(nx,ny)/(2*3.141592653589793);
        char* comment=strchr(label,'\t');
        if(comment) {
            *comment++=0;
            if(design.tierComments.size()<=tier)design.tierComments.resize(tier+1);
            if(!design.tierComments[tier].length())design.tierComments[tier]=String(comment).substring(0,96);
        }
        snprintf(cut.name,sizeof(cut.name),"%s",label);
        design.cuts.push_back(cut);design.tierCount=max(design.tierCount,uint16_t(tier));
    }
    return GemSdResult::BAD_HEADER;
}
