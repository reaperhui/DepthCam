#include "../common/common.hpp"


#include <pcl/common/transforms.h>

#include "CamData.hpp"


unsigned char buffer[1024 * 1024];
#define OPENRGB 0

// Choose depth image resolution
#define DEPTHRESO TY_IMAGE_MODE_640x480

// #define DepthReso TY_IMAGE_MODE_1280x960

typedef pcl::PointXYZ                PointT;
typedef pcl::PointCloud<PointT>      PointCloud;
typedef pcl::PointCloud<PointT>::Ptr PointCloudPtr;

void    StartDevice(HandleData              & cam,
                    const TY_DEVICE_BASE_INFO base_info);

void    FrameHandler(TY_FRAME_DATA *frame,
                     void          *userdata);

cv::Mat DepthToWorld(const cv::Mat& depth,
                     TY_DEV_HANDLE  hDevice);

void    GenPointCloud(cv::Mat        img,
                      PointCloudPtr& cloud_ptr);

bool    UpdateDevPose(std::vector<HandleData>& handle_data);

void    ConPoint3D(const std::vector<HandleData>& handle_data,
                   PointCloudPtr                   & cloud_out);

int count = 0;

bool exit_main = false;

int main(int argc, char *argv[]) {
  // Device number
  int dev_num = 0;

  LOGD("=== Init lib");
  ASSERT_OK(TYInitLib());
  TY_VERSION_INFO *pVer = (TY_VERSION_INFO *)buffer;
  ASSERT_OK(TYLibVersion(pVer));
  LOGD("     - lib version: %d.%d.%d", pVer->major, pVer->minor, pVer->patch);

  LOGD("=== Get device info");
  TY_DEVICE_BASE_INFO *pBaseInfo = (TY_DEVICE_BASE_INFO *)buffer;
  ASSERT_OK(TYGetDeviceList(pBaseInfo, 100, &dev_num));

  if (dev_num < 1) {
    LOGD("=== Need more than 1 devices");
    return -1;
  }

  std::vector<HandleData> handle_data(dev_num);


  for (int i = 0; i < dev_num; ++i) {
    StartDevice(handle_data[i], pBaseInfo[i]);

    handle_data[i].p_render   = new DepthRender();
    handle_data[i].p_pcviewer = new PointCloudViewer();
  }


  bool ret = UpdateDevPose(handle_data);

  if (!ret) {
    LOGD("Update device pose failed");
    return -1;
  }


  PointCloudPtr cloud_ptr(new PointCloud);
  PointCloudViewerImpl cloud_viewer;

  while (!exit_main) {
    for (int i = 0; i < handle_data.size(); i++) {
      TY_FRAME_DATA frame;
      int err = TYFetchFrame(handle_data[i].hDev, &frame, 1000);

      if (err != TY_STATUS_OK) {
        LOGD("cam %s %d ... Drop one frame", handle_data[i].sn,
             handle_data[i].idx);
        continue;
      }

      FrameHandler(&frame, &handle_data[i]);
    }

    ConPoint3D(handle_data, cloud_ptr);

    cloud_viewer.show(cloud_ptr, "cloud");


    // use keyboard to control picture collect
    int key = cv::waitKey(1);

    switch (key & 0xff) {
    case 0xff:
      break;

    case 'q':
      exit_main = true;
      break;

    default:
      LOGD("Unmapped key %d", key);
    }
  }


  // close device
  for (int i = 0; i < handle_data.size(); i++) {
    ASSERT_OK(TYStopCapture(handle_data[i].hDev));
    ASSERT_OK(TYCloseDevice(handle_data[i].hDev));

    // // MSLEEP(10); // sleep to ensure buffer is not used any more
    // delete handle_data[i].fb[0];
    // delete handle_data[i].fb[1];
    delete handle_data[i].p_pcviewer;
    delete handle_data[i].p_render;
  }
  ASSERT_OK(TYDeinitLib());

  LOGD("=== Main done!");
  return 0;
}

/**
 * @brief Start device by BaseInfo
 * @param [out] cam      CamInfo data, returned after start device
 * @param [in] BaseInfo  Device data, used to start device
 */
void StartDevice(HandleData              & cam,
                 const TY_DEVICE_BASE_INFO base_info) {
  strncpy(cam.sn, base_info.id, sizeof(cam.sn));

  // Open Device
  ASSERT_OK(TYOpenDevice(base_info.id, &(cam.hDev)));

  // Into develop mode
  #ifdef DEVELOPER_MODE
  LOGD("=== Enter Developer Mode");
  ASSERT_OK(TYEnterDeveloperMode(cam.hDev));
  #endif // ifdef DEVELOPER_MODE

  // handle component
  int32_t allComps;
  ASSERT_OK(TYGetComponentIDs(cam.hDev, &allComps));

  // whether to open RGB camera
  if (OPENRGB && allComps & TY_COMPONENT_RGB_CAM) {
    LOGD("=== Has RGB camera, open RGB cam");
    ASSERT_OK(TYEnableComponents(cam.hDev, TY_COMPONENT_RGB_CAM));
  }

  // open depth camera
  LOGD("=== Configure components, open depth cam");
  int32_t componentIDs = TY_COMPONENT_DEPTH_CAM;
  ASSERT_OK(TYEnableComponents(cam.hDev, componentIDs));

  LOGD("=== Configure components, open point3d cam");

  // int32_t componentIDs = TY_COMPONENT_POINT3D_CAM;
  ASSERT_OK(TYEnableComponents(cam.hDev, TY_COMPONENT_POINT3D_CAM));

  LOGD("=== Configure feature, set resolution to 640x480.");
  int err = TYSetEnum(cam.hDev,
                      TY_COMPONENT_DEPTH_CAM,
                      TY_ENUM_IMAGE_MODE,
                      DEPTHRESO);
  ASSERT(err == TY_STATUS_OK || err == TY_STATUS_NOT_PERMITTED);

  LOGD("=== Prepare image buffer");
  int32_t frameSize;
  ASSERT_OK(TYGetFrameBufferSize(cam.hDev, &frameSize));
  LOGD("     - Get size of framebuffer, %d", frameSize);
  ASSERT(frameSize >= 640 * 480 * 2);

  LOGD("     - Allocate & enqueue buffers");
  cam.fb[0] = new char[frameSize];
  cam.fb[1] = new char[frameSize];
  LOGD("     - Enqueue buffer (%p, %d)", cam.fb[0], frameSize);
  ASSERT_OK(TYEnqueueBuffer(cam.hDev, cam.fb[0], frameSize));
  LOGD("     - Enqueue buffer (%p, %d)", cam.fb[1], frameSize);
  ASSERT_OK(TYEnqueueBuffer(cam.hDev, cam.fb[1], frameSize));

  bool triggerMode = false;
  LOGD("=== Set trigger mode %d", triggerMode);
  ASSERT_OK(TYSetBool(cam.hDev, TY_COMPONENT_DEVICE, TY_BOOL_TRIGGER_MODE,
                      triggerMode));

  LOGD("=== Start capture");
  ASSERT_OK(TYStartCapture(cam.hDev));
}

void FrameHandler(TY_FRAME_DATA *frame,
                  void          *userdata) {
  HandleData *pData = (HandleData *)userdata;

  count++;
  LOGD("count is: %d", count);

  //
  cv::Mat depth, irl, irr, color, p3d;
  cv::Mat point;


  parseFrame(*frame, &depth, 0, 0, 0, &p3d);

  char win[64];

//  if (!depth.empty()) {
//    cv::Mat colorDepth = pData->p_render->Compute(depth);
//    sprintf(win, "depth-%s", pData->sn);
//    cv::imshow(win, colorDepth);

    //    point = DepthToWorld(depth, pData->hDev);
    //
    //    pData->p_pcviewer->show(p3d, "Point3D");
    //
    //    if (pData->p_pcviewer->isStopped("Point3D")) {
    //        exit_main = true;
    //      return;
    //    }
//  }

  if (!p3d.empty()) {
    // Save 3d point image
    pData->point3d = p3d;
//
//    sprintf(win, "Point3D-%s", pData->sn);
//    pData->p_pcviewer->show(p3d, win);
//
//    if (pData->p_pcviewer->isStopped(win)) {
//      exit_main = true;
//      return;
//    }
  }

  // if (!irl.empty()) {
  //   sprintf(win, "LeftIR-%s", pData->sn);
  //   cv::imshow(win, irl);
  // }
  //
  // if (!irr.empty()) {
  //   sprintf(win, "RightIR-%s", pData->sn);
  //   cv::imshow(win, irr);
  // }
  //
  // if (!color.empty()) {
  //   sprintf(win, "color-%s", pData->sn);
  //   cv::imshow(win, color);
  // }

  pData->idx++;


  LOGD("=== Callback: Re-enqueue buffer(%p, %d)",
       frame->userBuffer,
       frame->bufferSize);

  ASSERT_OK(TYEnqueueBuffer(pData->hDev, frame->userBuffer,
                            frame->bufferSize));
}

cv::Mat DepthToWorld(const cv::Mat& depth,
                     TY_DEV_HANDLE  hDevice) {
  // conver depth to world
  //  static TY_VECT_3F depthbuf[1280 * 960];
  //  static TY_VECT_3F worldbuf[1280 * 960];
  static TY_VECT_3F depthbuf[640 * 480];
  static TY_VECT_3F worldbuf[640 * 480];

  int k            = 0;
  uint16_t *pdepth = (uint16_t *)depth.data;

  for (int r = 0; r < depth.rows; r++)
    for (int c = 0; c < depth.cols; c++) {
      depthbuf[k].x = c;
      depthbuf[k].y = r;
      depthbuf[k].z = pdepth[k];
      k++;
    }
  ASSERT_OK(TYDepthToWorld(hDevice, depthbuf, worldbuf, 0,
                           depth.rows * depth.cols));

  // show point3d
  cv::Mat point3D(depth.rows,
                  depth.cols,
                  CV_32FC3,
                  (void *)worldbuf);
  return point3D;
}

bool UpdateDevPose(std::vector<HandleData>& handle_data) {
  for (int i = 0; i < handle_data.size(); ++i) {
    char *ch = handle_data[i].sn;

    if (0 == strcmp("207000002038", handle_data[i].sn)) {
      handle_data[i].dev_pose << 1, 0, 0, 0,
              0, 1, 0, 0,
              0, 0, 1, 0,
              0, 0, 0, 1;
    } else {
      handle_data[i].dev_pose << 1, 0, 0, 720,
              0, 1, 0, 0,
              0, 0, 1, 0,
              0, 0, 0, 1;
    }
  }
  return true;
}

void GenPointCloud(cv::Mat img, PointCloudPtr& cloud_ptr) {
  int n       = img.rows * img.cols;
  float *data = (float *)img.data;

  for (int i = 0; i < n; ++i) {
    cloud_ptr->push_back(PointT(data[i * 3 + 0], data[i * 3 + 1],
                                data[i * 3 + 2]));
  }
}

void ConPoint3D(const std::vector<HandleData>& handle_data,
                PointCloudPtr                   & cloud_out) {
  cloud_out->clear();

  Eigen::Matrix4f trans_mat;
  PointCloudPtr   trans_cloud(new PointCloud);

  for (int i = 0; i < handle_data.size(); ++i) {
    trans_cloud->clear();

    PointCloudPtr cloud_ptr(new PointCloud);
    GenPointCloud(handle_data[i].point3d, cloud_ptr);
    pcl::transformPointCloud(*cloud_ptr, *trans_cloud, handle_data[i].dev_pose);

    for (int j = 0; j < trans_cloud->size(); ++j) {
      cloud_out->push_back(trans_cloud->at(j));
        std::cout << "the point cloud before transform is: " << cloud_ptr->at(j) << std::endl;
    }
  }
}

// void ConP3d(const HandleData                   & handle_data,
//            pcl::PointCloud<pcl::PointXYZ>::Ptr& cloud) {
//  cloud->resize(0);
//
//  cv::Mat p3d = handle_data.point3d;
//
//  int n       = p3d.rows * p3d.cols;
//  float *data = p3d.data;
//
//  for (int j = 0; j < n; ++j) {
//    cloud->push_back(pcl::PointXYZ(data[i * 3 + 0], data[i * 3 + 1],
//                                   data[i * 3 + 2]));
//  }
// }