#ifndef _WINDOWSYSTEM_H_
#define _WINDOWSYSTEM_H_

#include <webgl.h>

#include <iostream>
#include <vector>

#include <v8.h>
#include <nan.h>
#include <defines.h>

using namespace v8;

// class WebGLRenderingContext;
typedef unsigned int GLuint;
typedef int GLint;

namespace windowsystembase {

class LayerSpec {
public:
  int width;
  int height;
  GLuint msTex;
  GLuint msDepthTex;
  GLuint tex;
  GLuint depthTex;
  bool blit;
};

class ComposeSpec {
public:
  GLuint composeVao;
  GLuint composeReadFbo;
  GLuint composeWriteFbo;
  GLuint composeProgram;
  GLint positionLocation;
  GLint uvLocation;
  GLint colorTexLocation;
  GLint depthTexLocation;
  GLuint positionBuffer;
  GLuint uvBuffer;
  GLuint indexBuffer;
};

void InitializeLocalGlState(WebGLRenderingContext *gl);
void ComposeLayers(WebGLRenderingContext *gl, const std::vector<LayerSpec> &layers);
NAN_METHOD(CreateRenderTarget);
NAN_METHOD(ResizeRenderTarget);
NAN_METHOD(DestroyRenderTarget);
NAN_METHOD(ComposeLayers);
void Decorate(Local<Object> target);

}

// Local<Object> makeGlfw();
Local<Object> makeWindow();

#endif