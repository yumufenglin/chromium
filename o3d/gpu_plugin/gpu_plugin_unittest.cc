// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "o3d/gpu_plugin/gpu_plugin.h"
#include "o3d/gpu_plugin/gpu_plugin_object.h"
#include "o3d/gpu_plugin/plugin_object_factory_mock.h"
#include "o3d/gpu_plugin/plugin_object_mock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/plugins/nphostapi.h"

#if defined(OS_LINUX)
#define INITIALIZE_PLUGIN_FUNCS , &plugin_funcs_
#else
#define INITIALIZE_PLUGIN_FUNCS
#endif

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgumentPointee;
using testing::StrictMock;

namespace o3d {
namespace gpu_plugin {

class GPUPluginTest : public testing::Test {
 protected:
  virtual void SetUp() {
    memset(&npp_, 0, sizeof(npp_));
    memset(&browser_funcs_, 0, sizeof(browser_funcs_));
    memset(&plugin_funcs_, 0, sizeof(plugin_funcs_));
    plugin_object_factory_ = new StrictMock<MockPluginObjectFactory>;
    previous_plugin_object_factory_ = SetPluginObjectFactory(
        plugin_object_factory_);
  }

  virtual void TearDown() {
    SetPluginObjectFactory(previous_plugin_object_factory_);
    delete plugin_object_factory_;
  }

  NPP_t npp_;
  NPNetscapeFuncs browser_funcs_;
  NPPluginFuncs plugin_funcs_;
  MockPluginObjectFactory* plugin_object_factory_;
  PluginObjectFactory* previous_plugin_object_factory_;
};

TEST_F(GPUPluginTest, GetEntryPointsSetsNeededFunctionPointers) {
#if defined(OS_LINUX)
  NPError error = NP_Initialize(&browser_funcs_, &plugin_funcs_);
  NP_Shutdown();
#else
  NPError error = NP_GetEntryPoints(&plugin_funcs_);
#endif

  EXPECT_EQ(NPERR_NO_ERROR, error);
  EXPECT_TRUE(NULL != plugin_funcs_.newp);
  EXPECT_TRUE(NULL != plugin_funcs_.destroy);
  EXPECT_TRUE(NULL != plugin_funcs_.setwindow);
  EXPECT_TRUE(NULL != plugin_funcs_.event);
  EXPECT_TRUE(NULL != plugin_funcs_.getvalue);
  EXPECT_TRUE(NULL != plugin_funcs_.setvalue);
}

TEST_F(GPUPluginTest, CanInitializeAndShutdownPlugin) {
  EXPECT_EQ(NPERR_NO_ERROR,
      NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS));
  EXPECT_EQ(NPERR_NO_ERROR, NP_Shutdown());
}

TEST_F(GPUPluginTest, InitializeFailsIfBrowserFuncsIsNull) {
  EXPECT_EQ(NPERR_INVALID_FUNCTABLE_ERROR,
      NP_Initialize(NULL INITIALIZE_PLUGIN_FUNCS));
}

TEST_F(GPUPluginTest, InitializeFailsIfAlreadyInitialized) {
  EXPECT_EQ(NPERR_NO_ERROR,
      NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS));
  EXPECT_EQ(NPERR_GENERIC_ERROR,
      NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS));
  EXPECT_EQ(NPERR_NO_ERROR, NP_Shutdown());
}

TEST_F(GPUPluginTest, ShutdownFailsIfNotInitialized) {
  EXPECT_EQ(NPERR_GENERIC_ERROR, NP_Shutdown());
}

TEST_F(GPUPluginTest, NewReturnsErrorForInvalidInstance) {
  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_INVALID_INSTANCE_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      NULL, 0, 0, NULL, NULL, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, GetValueReturnsErrorForInvalidInstance) {
  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  int* result = NULL;
  EXPECT_EQ(NPERR_INVALID_INSTANCE_ERROR, plugin_funcs_.getvalue(
      NULL, NPPVjavaClass, &result));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, DestroyReturnsErrorForInvalidInstance) {
  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_INVALID_INSTANCE_ERROR, plugin_funcs_.destroy(NULL, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, SetWindowReturnsErrorForInvalidInstance) {
  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_INVALID_INSTANCE_ERROR, plugin_funcs_.setwindow(NULL, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, HandleEventReturnsFalseForInvalidInstance) {
  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(0, plugin_funcs_.event(NULL, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, NewCreatesAPluginObjectAndInitializesIt) {
  StrictMock<MockPluginObject> plugin_object;

  EXPECT_CALL(*plugin_object_factory_, CreatePluginObject(
      &npp_, const_cast<NPMIMEType>(GPUPluginObject::kPluginType)))
    .WillOnce(Return(&plugin_object));

  NPObject scriptable_instance;

  EXPECT_CALL(plugin_object, New(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      0, NULL, NULL, NULL))
    .WillOnce(Return(NPERR_NO_ERROR));

  EXPECT_CALL(plugin_object, GetScriptableInstance())
    .WillOnce(Return(&scriptable_instance));

  EXPECT_CALL(plugin_object, Destroy(static_cast<NPSavedData**>(NULL)))
    .WillOnce(Return(NPERR_NO_ERROR));

  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      &npp_, 0, 0, NULL, NULL, NULL));

  NPObject* result = NULL;
  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.getvalue(
      &npp_, NPPVpluginScriptableInstance, &result));
  EXPECT_EQ(&scriptable_instance, result);

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.destroy(&npp_, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, NewFailsIfPluginObjectFactoryFails) {
  EXPECT_CALL(*plugin_object_factory_, CreatePluginObject(
      &npp_, const_cast<NPMIMEType>(GPUPluginObject::kPluginType)))
    .WillOnce(Return(static_cast<PluginObject*>(NULL)));

  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_GENERIC_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      &npp_, 0, 0, NULL, NULL, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, SetWindowForwardsToPluginObject) {
  StrictMock<MockPluginObject> plugin_object;

  EXPECT_CALL(*plugin_object_factory_, CreatePluginObject(
      &npp_, const_cast<NPMIMEType>(GPUPluginObject::kPluginType)))
    .WillOnce(Return(&plugin_object));

  EXPECT_CALL(plugin_object, New(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      0, NULL, NULL, NULL))
    .WillOnce(Return(NPERR_NO_ERROR));

  NPWindow window = {0};

  EXPECT_CALL(plugin_object, SetWindow(&window))
    .WillOnce(Return(NPERR_NO_ERROR));

  EXPECT_CALL(plugin_object, Destroy(static_cast<NPSavedData**>(NULL)))
    .WillOnce(Return(NPERR_NO_ERROR));

  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      &npp_, 0, 0, NULL, NULL, NULL));

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.setwindow(&npp_, &window));

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.destroy(&npp_, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, HandleEventForwardsToPluginObject) {
  StrictMock<MockPluginObject> plugin_object;

  EXPECT_CALL(*plugin_object_factory_, CreatePluginObject(
      &npp_, const_cast<NPMIMEType>(GPUPluginObject::kPluginType)))
    .WillOnce(Return(&plugin_object));

  EXPECT_CALL(plugin_object, New(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      0, NULL, NULL, NULL))
    .WillOnce(Return(NPERR_NO_ERROR));

  NPEvent event = {0};

  EXPECT_CALL(plugin_object, HandleEvent(&event))
    .WillOnce(Return(7));

  EXPECT_CALL(plugin_object, Destroy(static_cast<NPSavedData**>(NULL)))
    .WillOnce(Return(NPERR_NO_ERROR));

  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      &npp_, 0, 0, NULL, NULL, NULL));

  EXPECT_EQ(7, plugin_funcs_.event(&npp_, &event));

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.destroy(&npp_, NULL));

  NP_Shutdown();
}

TEST_F(GPUPluginTest, GetValueReturnsErrorForUnknownVariable) {
  StrictMock<MockPluginObject> plugin_object;

  EXPECT_CALL(*plugin_object_factory_, CreatePluginObject(
      &npp_, const_cast<NPMIMEType>(GPUPluginObject::kPluginType)))
    .WillOnce(Return(&plugin_object));

  EXPECT_CALL(plugin_object, New(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      0, NULL, NULL, NULL))
    .WillOnce(Return(NPERR_NO_ERROR));

  EXPECT_CALL(plugin_object, Destroy(static_cast<NPSavedData**>(NULL)))
    .WillOnce(Return(NPERR_NO_ERROR));

  NP_GetEntryPoints(&plugin_funcs_);
  NP_Initialize(&browser_funcs_ INITIALIZE_PLUGIN_FUNCS);

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.newp(
      const_cast<NPMIMEType>(GPUPluginObject::kPluginType),
      &npp_, 0, 0, NULL, NULL, NULL));

  int* result = NULL;
  EXPECT_EQ(NPERR_GENERIC_ERROR, plugin_funcs_.getvalue(
      &npp_, NPPVjavaClass, &result));

  EXPECT_EQ(NPERR_NO_ERROR, plugin_funcs_.destroy(&npp_, NULL));

  NP_Shutdown();
}

}  // namespace gpu_plugin
}  // namespace o3d
