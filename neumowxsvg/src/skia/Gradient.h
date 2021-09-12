#ifndef __GRADIENT_H__
#define __GRADIENT_H__


#include <vector>
#include "SkMatrix.h"
#include "SkColor.h"
#include "SkRefCnt.h"
class SkShader;
class Gradient
{
public:
    struct ColorStop
    {
        float offset;
        SkColor color;

        //ColorStop() : offset(0), alpha(0), red(0), green(0), blue(0) {}
        ColorStop(float o, SkColor col) : offset(o), color(col){}
    };
public:
    Gradient();
    Gradient(float x1, float y1, float x2, float y2);
    Gradient(float x1, float y1, float r1, float x2, float y2, float r2);
    Gradient(float x1, float y1, float r1, float x2, float y2, float r2, const float fMatrix[6]);
    virtual ~Gradient();
public:
    static Gradient* Create(float x1, float y1, float x2, float y2);
    static Gradient* Create(float x1, float y1, float r1, float x2, float y2, float r2, const float fMatrix[6]);
public:
    sk_sp<SkShader> GetShader();
public:
    void AddColorStop(float offset, const SkColor& color);
protected:
    void SortStops();
protected:
    bool m_radial;
    float m_x1;
    float m_y1;
    float m_x2;
    float m_y2;
    float m_r1;
    float m_r2;
    SkMatrix m_fMatrix;
    std::vector<ColorStop> m_stops;
    bool m_bStopsSorted;
    sk_sp<SkShader>   m_spShader;
};

#endif