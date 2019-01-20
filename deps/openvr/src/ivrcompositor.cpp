#include <ivrcompositor.h>

#include <array>
#include <node.h>
#include <openvr.h>
#include <ivrsystem.h>

using namespace v8;

using TrackedDevicePoseArray = std::array<vr::TrackedDevicePose_t, vr::k_unMaxTrackedDeviceCount>;

namespace vr {
  uv_sem_t reqSem;
  uv_sem_t resSem;
  uv_async_t resAsync;
  std::mutex reqMutex;
  std::mutex resMutex;
  std::deque<std::function<void()>> reqCbs;
  std::deque<VRPoseRes *> resCbs;
  std::thread reqThead;
};

VRPoseRes::VRPoseRes(Local<Function> cb) : cb(cb);

VRPoseRes::~VRPoseRes() {}

void RunResInMainThread(uv_async_t *handle) {
  Nan::HandleScope scope;

  PoseRes *vrPoseRes;
  {
    std::lock_guard<std::mutex> lock(reqMutex);

    vrPoseRes = resCbs.front();
    resCbs.pop_front();
  }

  {
    Local<Object> asyncObject = Nan::New<Object>();
    AsyncResource asyncResource(Isolate::GetCurrent(), asyncObject, "RunResInMainThread");

    Local<Function> cb = Nan::New(vrPoseRes->cb);
    asyncResource.MakeCallback(cb, 0, nullptr);
  }

  delete vrPoseRes;
}

//=============================================================================
NAN_MODULE_INIT(IVRCompositor::Init)
{
  // Create a function template that is called in JS to create this wrapper.
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);

  // Declare human-readable name for this wrapper.
  tpl->SetClassName(Nan::New("IVRCompositor").ToLocalChecked());

  // Declare the stored number of fields (just the wrapped C++ object).
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  // Assign all the wrapped methods of this object.
  // Nan::SetPrototypeMethod(tpl, "WaitGetPoses", WaitGetPoses);
  Nan::SetPrototypeMethod(tpl, "RequestGetPoses", RequestGetPoses);
  Nan::SetPrototypeMethod(tpl, "Submit", Submit);

  // Set a static constructor function to reference the `New` function template.
  constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());

  uv_sem_init(&reqSem, 0);
  uv_sem_init(&resSem, 0);
  uv_async_init(uv_default_loop(), &resAsync, RunResInMainThread);
  reqThead = std::thread([]() -> void {
    for (;;) {
      uv_sem_wait(&reqSem);

      std::function<void()> reqCb;
      {
        std::lock_guard<std::mutex> lock(reqMutex);

        if (reqCbs.size() > 0) {
          reqCb = reqCbs.front();
          reqCbs.pop_front();
        }
      }
      if (reqFn) {
        reqFn();
      } else {
        break;
      }
    }
  });
}

//=============================================================================
Local<Object> IVRCompositor::NewInstance(vr::IVRCompositor *compositor)
{
  Nan::EscapableHandleScope scope;
  Local<Function> cons = Nan::New(constructor());
  Local<Value> argv[1] = { Nan::New<External>(compositor) };
  return scope.Escape(Nan::NewInstance(cons, 1, argv).ToLocalChecked());
}

//=============================================================================
IVRCompositor::IVRCompositor(vr::IVRCompositor *self)
: self_(self)
{
  // Do nothing.
}

//=============================================================================
NAN_METHOD(IVRCompositor::New)
{
  if (!info.IsConstructCall())
  {
    Nan::ThrowError("Use the `new` keyword when creating a new instance.");
    return;
  }

  if (info.Length() != 1 || !info[0]->IsExternal())
  {
    Nan::ThrowTypeError("Argument[0] must be an `IVRCompositor*`.");
    return;
  }

  auto wrapped_instance = static_cast<vr::IVRCompositor*>(
    Local<External>::Cast(info[0])->Value());
  IVRCompositor *obj = new IVRCompositor(wrapped_instance);
  obj->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
}

NAN_METHOD(IVRCompositor::WaitGetPoses)
{
  IVRCompositor* obj = ObjectWrap::Unwrap<IVRCompositor>(info.Holder());

  if (info.Length() != 4)
  {
    Nan::ThrowError("Wrong number of arguments.");
    return;
  }

  TrackedDevicePoseArray trackedDevicePoseArray;
	obj->self_->WaitGetPoses(trackedDevicePoseArray.data(), static_cast<uint32_t>(trackedDevicePoseArray.size()), nullptr, 0);

  IVRSystem* system = IVRSystem::Unwrap<IVRSystem>(Local<Object>::Cast(info[0]));
  Local<Float32Array> hmdFloat32Array = Local<Float32Array>::Cast(info[1]);
  Local<Float32Array> leftControllerFloat32Array = Local<Float32Array>::Cast(info[2]);
  Local<Float32Array> rightControllerFloat32Array = Local<Float32Array>::Cast(info[3]);
  float *hmdArray = (float *)((char *)hmdFloat32Array->GetContents().Data() + hmdFloat32Array->ByteOffset());
  float *leftController = (float *)((char *)leftControllerFloat32Array->GetContents().Data() + leftControllerFloat32Array->ByteOffset());
  float *rightController = (float *)((char *)rightControllerFloat32Array->GetContents().Data() + rightControllerFloat32Array->ByteOffset());

  memset(hmdArray, std::numeric_limits<float>::quiet_NaN(), 16);
  memset(leftController, std::numeric_limits<float>::quiet_NaN(), 16);
  memset(rightController, std::numeric_limits<float>::quiet_NaN(), 16);

  for (unsigned int i = 0; i < trackedDevicePoseArray.size(); i++) {
    const vr::TrackedDevicePose_t &trackedDevicePose = trackedDevicePoseArray[i];
    if (trackedDevicePose.bPoseIsValid) {
      const vr::ETrackedDeviceClass deviceClass = system->self_->GetTrackedDeviceClass(i);
      if (deviceClass == vr::TrackedDeviceClass_HMD) {
        const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

        for (unsigned int v = 0; v < 4; v++) {
          for (unsigned int u = 0; u < 3; u++) {
            hmdFloat32Array->Set(v * 4 + u, Number::New(Isolate::GetCurrent(), matrix.m[u][v]));
          }
        }
        hmdFloat32Array[0 * 4 + 3] = 0;
        hmdFloat32Array[1 * 4 + 3] = 0;
        hmdFloat32Array[2 * 4 + 3] = 0;
        hmdFloat32Array[3 * 4 + 3] = 1;
      } else if (deviceClass == vr::TrackedDeviceClass_Controller) {
        const vr::ETrackedControllerRole controllerRole = system->self_->GetControllerRoleForTrackedDeviceIndex(i);
        if (controllerRole == vr::TrackedControllerRole_LeftHand) {
          const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

          for (unsigned int v = 0; v < 4; v++) {
            for (unsigned int u = 0; u < 3; u++) {
              leftControllerFloat32Array[v * 4 + u] = matrix.m[u][v];
            }
          }
          leftControllerFloat32Array[0 * 4 + 3] = 0;
          leftControllerFloat32Array[1 * 4 + 3] = 0;
          leftControllerFloat32Array[2 * 4 + 3] = 0;
          leftControllerFloat32Array[3 * 4 + 3] = 1;
        } else if (controllerRole == vr::TrackedControllerRole_RightHand) {
          const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

          for (unsigned int v = 0; v < 4; v++) {
            for (unsigned int u = 0; u < 3; u++) {
              rightControllerFloat32Array[v * 4 + u] = matrix.m[u][v];
            }
          }
          rightControllerFloat32Array[0 * 4 + 3] = 0;
          rightControllerFloat32Array[1 * 4 + 3] = 0;
          rightControllerFloat32Array[2 * 4 + 3] = 0;
          rightControllerFloat32Array[3 * 4 + 3] = 1;
        }
      }
    }
  }
}

NAN_METHOD(IVRCompositor::RequestGetPoses) {
  IVRCompositor* obj = ObjectWrap::Unwrap<IVRCompositor>(info.Holder());

  if (info.Length() != 5)
  {
    Nan::ThrowError("Wrong number of arguments.");
    return;
  }

  TrackedDevicePoseArray trackedDevicePoseArray;
	obj->self_->WaitGetPoses(trackedDevicePoseArray.data(), static_cast<uint32_t>(trackedDevicePoseArray.size()), nullptr, 0);

  IVRSystem* system = IVRSystem::Unwrap<IVRSystem>(Local<Object>::Cast(info[0]));
  Local<Float32Array> hmdFloat32Array = Local<Float32Array>::Cast(info[1]);
  Local<Float32Array> leftControllerFloat32Array = Local<Float32Array>::Cast(info[2]);
  Local<Float32Array> rightControllerFloat32Array = Local<Float32Array>::Cast(info[3]);
  Local<Function> cbFn = Local<Function>::Cast(info[4]);

  float *hmdArray = (float *)((char *)hmdFloat32Array->GetContents().Data() + hmdFloat32Array->ByteOffset());
  float *leftController = (float *)((char *)leftControllerFloat32Array->GetContents().Data() + leftControllerFloat32Array->ByteOffset());
  float *rightController = (float *)((char *)rightControllerFloat32Array->GetContents().Data() + rightControllerFloat32Array->ByteOffset());

  {
    std::lock_guard<std::mutex> lock(reqMutex);

    reqCbs.push_back([hmdArray, leftController, rightController]() -> void {
      memset(hmdArray, std::numeric_limits<float>::quiet_NaN(), 16);
      memset(leftController, std::numeric_limits<float>::quiet_NaN(), 16);
      memset(rightController, std::numeric_limits<float>::quiet_NaN(), 16);

      for (unsigned int i = 0; i < trackedDevicePoseArray.size(); i++) {
        const vr::TrackedDevicePose_t &trackedDevicePose = trackedDevicePoseArray[i];
        if (trackedDevicePose.bPoseIsValid) {
          const vr::ETrackedDeviceClass deviceClass = system->self_->GetTrackedDeviceClass(i);
          if (deviceClass == vr::TrackedDeviceClass_HMD) {
            const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

            for (unsigned int v = 0; v < 4; v++) {
              for (unsigned int u = 0; u < 3; u++) {
                hmdFloat32Array->Set(v * 4 + u, Number::New(Isolate::GetCurrent(), matrix.m[u][v]));
              }
            }
            hmdFloat32Array[0 * 4 + 3] = 0;
            hmdFloat32Array[1 * 4 + 3] = 0;
            hmdFloat32Array[2 * 4 + 3] = 0;
            hmdFloat32Array[3 * 4 + 3] = 1;
          } else if (deviceClass == vr::TrackedDeviceClass_Controller) {
            const vr::ETrackedControllerRole controllerRole = system->self_->GetControllerRoleForTrackedDeviceIndex(i);
            if (controllerRole == vr::TrackedControllerRole_LeftHand) {
              const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

              for (unsigned int v = 0; v < 4; v++) {
                for (unsigned int u = 0; u < 3; u++) {
                  leftControllerFloat32Array[v * 4 + u] = matrix.m[u][v];
                }
              }
              leftControllerFloat32Array[0 * 4 + 3] = 0;
              leftControllerFloat32Array[1 * 4 + 3] = 0;
              leftControllerFloat32Array[2 * 4 + 3] = 0;
              leftControllerFloat32Array[3 * 4 + 3] = 1;
            } else if (controllerRole == vr::TrackedControllerRole_RightHand) {
              const vr::HmdMatrix34_t &matrix = trackedDevicePose.mDeviceToAbsoluteTracking;

              for (unsigned int v = 0; v < 4; v++) {
                for (unsigned int u = 0; u < 3; u++) {
                  rightControllerFloat32Array[v * 4 + u] = matrix.m[u][v];
                }
              }
              rightControllerFloat32Array[0 * 4 + 3] = 0;
              rightControllerFloat32Array[1 * 4 + 3] = 0;
              rightControllerFloat32Array[2 * 4 + 3] = 0;
              rightControllerFloat32Array[3 * 4 + 3] = 1;
            }
          }
        }
      }

      uv_async_send(&resAsync);
    });
  }

  VRPoseRes *vrPoseRes = new VRPoseRes(cbFn);
  {
    std::lock_guard<std::mutex> lock(resMutex);

    resCbs.push_back(vrPoseRes);
  }

  uv_sem_post(&reqSem);
}

NAN_METHOD(IVRCompositor::Submit)
{
  IVRCompositor* obj = ObjectWrap::Unwrap<IVRCompositor>(info.Holder());

  if (info.Length() != 2)
  {
    Nan::ThrowError("Wrong number of arguments.");
    return;
  }

  if (!(info[0]->IsObject() && info[1]->IsNumber()))
  {
    Nan::ThrowError("Expected arguments (object, number).");
    return;
  }

  WebGLRenderingContext *gl = node::ObjectWrap::Unwrap<WebGLRenderingContext>(Local<Object>::Cast(info[0]));
  GLuint texture = info[1]->Uint32Value();

  vr::EColorSpace colorSpace = vr::ColorSpace_Gamma;

  vr::Texture_t leftEyeTexture = {(void *)(size_t)texture, vr::TextureType_OpenGL, colorSpace};
  vr::VRTextureBounds_t leftEyeTextureBounds = {
    0, 0,
    0.5, 1,
  };
  vr::EVRCompositorError compositorError = obj->self_->Submit(vr::Eye_Left, &leftEyeTexture, &leftEyeTextureBounds);
  if (compositorError != vr::VRCompositorError_None) {
    if (compositorError == vr::VRCompositorError_RequestFailed) Nan::ThrowError("Compositor error: VRCompositorError_RequestFailed");
    else if (compositorError == vr::VRCompositorError_IncompatibleVersion) Nan::ThrowError("Compositor error: VRCompositorError_IncompatibleVersion");
    else if (compositorError == vr::VRCompositorError_DoNotHaveFocus) {} // Nan::ThrowError("Compositor error: VRCompositorError_DoNotHaveFocus");
    else if (compositorError == vr::VRCompositorError_InvalidTexture) Nan::ThrowError("Compositor error: VRCompositorError_InvalidTexture");
    else if (compositorError == vr::VRCompositorError_IsNotSceneApplication) Nan::ThrowError("Compositor error: VRCompositorError_IsNotSceneApplication");
    else if (compositorError == vr::VRCompositorError_TextureIsOnWrongDevice) Nan::ThrowError("Compositor error: VRCompositorError_TextureIsOnWrongDevice");
    else if (compositorError == vr::VRCompositorError_TextureUsesUnsupportedFormat) Nan::ThrowError("Compositor error: VRCompositorError_TextureUsesUnsupportedFormat");
    else if (compositorError == vr::VRCompositorError_SharedTexturesNotSupported) Nan::ThrowError("Compositor error: VRCompositorError_SharedTexturesNotSupported");
    else if (compositorError == vr::VRCompositorError_IndexOutOfRange) Nan::ThrowError("Compositor error: VRCompositorError_IndexOutOfRange");
    else if (compositorError == vr::VRCompositorError_AlreadySubmitted) Nan::ThrowError("Compositor error: VRCompositorError_AlreadySubmitted");
    else if (compositorError == vr::VRCompositorError_InvalidBounds) Nan::ThrowError("Compositor error: VRCompositorError_InvalidBounds");
    else Nan::ThrowError("Compositor error: unknown");
    return;
  }

  vr::Texture_t rightEyeTexture = {(void *)(size_t)texture, vr::TextureType_OpenGL, colorSpace};
  vr::VRTextureBounds_t rightEyeTextureBounds = {
    0.5, 0,
    1, 1,
  };
  compositorError = obj->self_->Submit(vr::Eye_Right, &rightEyeTexture, &rightEyeTextureBounds);
  if (compositorError != vr::VRCompositorError_None) {
    if (compositorError == vr::VRCompositorError_RequestFailed) Nan::ThrowError("Compositor error: VRCompositorError_RequestFailed");
    else if (compositorError == vr::VRCompositorError_IncompatibleVersion) Nan::ThrowError("Compositor error: VRCompositorError_IncompatibleVersion");
    else if (compositorError == vr::VRCompositorError_DoNotHaveFocus) {} // Nan::ThrowError("Compositor error: VRCompositorError_DoNotHaveFocus");
    else if (compositorError == vr::VRCompositorError_InvalidTexture) Nan::ThrowError("Compositor error: VRCompositorError_InvalidTexture");
    else if (compositorError == vr::VRCompositorError_IsNotSceneApplication) Nan::ThrowError("Compositor error: VRCompositorError_IsNotSceneApplication");
    else if (compositorError == vr::VRCompositorError_TextureIsOnWrongDevice) Nan::ThrowError("Compositor error: VRCompositorError_TextureIsOnWrongDevice");
    else if (compositorError == vr::VRCompositorError_TextureUsesUnsupportedFormat) Nan::ThrowError("Compositor error: VRCompositorError_TextureUsesUnsupportedFormat");
    else if (compositorError == vr::VRCompositorError_SharedTexturesNotSupported) Nan::ThrowError("Compositor error: VRCompositorError_SharedTexturesNotSupported");
    else if (compositorError == vr::VRCompositorError_IndexOutOfRange) Nan::ThrowError("Compositor error: VRCompositorError_IndexOutOfRange");
    else if (compositorError == vr::VRCompositorError_AlreadySubmitted) Nan::ThrowError("Compositor error: VRCompositorError_AlreadySubmitted");
    else if (compositorError == vr::VRCompositorError_InvalidBounds) Nan::ThrowError("Compositor error: VRCompositorError_InvalidBounds");
    else Nan::ThrowError("Compositor error: unknown");
    return;
  }

  obj->self_->PostPresentHandoff();

  if (gl->HasTextureBinding(gl->activeTexture, GL_TEXTURE_2D)) {
    glBindTexture(GL_TEXTURE_2D, gl->GetTextureBinding(gl->activeTexture, GL_TEXTURE_2D));
  } else {
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  if (gl->HasTextureBinding(gl->activeTexture, GL_TEXTURE_2D_MULTISAMPLE)) {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, gl->GetTextureBinding(gl->activeTexture, GL_TEXTURE_2D_MULTISAMPLE));
  } else {
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
  }
  if (gl->HasTextureBinding(gl->activeTexture, GL_TEXTURE_CUBE_MAP)) {
    glBindTexture(GL_TEXTURE_CUBE_MAP, gl->GetTextureBinding(gl->activeTexture, GL_TEXTURE_CUBE_MAP));
  } else {
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
  }
}

NAN_METHOD(NewCompositor) {
  if (info.Length() != 0)
  {
    Nan::ThrowError("Wrong number of arguments.");
    return;
  }

  // Perform the actual wrapped call.
  vr::IVRCompositor *compositor = vr::VRCompositor();
  if (!compositor)
  {
    Nan::ThrowError("Unable to initialize VR compositor.");
    return;
  }

  // Wrap the resulting system in the correct wrapper and return it.
  auto result = IVRCompositor::NewInstance(compositor);
  info.GetReturnValue().Set(result);
}
