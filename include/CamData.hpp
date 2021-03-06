#ifndef _CAMDATA_HPP
#define _CAMDATA_HPP


#include <Eigen/Dense>


struct CamInfo {
	char		sn[32];
	TY_DEV_HANDLE	hDev;
	char *		fb[2];
	TY_FRAME_DATA	frame;
	int		idx;
	DepthRender	render;

	CamInfo() : hDev(0), idx(0)
	{
		fb[0] = 0; fb[1] = 0;
	}
};


struct HandleData {
	char			sn[32];
	TY_DEV_HANDLE		hDev;
	char *			fb[2];
	TY_FRAME_DATA		frame;

	// device pose
	Eigen::Matrix4f		dev_pose;


	cv::Mat			point3d;
	cv::Mat			color;
	cv::Mat			depth;

	DepthRender *		p_render;

	PointCloudViewer *	p_pcviewer;

	int			idx;
	HandleData() : hDev(0), idx(0), dev_pose(Eigen::Matrix4f::Identity())
	{
		fb[0] = 0; fb[1] = 0;
	}
};

typedef enum CAM_TYPE {
	RGB_VGA,
	RGB_HD,
	DEPTH_VGA,
	DEPTH_HD,
	POINT3D_VGA,
	POINT3D_HD
}CAM_TYPE;


#endif // ifndef _CAMDATA_HPP
