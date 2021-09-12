#include "Gradient.h"

#include "SkGradientShader.h"
#include "SkShader.h"
#include "SkAutoMalloc.h"
#include <algorithm>

Gradient::Gradient(float x1, float y1, float x2, float y2) :
    m_radial(false),
    m_x1(x1),
    m_y1(y1),
    m_x2(x2),
    m_y2(y2),
    m_r1(0),
    m_r2(0),
    m_bStopsSorted(false)
{
    m_fMatrix.reset();
}

Gradient::Gradient(float x1, float y1, float r1, float x2, float y2, float r2) :
    m_radial(true),
    m_x1(x1),
    m_y1(y1),
    m_x2(x2),
    m_y2(y2),
    m_r1(r1),
    m_r2(r2),
    m_bStopsSorted(false)
{
    m_fMatrix.reset();
}

Gradient::Gradient(float x1, float y1, float r1, float x2, float y2, float r2, const float fMatrix[6]) :
    m_radial(true),
    m_x1(x1),
    m_y1(y1),
    m_x2(x2),
    m_y2(y2),
    m_r1(r1),
    m_r2(r2),
    m_bStopsSorted(false)
{
    m_fMatrix.setAll(fMatrix[0], fMatrix[2], fMatrix[4], fMatrix[1], fMatrix[3], fMatrix[5], 0, 0, 1);
}

Gradient::~Gradient()
{
}

Gradient* Gradient::Create(float x1, float y1, float x2, float y2)
{
    return new Gradient(x1, y1, x2, y2);
}

Gradient* Gradient::Create(float x1, float y1, float r1, float x2, float y2, float r2, const float fMatrix[6])
{
    return new Gradient(x1, y1, r1, x2, y2, r2, fMatrix);
}

sk_sp<SkShader> Gradient::GetShader()
{
    if (m_spShader)
        return nullptr;

    SortStops();
    size_t count = m_stops.size();
    std::vector<ColorStop>::iterator iterStop = m_stops.begin();
    std::vector<ColorStop>::reverse_iterator riterStop = m_stops.rbegin();
    switch (count)
    {
    case 0:
        count = 2;
        break;
    default:
        if (iterStop->offset > 0.0)
            count++;
        if (riterStop->offset < 1.0)
            count++;
        break;
    }

    SkAutoMalloc data((sizeof(SkColor) + sizeof(SkScalar)) * count);
    SkColor* colors = (SkColor*)data.get();
    SkScalar* pos = (SkScalar*)(colors + count);

    size_t start = 0;
    size_t n = m_stops.size();
    if (n == 0)
    {
        pos[0] = SkFloatToScalar(0.0);
        colors[0] = SkColorSetARGB(0, 0, 0, 0);
        start = 1;
    }
    else if (iterStop->offset > 0.0)
    {
        pos[0] = SkFloatToScalar(0.0);
        colors[0] = iterStop->color;
        start = 1;
    }

    for (size_t i = start; i < start + n; i++)
    {
        pos[i] = SkFloatToScalar(iterStop->offset);
        colors[i] = iterStop->color;
        ++iterStop;
    }

    if (n == 0 || riterStop->offset < 1.0)
    {
        pos[count - 1] = SkFloatToScalar(1.0);
        colors[count - 1] = colors[count - 2];
    }
    
    SkShader::TileMode mode = SkShader::kClamp_TileMode;

    SkPoint p1, p2;

    p1.set(SkFloatToScalar(m_x1), SkFloatToScalar(m_y1));
    p2.set(SkFloatToScalar(m_x2), SkFloatToScalar(m_y2));

    if (m_radial)
    {
        /*if (m_x1 == m_x2 && m_y1 == m_y2 && m_r1 <= 0.0f)
        {
            SkScalar r1 = m_r2 >= 0.0f ? SkFloatToScalar(m_r2) : 0;
            m_spShader = SkGradientShader::MakeRadial(p1, r1, colors, pos, static_cast<int>(count), mode, NULL, &m_fMatrix);
        }
        else*/
        {
            SkScalar r1 = m_r1 >= 0.0f ? SkFloatToScalar(m_r1) : 0;
            SkScalar r2 = m_r2 >= 0.0f ? SkFloatToScalar(m_r2) : 0;
            if (r1 != r2)
            {
                m_spShader = SkGradientShader::MakeTwoPointConical(p1, r1, p2, r2, colors, pos, static_cast<int>(count), mode, NULL, &m_fMatrix);
                /*paint.setShader(SkGradientShader::MakeTwoPointConical(
                    SkPoint::Make(100, 100), 0, SkPoint::Make(100, 100), 100, colors, nullptr, 2,
                    SkShader::kClamp_TileMode, 0, &matrix));*/
            }
        }
    }
    else
    {
        SkPoint pts[2] = {p1, p2};
        m_spShader = SkGradientShader::MakeLinear(pts, colors, pos, static_cast<int>(count), mode, 0, nullptr);
    }

    return m_spShader;
}

void Gradient::AddColorStop(float offset, const SkColor& color)
{
    if (offset >= 0.0 && offset <= 1.0)
    {
        m_stops.push_back(ColorStop(offset, color));
        m_bStopsSorted = false;
        //SkSafeUnref(m_pShader);
        //m_pShader = nullptr;
    }
}

static inline bool compareStops(const Gradient::ColorStop& a, const Gradient::ColorStop& b)
{
    return a.offset < b.offset;
}

void Gradient::SortStops()
{
    if (m_bStopsSorted)
        return;
    if (!m_stops.size())
        return;
    stable_sort(m_stops.begin(), m_stops.end(), compareStops);
}
