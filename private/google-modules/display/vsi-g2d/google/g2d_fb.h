/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_FB_H_
#define _G2D_FB_H_

#include <drm/drm.h>

struct drm_format_info;

#define MAX_NUM_PLANES 3 /* RGB */

struct drm_framebuffer *g2d_fb_create(struct drm_device *dev, struct drm_file *file_priv,
				      const struct drm_format_info *info,
				      const struct drm_mode_fb_cmd2 *mode_cmd);

#endif /* _G2D_FB_H_ */
