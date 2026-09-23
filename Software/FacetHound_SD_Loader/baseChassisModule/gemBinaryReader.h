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
    uint8_t replay[40];size_t replayAt=0,replayEnd=0;
    uint32_t sourceCrc=0xffffffff;
    // A Stream may return a short read. Accumulate it, without treating an
    // empty metadata string as an I/O operation or retrying a failed read.
    auto bytes=[&](void* dst,size_t n) {
        auto* out=static_cast<uint8_t*>(dst);size_t done=0;
        while(done<n && replayAt<replayEnd)out[done++]=replay[replayAt++];
        while(done<n) {
            int got=file.read(out+done,n-done);
            if(got<=0 || size_t(got)>n-done) {
                Serial.print("@GEM_READ_ERROR,offset=");Serial.print(file.position());
                Serial.print(",wanted=");Serial.print(n-done);Serial.print(",got=");Serial.println(got);
                return false;
            }
            for(int i=0;i<got;++i) {
                sourceCrc^=out[done+size_t(i)];
                for(int bit=0;bit<8;++bit)sourceCrc=(sourceCrc>>1)^((sourceCrc&1)?0xedb88320UL:0);
            }
            done+=size_t(got);
        }
        return true;
    };
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
        // Inspect the footer discriminator once, then replay those bytes from
        // RAM if this is a polygon. Never rewind the SD file for every face.
        uint8_t header[40];if(!bytes(header,sizeof(header)))return GemSdResult::BAD_HEADER;
        // Avoid Arduino's word(...) macro, which replaces function calls.
        auto readLe32=[&](size_t p) {return uint32_t(header[p])|(uint32_t(header[p+1])<<8)|
            (uint32_t(header[p+2])<<16)|(uint32_t(header[p+3])<<24);};
        auto scalar=[&](size_t p) {uint64_t bits=uint64_t(readLe32(p))|(uint64_t(readLe32(p+4))<<32);
            double v;memcpy(&v,&bits,8);return v;};
        uint32_t zero=readLe32(0),fold=readLe32(8),mirror=readLe32(12),gear=readLe32(16);
        double ri=scalar(20),meridian=scalar(32);
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
            Serial.print("@GEM_CRC32,");Serial.println(sourceCrc^0xffffffff,HEX);
            for(auto& cut:design.cuts) {
                cut.index=fmod(cut.index*design.designIndexSign*design.wheelIndex+meridian,design.wheelIndex);
                if(cut.index<0)cut.index+=design.wheelIndex;
                double nearest=round(cut.index);
                if(fabs(cut.index-nearest)<1e-7)cut.index=nearest;
            }
            return GemSdResult::OK;
        }
        memcpy(replay,header,sizeof(header));replayAt=0;replayEnd=sizeof(header);
        double nx,ny,nz;uint32_t tier,marker;
        if(!real(nx)||!real(ny)||!real(nz)||!u32(tier))return GemSdResult::BAD_HEADER;
        if(!tier||tier>GEM_SD_MAX_CUTS) {
            Serial.print("@GEM_TIER_ERROR,value=");Serial.print(tier);
            Serial.print(",header=");
            for(uint8_t b:header) {if(b<16)Serial.print('0');Serial.print(b,HEX);}
            Serial.println();return GemSdResult::BAD_HEADER;
        }
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
