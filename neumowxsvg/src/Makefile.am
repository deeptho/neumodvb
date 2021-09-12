
if USE_RENDER_SKIA
RENDER_DIR = skia
RENDER_LIB = skia/libwxsvg_skia.la $(SKIA_LIBS)
else
RENDER_DIR = cairo
RENDER_LIB = cairo/libwxsvg_cairo.la $(CAIRO_LIBS)
endif

SUBDIRS = svgxml $(RENDER_DIR)

if USE_LIBAV
LIBAV_SRC = mediadec_ffmpeg.cpp
endif

lib_LTLIBRARIES = libwxsvg.la
libwxsvg_la_SOURCES = SVGDocument.cpp svgctrl.cpp imagsvg.cpp ExifHandler.cpp\
  SVGAnimatedType.cpp SVGAnimationElement.cpp SVGAnimateMotionElement.cpp SVGAnimateTransformElement.cpp\
  SVGCircleElement.cpp SVGEllipseElement.cpp SVGFEGaussianBlurElement.cpp SVGFilterElement.cpp\
  SVGImageElement.cpp SVGLineElement.cpp SVGLocatable.cpp SVGMarkerElement.cpp SVGPathElement.cpp\
  SVGPolylineElement.cpp SVGPolygonElement.cpp SVGRectElement.cpp SVGRadialGradientElement.cpp\
  SVGSVGElement.cpp SVGTextContentElement.cpp SVGTextElement.cpp\
  SVGUseElement.cpp SVGVideoElement.cpp\
  SVGAngle.cpp SVGColor.cpp GetSVGDocument.cpp SVGLength.cpp\
  SVGMatrix.cpp SVGPaint.cpp SVGPoint.cpp SVGPreserveAspectRatio.cpp\
  SVGRect.cpp SVGTests.cpp SVGTransform.cpp\
  SVGFitToViewBox.cpp SVGStylable.cpp SVGTransformable.cpp\
  SVGElementInstanceList.cpp SVGLengthList.cpp SVGNumberList.cpp\
  SVGPathSegList.cpp SVGPointList.cpp SVGStringList.cpp SVGTransformList.cpp\
  Elements_CopyConstructors.cpp Elements_HasAttribute.cpp\
  Elements_GetAttribute.cpp Elements_GetAttributes.cpp Elements_SetAttribute.cpp\
  CSSValue.cpp CSSStyleDeclaration.cpp\
  SVGCanvas.cpp SVGCanvasItem.cpp NodeList.cpp $(LIBAV_SRC)

libwxsvg_la_LIBADD = svgxml/libwxsvgxml.la $(RENDER_LIB)
libwxsvg_la_LDFLAGS = -no-undefined -version-info @LIBRARY_VERSION@

AM_CPPFLAGS = -fPIC -I$(top_srcdir)/include -I$(top_srcdir)/include/wxSVG
