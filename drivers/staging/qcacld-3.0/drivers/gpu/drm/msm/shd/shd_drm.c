/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"[drm-shd] %s: " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/component.h>
#include <linux/of_irq.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include "sde_connector.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_crtc.h>
#include <drm/drm_vblank.h>

#include "msm_drv.h"
#include "msm_kms.h"
#include "sde_connector.h"
#include "sde_encoder.h"
#include "sde_crtc.h"
#include "sde_plane.h"
#include "shd_drm.h"
#include "shd_hw.h"

static LIST_HEAD(g_base_list);

struct shd_crtc {
	struct drm_crtc_helper_funcs helper_funcs;
	const struct drm_crtc_helper_funcs *orig_helper_funcs;
	struct drm_crtc_funcs funcs;
	const struct drm_crtc_funcs *orig_funcs;
	struct shd_display *display;
};

struct shd_bridge {
	struct drm_bridge base;
	struct shd_display *display;
};

struct shd_kms {
	struct msm_kms_funcs funcs;
	const struct msm_kms_funcs *orig_funcs;
};

struct sde_cp_node_dummy {
	u32 property_id;
	u32 prop_flags;
	u32 feature;
	void *blob_ptr;
	uint64_t prop_val;
	const struct sde_pp_blk *pp_blk;
	struct list_head feature_list;
	struct list_head active_list;
	struct list_head dirty_list;
};

static struct shd_kms *g_shd_kms;

static enum drm_connector_status shd_display_base_detect(
		struct drm_connector *connector,
		bool force,
		void *disp)
{
	return connector_status_disconnected;
}

static int shd_display_init_base_connector(struct drm_device *dev,
						struct shd_display_base *base)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct sde_connector *sde_conn;
	struct drm_connector_list_iter conn_iter;
	int rc = 0;

	drm_connector_list_iter_begin(dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		encoder = drm_atomic_helper_best_encoder(connector);
		if (encoder == base->encoder) {
			base->connector = connector;
			break;
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	if (!base->connector) {
		SDE_ERROR("failed to find connector\n");
		return -ENOENT;
	}

	/* set base connector disconnected*/
	sde_conn = to_sde_connector(base->connector);
	base->ops = sde_conn->ops;
	sde_conn->ops.detect = shd_display_base_detect;

	SDE_DEBUG("found base connector %d\n", base->connector->base.id);

	return rc;
}

static int shd_display_init_base_encoder(struct drm_device *dev,
						struct shd_display_base *base)
{
	struct drm_encoder *encoder;
	struct sde_encoder_hw_resources hw_res;
	struct sde_connector_state conn_state = {};
	bool has_mst;
	int i, rc = 0;

	drm_for_each_encoder(encoder, dev) {
		sde_encoder_get_hw_resources(encoder,
				&hw_res, &conn_state.base);
		has_mst = (encoder->encoder_type == DRM_MODE_ENCODER_DPMST);
		for (i = INTF_0; i < INTF_MAX; i++) {
			if (hw_res.intfs[i - INTF_0] != INTF_MODE_NONE &&
					base->intf_idx == (i - INTF_0) &&
					base->mst_port == has_mst) {
				base->encoder = encoder;
				break;
			}
		}
		if (base->encoder)
			break;
	}

	if (!base->encoder) {
		SDE_ERROR("can't find base encoder for intf %d\n",
			base->intf_idx);
		return -ENOENT;
	}

	switch (base->encoder->encoder_type) {
	case DRM_MODE_ENCODER_DSI:
		base->connector_type = DRM_MODE_CONNECTOR_DSI;
		break;
	case DRM_MODE_ENCODER_TMDS:
	case DRM_MODE_ENCODER_DPMST:
		base->connector_type = DRM_MODE_CONNECTOR_DisplayPort;
		break;
	default:
		base->connector_type = DRM_MODE_CONNECTOR_Unknown;
		break;
	}

	SDE_DEBUG("found base encoder %d, type %d, connect type %d\n",
			base->encoder->base.id,
			base->encoder->encoder_type,
			base->connector_type);

	return rc;
}

static int shd_display_init_base_crtc(struct drm_device *dev,
						struct shd_display_base *base)
{
	struct drm_crtc *crtc = NULL;
	struct msm_drm_private *priv;
	struct drm_plane *primary;
	int crtc_idx;
	int i;

	priv = dev->dev_private;

	if (base->encoder->crtc) {
		/* if cont splash is enabled on crtc */
		crtc = base->encoder->crtc;
		crtc_idx = drm_crtc_index(crtc);
	} else {
		/* find last crtc for base encoder */
		for (i = priv->num_crtcs - 1; i >= 0; i--) {
			if (base->encoder->possible_crtcs & (1 << i)) {
				crtc = priv->crtcs[i];
				crtc_idx = i;
				break;
			}
		}

		if (!crtc)
			return -ENOENT;
	}

	if (priv->num_planes >= MAX_PLANES)
		return -ENOENT;

	/* create dummy primary plane for base crtc */
	primary = sde_plane_init(dev, SSPP_DMA0, true, 0, 0);
	if (IS_ERR(primary))
		return -ENOMEM;
	priv->planes[priv->num_planes++] = primary;
	if (primary->funcs->reset)
		primary->funcs->reset(primary);

	SDE_DEBUG("create dummay plane%d free plane%d\n",
			DRMID(primary), DRMID(crtc->primary));

	crtc->primary = primary;
	primary->crtc = crtc;

	/* disable crtc from other encoders */
	for (i = 0; i < priv->num_encoders; i++) {
		if (priv->encoders[i] != base->encoder)
			priv->encoders[i]->possible_crtcs &= ~(1 << crtc_idx);
	}

	base->crtc = crtc;
	SDE_DEBUG("found base crtc %d\n", crtc->base.id);

	return 0;
}

static int shd_crtc_validate_shared_display(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct sde_crtc *sde_crtc;
	struct shd_crtc *shd_crtc;
	struct sde_crtc_state *sde_crtc_state;
	struct drm_plane *plane;
	const struct drm_plane_state *pstate;
	struct sde_plane_state *sde_pstate;
	int i;

	sde_crtc = to_sde_crtc(crtc);
	shd_crtc = sde_crtc->priv_handle;
	sde_crtc_state = to_sde_crtc_state(state);

	/* check z-pos for all planes */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		sde_pstate = to_sde_plane_state(pstate);
		if (sde_pstate->stage >=
			shd_crtc->display->stage_range.size + SDE_STAGE_0) {
			SDE_DEBUG("plane stage %d is larger than maximum %d\n",
				sde_pstate->stage,
				shd_crtc->display->stage_range.size);
			return -EINVAL;
		}
	}

	/* check z-pos for all dim layers */
	for (i = 0; i < sde_crtc_state->num_dim_layers; i++) {
		if (sde_crtc_state->dim_layer[i].stage >=
			shd_crtc->display->stage_range.size + SDE_STAGE_0) {
			SDE_DEBUG("dim stage %d is larger than maximum %d\n",
				sde_crtc_state->dim_layer[i].stage,
				shd_crtc->display->stage_range.size);
			return -EINVAL;
		}
	}

	/* update crtc_roi */
	sde_crtc_state->crtc_roi.x = -shd_crtc->display->roi.x;
	sde_crtc_state->crtc_roi.y = -shd_crtc->display->roi.y;
	sde_crtc_state->crtc_roi.w = 0;
	sde_crtc_state->crtc_roi.h = 0;

	return 0;
}

static int shd_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct sde_crtc *sde_crtc = to_sde_crtc(crtc);
	struct shd_crtc *shd_crtc = sde_crtc->priv_handle;
	struct sde_crtc_state *sde_crtc_state = to_sde_crtc_state(state);
	int rc;

	/* disable bw voting if not full size in vertical */
	if (shd_crtc->display->roi.h != shd_crtc->display->base->mode.vdisplay)
		sde_crtc_state->bw_control = false;

	rc = shd_crtc->orig_helper_funcs->atomic_check(crtc, state);
	if (rc)
		return rc;

	return shd_crtc_validate_shared_display(crtc, state);
}

static int shd_crtc_atomic_set_property(struct drm_crtc *crtc,
		struct drm_crtc_state *state,
		struct drm_property *property,
		uint64_t val)
{
	struct sde_crtc *sde_crtc = to_sde_crtc(crtc);
	struct shd_crtc *shd_crtc = sde_crtc->priv_handle;
	struct sde_cp_node_dummy *prop_node;

	if (!crtc || !state || !property) {
		SDE_ERROR("invalid argument(s)\n");
		return -EINVAL;
	}

	/* ignore all the dspp properties */
	list_for_each_entry(prop_node, &sde_crtc->feature_list, feature_list) {
		if (property->base.id == prop_node->property_id)
			return 0;
	}

	return shd_crtc->orig_funcs->atomic_set_property(crtc,
		state, property, val);
}

u32 shd_get_shared_crtc_mask(struct drm_crtc *src_crtc)
{
	struct shd_crtc *shd_src_crtc, *shd_crtc;
	struct drm_crtc *crtc;
	u32 crtc_mask = 0;

	if (!src_crtc)
		return 0;

	if (src_crtc->helper_private->atomic_check != shd_crtc_atomic_check)
		return drm_crtc_mask(src_crtc);

	shd_src_crtc = to_sde_crtc(src_crtc)->priv_handle;

	drm_for_each_crtc(crtc, src_crtc->dev) {
		if (crtc->helper_private->atomic_check !=
				shd_crtc_atomic_check)
			continue;

		shd_crtc = to_sde_crtc(crtc)->priv_handle;

		if (shd_src_crtc->display->base == shd_crtc->display->base)
			crtc_mask |= drm_crtc_mask(crtc);
	}

	return crtc_mask;
}

void shd_skip_shared_plane_update(struct drm_plane *plane,
		struct drm_crtc *crtc)
{
	struct sde_crtc *sde_crtc;
	struct shd_crtc *shd_crtc;
	enum sde_sspp sspp;
	bool is_virtual;
	int i;

	if (!plane || !crtc) {
		SDE_ERROR("invalid plane or crtc\n");
		return;
	}

	if (crtc->funcs->atomic_set_property !=
		shd_crtc_atomic_set_property) {
		SDE_ERROR("not shared crtc\n");
		return;
	}

	sde_crtc = to_sde_crtc(crtc);
	shd_crtc = sde_crtc->priv_handle;
	sspp = sde_plane_pipe(plane);
	is_virtual = is_sde_plane_virtual(plane);

	for (i = 0; i < sde_crtc->num_ctls; i++)
		sde_shd_hw_skip_sspp_clear(
			sde_crtc->mixers[i].hw_ctl, sspp, is_virtual);
}

static int shd_display_atomic_check(struct msm_kms *kms,
		struct drm_atomic_state *state)
{
	struct msm_drm_private *priv;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_connector_state *conn_state;
	struct sde_crtc *sde_crtc;
	struct shd_crtc *shd_crtc;
	struct shd_display *display;
	struct shd_display_base *base;
	u32 base_mask = 0, enable_mask = 0, disable_mask = 0;
	u32 crtc_mask, active_mask;
	bool active;
	int i, rc;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state,
			new_crtc_state, i) {
		if (crtc->helper_private->atomic_check !=
				shd_crtc_atomic_check)
			continue;

		if (old_crtc_state->active == new_crtc_state->active)
			continue;

		sde_crtc = to_sde_crtc(crtc);
		shd_crtc = sde_crtc->priv_handle;
		base = shd_crtc->display->base;
		base_mask |= drm_crtc_mask(base->crtc);

		if (new_crtc_state->active)
			enable_mask |= drm_crtc_mask(crtc);
		else
			disable_mask |= drm_crtc_mask(crtc);
	}

	if (!base_mask)
		return g_shd_kms->orig_funcs->atomic_check(kms, state);

	/*
	 * If base display need to be enabled/disabled, add state
	 * changes to the same atomic state. As base crtc is always
	 * ahead of shared crtc in the crtc list, base crtc is
	 * enabled/disabled before shared crtcs.
	 */
	list_for_each_entry(base, &g_base_list, head) {
		if (!(drm_crtc_mask(base->crtc) & base_mask))
			continue;

		/* read old crtc state from all shared displays */
		crtc_mask = active_mask = 0;
		list_for_each_entry(display, &base->disp_list, head) {
			crtc_mask |= drm_crtc_mask(display->crtc);
			if (display->crtc->state->active)
				active_mask |= drm_crtc_mask(display->crtc);
		}

		/* apply changes in state */
		active_mask |= (enable_mask & crtc_mask);
		active_mask &= ~disable_mask;
		active = !!active_mask;

		/* skip if there is no change */
		if (base->crtc->state->active == active)
			continue;

		new_crtc_state = drm_atomic_get_crtc_state(state,
				base->crtc);
		if (IS_ERR(new_crtc_state))
			return PTR_ERR(new_crtc_state);

		new_crtc_state->active = active;

		conn_state = drm_atomic_get_connector_state(state,
				base->connector);
		if (IS_ERR(conn_state))
			return PTR_ERR(conn_state);

		rc = drm_atomic_set_mode_for_crtc(new_crtc_state,
				active ? &base->mode : NULL);
		if (rc) {
			SDE_ERROR("failed to set mode for crtc\n");
			return rc;
		}

		rc = drm_atomic_set_crtc_for_connector(conn_state,
				active ? base->crtc : NULL);
		if (rc) {
			SDE_ERROR("failed to set crtc for connector\n");
			return rc;
		}
	}

	rc = g_shd_kms->orig_funcs->atomic_check(kms, state);
	if (rc)
		return rc;

	/* wait if there is base thread running */
	priv = state->dev->dev_private;
	spin_lock(&priv->pending_crtcs_event.lock);
	wait_event_interruptible_locked(
			priv->pending_crtcs_event,
			!(priv->pending_crtcs & base_mask));
	spin_unlock(&priv->pending_crtcs_event.lock);

	return 0;
}

static int shd_connector_get_info(struct drm_connector *connector,
		struct msm_display_info *info, void *data)
{
	struct shd_display *display = data;

	if (!info || !data || !display->base || !display->drm_dev) {
		SDE_ERROR("invalid params\n");
		return -EINVAL;
	}

	info->intf_type = display->base->connector_type;
	info->capabilities = MSM_DISPLAY_CAP_VID_MODE |
				MSM_DISPLAY_CAP_HOT_PLUG |
				MSM_DISPLAY_CAP_MST_MODE;
	info->is_connected = true;
	info->num_of_h_tiles = 1;
	info->h_tile_instance[0] = display->base->intf_idx;

	return 0;
}

static int shd_connector_get_mode_info(struct drm_connector *connector,
		const struct drm_display_mode *drm_mode,
		struct msm_mode_info *mode_info,
		u32 max_mixer_width, void *display)
{
	struct shd_display *shd_display = display;

	if (!drm_mode || !mode_info || !max_mixer_width || !display) {
		SDE_ERROR("invalid params\n");
		return -EINVAL;
	}

	memset(mode_info, 0, sizeof(*mode_info));

	mode_info->frame_rate = drm_mode->vrefresh;
	mode_info->vtotal = drm_mode->vtotal;
	mode_info->comp_info.comp_type = MSM_DISPLAY_COMPRESSION_NONE;

	if (shd_display->src.h != shd_display->roi.h)
		mode_info->vpadding = shd_display->roi.h;

	return 0;
}

static
enum drm_connector_status shd_connector_detect(struct drm_connector *conn,
		bool force,
		void *display)
{
	struct shd_display *disp = display;
	struct sde_connector *sde_conn;
	struct drm_connector *b_conn;
	enum drm_connector_status status = connector_status_disconnected;

	if (!conn || !display || !disp->base) {
		SDE_ERROR("invalid params\n");
		goto end;
	}
	b_conn =  disp->base->connector;
	if (b_conn) {
		sde_conn = to_sde_connector(b_conn);
		status = disp->base->ops.detect(b_conn,
						force, sde_conn->display);
		conn->display_info.width_mm = b_conn->display_info.width_mm;
		conn->display_info.height_mm = b_conn->display_info.height_mm;
	}

end:
	return status;
}
static int shd_drm_update_edid_name(struct edid *edid, const char *name)
{
	u8 *dtd = (u8 *)&edid->detailed_timings[3];
	u8 standard_header[] = {0x00, 0x00, 0x00, 0xFE, 0x00};
	u32 dtd_size = 18;
	u32 header_size = sizeof(standard_header);

	if (!name)
		return -EINVAL;

	/* Fill standard header */
	memcpy(dtd, standard_header, header_size);

	dtd_size -= header_size;
	dtd_size = min_t(u32, dtd_size, strlen(name));

	memcpy(dtd + header_size, name, dtd_size);

	return 0;
}

static void shd_drm_update_checksum(struct edid *edid)
{
	u8 *data = (u8 *)edid;
	u32 i, sum = 0;

	for (i = 0; i < EDID_LENGTH - 1; i++)
		sum += data[i];

	edid->checksum = 0x100 - (sum & 0xFF);
}

static int shd_connector_get_modes(struct drm_connector *connector,
		void *data)
{
	struct drm_display_mode drm_mode;
	struct shd_display *disp = data;
	struct drm_display_mode *m;
	u32 count = 0;
	int rc;
	u32 edid_size;
	struct edid edid;
	const u8 edid_buf[EDID_LENGTH] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x44, 0x6D,
		0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x1B, 0x10, 0x01, 0x03,
		0x80, 0x50, 0x2D, 0x78, 0x0A, 0x0D, 0xC9, 0xA0, 0x57, 0x47,
		0x98, 0x27, 0x12, 0x48, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
	};
	edid_size = min_t(u32, sizeof(edid), EDID_LENGTH);
	memcpy(&edid, edid_buf, edid_size);

	memcpy(&drm_mode, &disp->base->mode, sizeof(drm_mode));

	drm_mode.hdisplay = disp->src.w;
	drm_mode.hsync_start = drm_mode.hdisplay;
	drm_mode.hsync_end = drm_mode.hsync_start;
	drm_mode.htotal = drm_mode.hsync_end;

	drm_mode.vdisplay = disp->src.h;
	drm_mode.vsync_start = drm_mode.vdisplay;
	drm_mode.vsync_end = drm_mode.vsync_start;
	drm_mode.vtotal = drm_mode.vsync_end;

	m = drm_mode_duplicate(disp->drm_dev, &drm_mode);
	if (!m)
		return 0;
	drm_mode_set_name(m);
	drm_mode_probed_add(connector, m);
	rc = shd_drm_update_edid_name(&edid, disp->name);
	if (rc) {
		count = 0;
		return count;
	}
	shd_drm_update_checksum(&edid);
	rc = drm_mode_connector_update_edid_property(connector, &edid);
	if (rc)
		count = 0;
	return 1;
}

static
enum drm_mode_status shd_connector_mode_valid(struct drm_connector *connector,
		struct drm_display_mode *mode,
		void *display)
{
	return MODE_OK;
}

static int shd_conn_set_info_blob(struct drm_connector *connector,
		void *info,
		void *display,
		struct msm_mode_info *mode_info)
{
	struct shd_display *shd_display = display;

	if (!info || !shd_display)
		return -EINVAL;

	sde_kms_info_add_keyint(info, "max_blendstages",
				shd_display->stage_range.size);

	sde_kms_info_add_keystr(info, "display type",
				shd_display->display_type);

	sde_kms_info_add_keystr(info, "display type",
				shd_display->display_type);

	return 0;
}

static int shd_conn_set_property(struct drm_connector *connector,
		struct drm_connector_state *state,
		int property_index,
		uint64_t value,
		void *display)
{
	struct sde_connector *c_conn;

	c_conn = to_sde_connector(connector);

	/* overwrite properties that are not supported */
	switch (property_index) {
	case CONNECTOR_PROP_BL_SCALE:
		c_conn->bl_scale_dirty = false;
		c_conn->unset_bl_level = 0;
		break;
	case CONNECTOR_PROP_AD_BL_SCALE:
		c_conn->bl_scale_dirty = false;
		c_conn->unset_bl_level = 0;
		break;
	default:
		break;
	}

	return 0;
}

static inline
int shd_bridge_attach(struct drm_bridge *shd_bridge)
{
	return 0;
}

static inline
void shd_bridge_pre_enable(struct drm_bridge *drm_bridge)
{
}

static inline
void shd_bridge_enable(struct drm_bridge *drm_bridge)
{
}

static inline
void shd_bridge_disable(struct drm_bridge *drm_bridge)
{
}

static inline
void shd_bridge_post_disable(struct drm_bridge *drm_bridge)
{
}


static inline
void shd_bridge_mode_set(struct drm_bridge *drm_bridge,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
}

static inline
bool shd_bridge_mode_fixup(struct drm_bridge *drm_bridge,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static const struct drm_bridge_funcs shd_bridge_ops = {
	.attach       = shd_bridge_attach,
	.mode_fixup   = shd_bridge_mode_fixup,
	.pre_enable   = shd_bridge_pre_enable,
	.enable       = shd_bridge_enable,
	.disable      = shd_bridge_disable,
	.post_disable = shd_bridge_post_disable,
	.mode_set     = shd_bridge_mode_set,
};

static int shd_drm_bridge_init(void *data, struct drm_encoder *encoder)
{
	int rc = 0;
	struct shd_bridge *bridge;
	struct drm_device *dev;
	struct shd_display *display = data;
	struct msm_drm_private *priv = NULL;

	bridge = kzalloc(sizeof(*bridge), GFP_KERNEL);
	if (!bridge) {
		rc = -ENOMEM;
		goto error;
	}

	dev = display->drm_dev;
	bridge->display = display;
	bridge->base.funcs = &shd_bridge_ops;
	bridge->base.encoder = encoder;

	priv = dev->dev_private;

	rc = drm_bridge_attach(encoder, &bridge->base, NULL);
	if (rc) {
		SDE_ERROR("failed to attach bridge, rc=%d\n", rc);
		goto error_free_bridge;
	}

	encoder->bridge = &bridge->base;
	priv->bridges[priv->num_bridges++] = &bridge->base;
	display->bridge = &bridge->base;

	return 0;

error_free_bridge:
	kfree(bridge);
error:
	return rc;
}

static void shd_drm_bridge_deinit(void *data)
{
	struct shd_display *display = data;
	struct shd_bridge *bridge = container_of(display->bridge,
		struct shd_bridge, base);

	if (bridge && bridge->base.encoder)
		bridge->base.encoder->bridge = NULL;

	kfree(bridge);
}

static int shd_drm_obj_init(struct shd_display *display)
{
	struct msm_drm_private *priv;
	struct drm_device *dev;
	struct drm_crtc *crtc;
	struct drm_plane *primary;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct sde_crtc *sde_crtc;
	struct shd_crtc *shd_crtc;
	struct msm_display_info info;
	int rc = 0;
	uint32_t i;

	static const struct sde_connector_ops shd_ops = {
		.set_info_blob = shd_conn_set_info_blob,
		.detect =       shd_connector_detect,
		.get_modes =    shd_connector_get_modes,
		.mode_valid =   shd_connector_mode_valid,
		.get_info =     shd_connector_get_info,
		.get_mode_info = shd_connector_get_mode_info,
		.set_property = shd_conn_set_property,
	};

	static const struct sde_encoder_ops enc_ops = {
		.phys_init =    sde_encoder_phys_shd_init,
	};

	dev = display->drm_dev;
	priv = dev->dev_private;

	if (priv->num_crtcs >= MAX_CRTCS) {
		SDE_ERROR("crtc reaches the maximum %d\n", priv->num_crtcs);
		rc = -ENOENT;
		goto end;
	}

	/* search plane that doesn't belong to any crtc */
	primary = NULL;
	for (i = 0; i < priv->num_planes; i++) {
		bool found = false;

		drm_for_each_crtc(crtc, dev) {
			if (crtc->primary == priv->planes[i]) {
				found = true;
				break;
			}
		}

		if (!found) {
			primary = priv->planes[i];
			if (primary->type == DRM_PLANE_TYPE_OVERLAY)
				dev->mode_config.num_overlay_plane--;
			primary->type = DRM_PLANE_TYPE_PRIMARY;
			break;
		}
	}

	if (!primary) {
		SDE_ERROR("failed to find primary plane\n");
		rc = -ENOENT;
		goto end;
	}

	SDE_DEBUG("find primary plane %d\n", DRMID(primary));

	memset(&info, 0x0, sizeof(info));
	rc = shd_connector_get_info(NULL, &info, display);
	if (rc) {
		SDE_ERROR("shd get_info failed\n");
		goto end;
	}

	encoder = sde_encoder_init_with_ops(dev, &info, &enc_ops);
	if (IS_ERR_OR_NULL(encoder)) {
		SDE_ERROR("shd encoder init failed\n");
		rc = -ENOENT;
		goto end;
	}

	SDE_DEBUG("create encoder %d\n", DRMID(encoder));

	rc = shd_drm_bridge_init(display, encoder);
	if (rc) {
		SDE_ERROR("shd bridge init failed, %d\n", rc);
		sde_encoder_destroy(encoder);
		goto end;
	}

	connector = sde_connector_init(dev,
				encoder,
				NULL,
				display,
				&shd_ops,
				DRM_CONNECTOR_POLL_HPD,
				info.intf_type);
	if (connector) {
		priv->encoders[priv->num_encoders++] = encoder;
		priv->connectors[priv->num_connectors++] = connector;
	} else {
		SDE_ERROR("shd connector init failed\n");
		shd_drm_bridge_deinit(display);
		sde_encoder_destroy(encoder);
		rc = -ENOENT;
		goto end;
	}

	SDE_DEBUG("create connector %d\n", DRMID(connector));

	crtc = sde_crtc_init(dev, primary);
	if (IS_ERR(crtc)) {
		rc = PTR_ERR(crtc);
		goto end;
	}
	priv->crtcs[priv->num_crtcs++] = crtc;
	sde_crtc_post_init(dev, crtc);

	SDE_DEBUG("create crtc %d index %d\n", DRMID(crtc),
		drm_crtc_index(crtc));

	/* update encoder's possible crtcs */
	encoder->possible_crtcs = 1 << (priv->num_crtcs - 1);

	/* update plane's possible crtcs */
	for (i = 0; i < priv->num_planes; i++)
		priv->planes[i]->possible_crtcs |= 1 << (priv->num_crtcs - 1);

	/* update crtc's check function */
	shd_crtc = kzalloc(sizeof(*shd_crtc), GFP_KERNEL);
	if (!shd_crtc) {
		rc = -ENOMEM;
		goto end;
	}

	shd_crtc->helper_funcs = *crtc->helper_private;
	shd_crtc->orig_helper_funcs = crtc->helper_private;
	shd_crtc->helper_funcs.atomic_check = shd_crtc_atomic_check;
	shd_crtc->funcs = *crtc->funcs;
	shd_crtc->orig_funcs = crtc->funcs;
	shd_crtc->funcs.atomic_set_property = shd_crtc_atomic_set_property;
	shd_crtc->display = display;
	sde_crtc = to_sde_crtc(crtc);
	sde_crtc->priv_handle = shd_crtc;
	crtc->helper_private = &shd_crtc->helper_funcs;
	crtc->funcs = &shd_crtc->funcs;
	display->crtc = crtc;

	/* initialize display thread */
	i = priv->num_crtcs - 1;
	priv->disp_thread[i].crtc_id = priv->crtcs[i]->base.id;
	kthread_init_worker(&priv->disp_thread[i].worker);
	priv->disp_thread[i].dev = dev;
	priv->disp_thread[i].thread =
		kthread_run(kthread_worker_fn,
			&priv->disp_thread[i].worker,
			"crtc_commit:%d", priv->disp_thread[i].crtc_id);
	if (IS_ERR(priv->disp_thread[i].thread)) {
		dev_err(dev->dev, "failed to create crtc_commit kthread\n");
		priv->disp_thread[i].thread = NULL;
	}

	/* initialize event thread */
	priv->event_thread[i].crtc_id = priv->crtcs[i]->base.id;
	kthread_init_worker(&priv->event_thread[i].worker);
	priv->event_thread[i].dev = dev;
	priv->event_thread[i].thread =
		kthread_run(kthread_worker_fn,
			&priv->event_thread[i].worker,
			"crtc_event:%d", priv->event_thread[i].crtc_id);
	if (IS_ERR(priv->event_thread[i].thread)) {
		dev_err(dev->dev, "failed to create crtc_event kthread\n");
		priv->event_thread[i].thread = NULL;
	}

	/* re-initialize vblank as num_crtcs changes */
	drm_vblank_cleanup(dev);
	rc = drm_vblank_init(dev, priv->num_crtcs);
	if (rc < 0)
		dev_err(dev->dev, "failed to initialize vblank\n");

	/* register components */
	if (crtc->funcs->late_register)
		crtc->funcs->late_register(crtc);
	if (encoder->funcs->late_register)
		encoder->funcs->late_register(encoder);
	drm_connector_register(connector);

	/* reset components */
	if (crtc->funcs->reset)
		crtc->funcs->reset(crtc);
	if (encoder->funcs->reset)
		encoder->funcs->reset(encoder);
	if (connector->funcs->reset)
		connector->funcs->reset(connector);

end:
	return rc;
}

static int shd_drm_base_init(struct drm_device *ddev,
		struct shd_display_base *base)
{
	struct msm_drm_private *priv;
	int rc;

	rc = shd_display_init_base_encoder(ddev, base);
	if (rc) {
		SDE_ERROR("failed to find base encoder\n");
		return rc;
	}

	rc = shd_display_init_base_connector(ddev, base);
	if (rc) {
		SDE_ERROR("failed to find base connector\n");
		return rc;
	}

	rc = shd_display_init_base_crtc(ddev, base);
	if (rc) {
		SDE_ERROR("failed to find base crtc\n");
		return rc;
	}

	if (!g_shd_kms) {
		priv = ddev->dev_private;
		g_shd_kms = kzalloc(sizeof(*g_shd_kms), GFP_KERNEL);
		g_shd_kms->funcs = *priv->kms->funcs;
		g_shd_kms->orig_funcs = priv->kms->funcs;
		g_shd_kms->funcs.atomic_check = shd_display_atomic_check;
		priv->kms->funcs = &g_shd_kms->funcs;
	}

	return rc;
}

static int shd_parse_display(struct shd_display *display)
{
	struct device_node *of_node = display->pdev->dev.of_node;
	struct device_node *of_src, *of_roi;
	u32 src_w, src_h, dst_x, dst_y, dst_w, dst_h;
	u32 range[2];
	int rc;


	display->base_of = of_parse_phandle(of_node,
		"qcom,shared-display-base", 0);
	if (!display->base_of) {
		SDE_ERROR("No base device present\n");
		rc = -ENODEV;
		goto error;
	}

	of_src = of_get_child_by_name(of_node, "qcom,shared-display-src-mode");
	if (!of_src) {
		SDE_ERROR("No src mode present\n");
		rc = -ENODEV;
		goto error;
	}

	rc = of_property_read_u32(of_src, "qcom,mode-h-active",
		&src_w);
	if (rc) {
		SDE_ERROR("Failed to parse h active\n");
		goto error;
	}

	rc = of_property_read_u32(of_src, "qcom,mode-v-active",
		&src_h);
	if (rc) {
		SDE_ERROR("Failed to parse v active\n");
		goto error;
	}

	of_roi = of_get_child_by_name(of_node, "qcom,shared-display-dst-mode");
	if (!of_roi) {
		SDE_ERROR("No roi mode present\n");
		rc = -ENODEV;
		goto error;
	}

	rc = of_property_read_u32(of_roi, "qcom,mode-x-offset",
		&dst_x);
	if (rc) {
		SDE_ERROR("Failed to parse x offset\n");
		goto error;
	}

	rc = of_property_read_u32(of_roi, "qcom,mode-y-offset",
		&dst_y);
	if (rc) {
		SDE_ERROR("Failed to parse y offset\n");
		goto error;
	}

	rc = of_property_read_u32(of_roi, "qcom,mode-width",
		&dst_w);
	if (rc) {
		SDE_ERROR("Failed to parse roi width\n");
		goto error;
	}

	rc = of_property_read_u32(of_roi, "qcom,mode-height",
		&dst_h);
	if (rc) {
		SDE_ERROR("Failed to parse roi height\n");
		goto error;
	}

	rc = of_property_read_u32_array(of_node, "qcom,blend-stage-range",
		range, 2);
	if (rc)
		SDE_ERROR("Failed to parse blend stage range\n");

	display->name = of_get_property(of_node,
		"qcom,shared-display-name", NULL);
	if (!display->name)
		display->name = of_node->full_name;

	display->src.w = src_w;
	display->src.h = src_h;
	display->roi.x = dst_x;
	display->roi.y = dst_y;
	display->roi.w = dst_w;
	display->roi.h = dst_h;
	display->stage_range.start = range[0];
	display->stage_range.size = range[1];

	SDE_DEBUG("%s src %dx%d dst %d,%d %dx%d range %d-%d\n", display->name,
		display->src.w, display->src.h,
		display->roi.x, display->roi.y,
		display->roi.w, display->roi.h,
		display->stage_range.start,
		display->stage_range.size);

	display->display_type = of_get_property(of_node,
		"qcom,display-type", NULL);
	if (!display->display_type)
		display->display_type = "unknown";

error:
	return rc;
}

static int shd_parse_base(struct shd_display_base *base)
{
	struct device_node *of_node = base->of_node;
	struct device_node *node;
	struct drm_display_mode *mode = &base->mode;
	u32 h_front_porch, h_pulse_width, h_back_porch;
	u32 v_front_porch, v_pulse_width, v_back_porch;
	bool h_active_high, v_active_high;
	u32 flags = 0;
	int rc;

	rc = of_property_read_u32(of_node, "qcom,shared-display-base-intf",
					&base->intf_idx);
	if (rc) {
		SDE_ERROR("failed to read base intf, rc=%d\n", rc);
		goto fail;
	}

	base->mst_port = of_property_read_bool(of_node,
					"qcom,shared-display-base-mst");

	node = of_get_child_by_name(of_node, "qcom,shared-display-base-mode");
	if (!node) {
		SDE_ERROR("No base mode present\n");
		rc = -ENODEV;
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-h-active",
					&mode->hdisplay);
	if (rc) {
		SDE_ERROR("failed to read h-active, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-h-front-porch",
					&h_front_porch);
	if (rc) {
		SDE_ERROR("failed to read h-front-porch, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-h-pulse-width",
					&h_pulse_width);
	if (rc) {
		SDE_ERROR("failed to read h-pulse-width, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-h-back-porch",
					&h_back_porch);
	if (rc) {
		SDE_ERROR("failed to read h-back-porch, rc=%d\n", rc);
		goto fail;
	}

	h_active_high = of_property_read_bool(node,
					"qcom,mode-h-active-high");

	rc = of_property_read_u32(node, "qcom,mode-v-active",
					&mode->vdisplay);
	if (rc) {
		SDE_ERROR("failed to read v-active, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-v-front-porch",
					&v_front_porch);
	if (rc) {
		SDE_ERROR("failed to read v-front-porch, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-v-pulse-width",
					&v_pulse_width);
	if (rc) {
		SDE_ERROR("failed to read v-pulse-width, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-v-back-porch",
					&v_back_porch);
	if (rc) {
		SDE_ERROR("failed to read v-back-porch, rc=%d\n", rc);
		goto fail;
	}

	v_active_high = of_property_read_bool(node,
					"qcom,mode-v-active-high");

	rc = of_property_read_u32(node, "qcom,mode-refresh-rate",
					&mode->vrefresh);
	if (rc) {
		SDE_ERROR("failed to read refresh-rate, rc=%d\n", rc);
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,mode-clock-in-khz",
					&mode->clock);
	if (rc) {
		SDE_ERROR("failed to read clock, rc=%d\n", rc);
		goto fail;
	}

	mode->hsync_start = mode->hdisplay + h_front_porch;
	mode->hsync_end = mode->hsync_start + h_pulse_width;
	mode->htotal = mode->hsync_end + h_back_porch;
	mode->vsync_start = mode->vdisplay + v_front_porch;
	mode->vsync_end = mode->vsync_start + v_pulse_width;
	mode->vtotal = mode->vsync_end + v_back_porch;
	if (h_active_high)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (v_active_high)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;
	mode->flags = flags;

	SDE_DEBUG("base mode h[%d,%d,%d,%d] v[%d,%d,%d,%d] %d %xH %d\n",
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal, mode->vdisplay,
		mode->vsync_start, mode->vsync_end, mode->vtotal,
		mode->vrefresh, mode->flags, mode->clock);

fail:
	return rc;
}

/**
 * sde_shd_probe - load shared display module
 * @pdev:	Pointer to platform device
 */
static int sde_shd_probe(struct platform_device *pdev)
{
	struct shd_display *shd_dev;
	struct shd_display_base *base;
	struct drm_minor *minor;
	struct drm_device *ddev;
	int ret;

	/* defer until primary drm is created */
	minor = drm_minor_acquire(0);
	if (IS_ERR(minor))
		return -EPROBE_DEFER;

	ddev = minor->dev;
	drm_minor_release(minor);
	if (!ddev)
		return -EPROBE_DEFER;

	shd_dev = devm_kzalloc(&pdev->dev, sizeof(*shd_dev), GFP_KERNEL);
	if (!shd_dev)
		return -ENOMEM;

	shd_dev->pdev = pdev;

	ret = shd_parse_display(shd_dev);
	if (ret) {
		SDE_ERROR("failed to parse shared display\n");
		goto error;
	}

	platform_set_drvdata(pdev, shd_dev);

	list_for_each_entry(base, &g_base_list, head) {
		if (base->of_node == shd_dev->base_of)
			goto next;
	}

	base = devm_kzalloc(&pdev->dev, sizeof(*base), GFP_KERNEL);
	if (!base) {
		ret = -ENOMEM;
		goto error;
	}

	INIT_LIST_HEAD(&base->disp_list);
	base->of_node = shd_dev->base_of;

	ret = shd_parse_base(base);
	if (ret) {
		SDE_ERROR("failed to parse shared display base\n");
		goto base_error;
	}

	mutex_lock(&ddev->mode_config.mutex);
	ret = shd_drm_base_init(ddev, base);
	mutex_unlock(&ddev->mode_config.mutex);
	if (ret) {
		SDE_ERROR("failed to init crtc for shared display base\n");
		goto base_error;
	}

	list_add_tail(&base->head, &g_base_list);

next:
	shd_dev->base = base;
	shd_dev->drm_dev = ddev;

	mutex_lock(&ddev->mode_config.mutex);
	ret = shd_drm_obj_init(shd_dev);
	mutex_unlock(&ddev->mode_config.mutex);
	if (ret) {
		SDE_ERROR("failed to init shared drm objects\n");
		goto error;
	}

	list_add_tail(&shd_dev->head, &base->disp_list);
	SDE_DEBUG("add shd to intf %d\n", base->intf_idx);

	return 0;

base_error:
	devm_kfree(&pdev->dev, base);
error:
	devm_kfree(&pdev->dev, shd_dev);
	return ret;
}

/**
 * sde_shd_remove - unload shared display module
 * @pdev:	Pointer to platform device
 */
static int sde_shd_remove(struct platform_device *pdev)
{
	struct shd_display *shd_dev;

	shd_dev = platform_get_drvdata(pdev);
	if (!shd_dev)
		return 0;

	list_del_init(&shd_dev->head);
	if (list_empty(&shd_dev->base->disp_list))
		list_del_init(&shd_dev->base->head);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,shared-display"},
	{},
};

static struct platform_driver sde_shd_driver = {
	.probe = sde_shd_probe,
	.remove = sde_shd_remove,
	.driver = {
		.name = "sde_shd",
		.of_match_table = dt_match,
		.suppress_bind_attrs = true,
	},
};

static int __init sde_shd_register(void)
{
	return platform_driver_register(&sde_shd_driver);
}

static void __exit sde_shd_unregister(void)
{
	platform_driver_unregister(&sde_shd_driver);
}

module_init(sde_shd_register);
module_exit(sde_shd_unregister);
