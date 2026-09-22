#pragma once
#include "sdGemLoader.h"
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

// Bounded streaming XML subset: quoted attributes, comments and XML declaration.
// No DTD/entity expansion; no dependence on line wrapping or attribute order.
namespace gcs {
inline bool decode(String& text) {
    String out;
    for(size_t i=0;i<text.length();++i) {
        if(text[i]!='&') {out+=text[i];continue;}
        int end=text.indexOf(';',i);if(end<0)return false;
        String entity=text.substring(i+1,end);uint32_t cp=0;
        if(entity=="amp")cp='&';else if(entity=="lt")cp='<';else if(entity=="gt")cp='>';
        else if(entity=="quot")cp='"';else if(entity=="apos")cp='\'';
        else if(entity.startsWith("#")) {
            bool hex=entity.startsWith("#x");char* tail=nullptr;
            const char* begin=entity.c_str()+(hex?2:1);
            cp=strtoul(begin,&tail,hex?16:10);
            if(tail==begin || *tail)return false;
        } else return false;
        if(!cp || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
        if(cp<128)out+=char(cp);
        else if(cp<2048) {out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
        else if(cp<65536) {out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        i=end;
    }
    text=out;return true;
}
struct Tag {
    String name;
    std::vector<String> keys,values;
    bool closing=false,empty=false;
    bool parse(char* p) {
        if(*p=='/') {closing=true;++p;}
        while(*p && !isspace(*p) && *p!='/')name+=*p++;
        if(!name.length())return false;
        while(*p) {
            while(isspace(*p))++p;
            if(!*p)break;
            if(*p=='/' && !p[1]) {empty=true;break;}
            if(closing || keys.size()>=32)return false;
            String key,value;
            while(*p && !isspace(*p) && *p!='=')key+=*p++;
            while(isspace(*p))++p;
            if(!key.length() || *p++!='=')return false;
            while(isspace(*p))++p;
            char quote=*p++;if(quote!='\'' && quote!='"')return false;
            while(*p && *p!=quote)value+=*p++;
            if(*p++!=quote || !decode(value))return false;
            for(const auto& old:keys)if(old==key)return false;
            keys.push_back(key);values.push_back(value);
        }
        return !(closing && empty);
    }
    const char* get(const char* key) const {
        for(size_t i=0;i<keys.size();++i)if(keys[i]==key)return values[i].c_str();
        return nullptr;
    }
    bool number(const char* key,double& value) const {
        const char* p=get(key);if(!p || !*p)return false;
        char* tail;value=strtod(p,&tail);
        return tail!=p && !*tail && isfinite(value);
    }
};
}

template<class Stream> GemSdResult readGcs(Stream& file,GemSdDesign& design) {
    if(file.size()>2*1024*1024)return GemSdResult::BAD_HEADER;
    String stack[16];unsigned level=0;bool root=false,done=false,gear=false,skip=false;
    bool inTier=false,inFacet=false,hasDistance=false,tierHasDistance=false;
    double tierDistance=0;
    double tierAngle=0,distance=0,nx=0,ny=0,nz=0,indexAngle=0;
    uint16_t tier=0,facet=0;unsigned vertices=0;
    String name,comment;
    auto finishFacet=[&]() -> bool {
        if(skip) {inFacet=false;return true;}
        if(!hasDistance || distance<=0 || !isfinite(distance) || design.cuts.size()>=GEM_SD_MAX_CUTS)return false;
        GemCutCoordinate cut;
        cut.tier=tier;cut.facet=++facet;cut.gemcadDistance=distance;
        cut.angleDegrees=copysign(atan2(hypot(nx,ny),fabs(nz))*180/M_PI,nz);
        double az=hypot(nx,ny)>1e-10?atan2(nx,-ny):indexAngle*M_PI/180;
        cut.index=fmod(az*design.wheelIndex/(2*M_PI)+design.wheelIndex,design.wheelIndex);
        if(fabs(cut.index-round(cut.index))<1e-7)cut.index=fmod(round(cut.index),design.wheelIndex);
        snprintf(cut.name,sizeof(cut.name),"%s",name.c_str());
        design.cuts.push_back(cut);inFacet=false;return true;
    };
    char token[4096];
    while(file.available()) {
        int ch=file.read();
        if(ch!='<') {if(!isspace(ch) && ch!=0xef && ch!=0xbb && ch!=0xbf)return GemSdResult::BAD_HEADER;continue;}
        size_t n=0;char quote=0;bool ended=false,commentTag=false;
        while(file.available()) {
            ch=file.read();
            if(n==3 && !strncmp(token,"!--",3))commentTag=true;
            if(commentTag) {
                if(ch=='>' && n>=2 && token[n-1]=='-' && token[n-2]=='-') {ended=true;break;}
            } else {
                if(ch==quote)quote=0;else if(!quote && (ch=='\'' || ch=='"'))quote=ch;
                else if(ch=='>' && !quote) {ended=true;break;}
            }
            if(n+1>=sizeof(token))return GemSdResult::LINE_TOO_LONG;
            token[n++]=char(ch);
        }
        if(!ended)return GemSdResult::BAD_HEADER;
        token[n]=0;
        if(commentTag)continue;
        if(token[0]=='?') {if(root || strncmp(token,"?xml ",5))return GemSdResult::BAD_HEADER;continue;}
        gcs::Tag t;if(!t.parse(token))return GemSdResult::BAD_HEADER;
        if(t.closing) {
            if(!level || stack[level-1]!=t.name)return GemSdResult::BAD_HEADER;
            if(t.name=="facet" && (!inFacet || !finishFacet()))return GemSdResult::BAD_NUMBER;
            if(t.name=="tier") {
                if(!skip && !facet)return GemSdResult::NO_CUTS;
                inTier=false;
            }
            if(t.name=="GemCutStudio")done=true;
            --level;continue;
        }
        if(done)return GemSdResult::BAD_HEADER;
        String parent=level?stack[level-1]:String();
        if(!root) {
            double version;
            if(t.name!="GemCutStudio" || !t.number("version",version) || version!=1000 || t.empty)return GemSdResult::UNSUPPORTED_FILE;
            root=true;snprintf(design.formatVersion,sizeof(design.formatVersion),"GCS 1000");
        } else if(!level)return GemSdResult::BAD_HEADER;
        if(t.name=="index" && parent=="GemCutStudio") {
            if(gear || !t.number("gear",design.wheelIndex) || design.wheelIndex<1 || design.wheelIndex>400)return GemSdResult::BAD_NUMBER;
            gear=true; // base/symmetry/mirror are editor state, not design data.
        } else if(t.name=="tier") {
            if(parent!="GemCutStudio" || !gear || inTier || !t.number("angle",tierAngle) || tierAngle<0 || tierAngle>180)return GemSdResult::BAD_HEADER;
            skip=t.get("guide") && !strcasecmp(t.get("guide"),"true");
            inTier=true;facet=0;
            if(!skip) {
                if(++tier>GEM_SD_MAX_CUTS)return GemSdResult::TOO_MANY_CUTS;
                design.tierCount=tier;
                name=t.get("name")?t.get("name"):"";
                comment=t.get("instructions")?t.get("instructions"):"";
                design.tierComments.resize(tier+1);design.tierComments[tier]=comment.substring(0,96);
            }
            tierHasDistance=t.number("depth",tierDistance);
            if(t.get("depth") && !tierHasDistance)return GemSdResult::BAD_NUMBER;
            // Remember optional tier distance independently of per-face derivation.
        } else if(t.name=="facet") {
            if(parent!="tier" || !inTier || inFacet)return GemSdResult::BAD_HEADER;
            inFacet=true;vertices=0;indexAngle=0;
            hasDistance=tierHasDistance;distance=tierDistance;
            if(!skip) {
                bool hasIndex=t.number("index_angle",indexAngle);
                if(t.get("index_angle") && !hasIndex)return GemSdResult::BAD_NUMBER;
                if(t.get("nx") || t.get("ny") || t.get("nz")) {
                    if(!t.number("nx",nx)||!t.number("ny",ny)||!t.number("nz",nz))return GemSdResult::BAD_NUMBER;
                } else {
                    if(!hasIndex)return GemSdResult::BAD_NUMBER;
                    double a=tierAngle*M_PI/180,b=indexAngle*M_PI/180;
                    nx=sin(a)*sin(b)*(tierAngle<90?1:-1);ny=-sin(a)*cos(b);nz=cos(a);
                }
                double length=sqrt(nx*nx+ny*ny+nz*nz);
                if(!isfinite(length)||length<1e-12)return GemSdResult::BAD_NUMBER;
                nx/=length;ny/=length;nz/=length;
            }
        } else if(t.name=="vertex") {
            if(parent!="facet" || !inFacet)return GemSdResult::BAD_HEADER;
            if(!skip) {
                double x,y,z;
                if(++vertices>4096 || !t.number("x",x)||!t.number("y",y)||!t.number("z",z))return GemSdResult::BAD_NUMBER;
                double d=nx*x+ny*y+nz*z;
                if(!isfinite(d))return GemSdResult::BAD_NUMBER;
                if(!hasDistance) {distance=d;hasDistance=true;}
                else if(fabs(d-distance)>1e-6*fmax(1.0,fabs(distance)))return GemSdResult::BAD_NUMBER;
            }
        } else if(t.name=="info" && parent=="GemCutStudio") {
            if(t.get("title"))snprintf(design.title,sizeof(design.title),"%s",t.get("title"));
            if(t.get("author"))snprintf(design.attribution,sizeof(design.attribution),"%s",t.get("author"));
        } else if(t.name=="render" && parent=="GemCutStudio") {
            if(t.get("refractive_index") && !t.number("refractive_index",design.refractiveIndex))return GemSdResult::BAD_NUMBER;
        }
        if(t.empty) {
            if(t.name=="facet" && !finishFacet())return GemSdResult::BAD_NUMBER;
            if(t.name=="tier") {
                if(!skip)return GemSdResult::NO_CUTS;
                inTier=false;
            }
        } else {
            if(level>=16)return GemSdResult::BAD_HEADER;
            stack[level++]=t.name;
        }
    }
    return done && !level && gear && !design.cuts.empty()?GemSdResult::OK:GemSdResult::BAD_HEADER;
}
