#pragma once
#include <math.h>

// Signed-zero/negative-distance culets belong to the pavilion, not the table.
inline bool transferPavilion(float angle, float distance=0.0f)
{
    return angle<0 || angle>90 ||
           (fabsf(angle)<0.000001f && (signbit(angle) || distance<0));
}

inline float wrapTransferTurns(float value)
{
    value=fmodf(value+0.5f,1.0f);
    if(value<0)value+=1;
    return value-0.5f;
}
