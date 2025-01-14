/*
 * Copyright (C) 2019 Allwinnertech, <liuli@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "include/eink_driver.h"
#include "include/eink_sys_source.h"
#include "lowlevel/eink_reg.h"

static struct pipe_manager *g_pipe_mgr;

inline bool is_invalid_pipe_id(struct pipe_manager *mgr, int id)
{
	return ((id < 0) || (id >= mgr->max_pipe_cnt));
}

struct pipe_manager *get_pipeline_manager(void)
{
	return g_pipe_mgr;
}

static int request_pipe(struct pipe_manager *mgr)
{
	u32 pipe_id = -1;
	unsigned long flags = 0;
	struct pipe_info_node *pipe = NULL, *temp = NULL;

	/* for pipe debug */
	if (eink_get_print_level() == 3) {
		EINK_INFO_MSG("Before Request Pipe\n");
		print_free_pipe_list(mgr);
		print_used_pipe_list(mgr);
	}

	spin_lock_irqsave(&mgr->list_lock, flags);

	if (list_empty(&mgr->pipe_free_list)) {
		pr_err("There is no free pipe!\n");
		spin_unlock_irqrestore(&mgr->list_lock, flags);
		return -1;
	}

	list_for_each_entry_safe(pipe, temp, &mgr->pipe_free_list, node) {
		pipe_id = pipe->pipe_id;

		list_move_tail(&pipe->node, &mgr->pipe_used_list);
		break;
	}

	spin_unlock_irqrestore(&mgr->list_lock, flags);

	/* for pipe debug */
	if (eink_get_print_level() == 3) {
		EINK_INFO_MSG("After Request Pipe\n");
		print_free_pipe_list(mgr);
		print_used_pipe_list(mgr);
	}

	EINK_DEBUG_MSG("request pipe_id = %d\n", pipe_id);
	return pipe_id;
}

static int config_pipe(struct pipe_manager *mgr, struct pipe_info_node info)
{
	int ret = 0;
	unsigned long flags = 0;
	struct pipe_info_node *pipe = NULL, *temp = NULL;
	u32 pipe_id = 0;

	pipe_id = info.pipe_id;

	if (pipe_id >= mgr->max_pipe_cnt) {
		pr_err("%s:pipe is invalied!\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&mgr->list_lock, flags);
	list_for_each_entry_safe(pipe, temp, &mgr->pipe_used_list, node) {
		if (pipe->pipe_id != pipe_id) {
			continue;
		}
		memcpy(&pipe->upd_win, &info.upd_win, sizeof(struct upd_win));
		pipe->img = info.img;
		pipe->wav_paddr = info.wav_paddr;
		pipe->wav_vaddr = info.wav_vaddr;
		pipe->total_frames = info.total_frames;
		pipe->upd_mode = info.upd_mode;
		pipe->fresh_frame_cnt = 0;
		pipe->dec_frame_cnt = 0;

		if (info.img->upd_all_en == true)
			eink_set_upd_all_en(1);
		else
			eink_set_upd_all_en(0);
		eink_pipe_config(pipe);
		eink_pipe_config_wavefile(pipe->wav_paddr, pipe->pipe_id);

		EINK_INFO_MSG("config Pipe_id=%d, UPD:(%d,%d)~(%d,%d), wav addr = 0x%lx, total frames=%d, upd_all_en = %d\n",
				pipe->pipe_id,
				pipe->upd_win.left, pipe->upd_win.top,
				pipe->upd_win.right, pipe->upd_win.bottom,
				pipe->wav_paddr, pipe->total_frames,
				info.img->upd_all_en);
		break;
	}

	spin_unlock_irqrestore(&mgr->list_lock, flags);

	return ret;
}

static int active_pipe(struct pipe_manager *mgr, u32 pipe_no)
{
	struct pipe_info_node *pipe = NULL, *tmp_pipe = NULL;
	unsigned long flags = 0;
	unsigned long tframes = 0;
#ifdef OFFLINE_SINGLE_MODE
	unsigned int tmp_total = 0, tmp_dec_cnt = 0, tmp_fresh_cnt = 0, tmp_cur_total = 0;
#endif
	int ret = -1;

	if (pipe_no >= mgr->max_pipe_cnt) {
		pr_err("%s:pipe is invalied!\n", __func__);
		return -EINVAL;
	}

	spin_lock_irqsave(&mgr->list_lock, flags);
	list_for_each_entry_safe(pipe, tmp_pipe, &mgr->pipe_used_list, node) {
		if (pipe->pipe_id != pipe_no)
			continue;
		if (pipe->active_flag == true) {
			pr_warn("%s:The pipe has already active!\n", __func__);
			spin_unlock_irqrestore(&mgr->list_lock, flags);
			return 0;
		} else {
			eink_pipe_enable(pipe->pipe_id);
			pipe->active_flag = true;
			tframes = pipe->total_frames;
			EINK_DEBUG_MSG("Enable an new pipe id = %d, pipe total frames=%lu\n", pipe->pipe_id, tframes);
			ret = 0;
			break;
		}
	}

	if (tframes == 0) {
		pr_err("%s:no pipe or total_frame of pipe %d is 0!\n", __func__, pipe_no);
		ret = -1;
		spin_unlock_irqrestore(&mgr->list_lock, flags);
		return ret;
	}

	spin_unlock_irqrestore(&mgr->list_lock, flags);
#ifdef OFFLINE_SINGLE_MODE
	spin_lock_irqsave(&mgr->frame_lock, flags);
	tmp_total = mgr->all_total_frames;
	tmp_fresh_cnt = mgr->all_fresh_frame_cnt;
	tmp_dec_cnt = mgr->all_dec_frame_cnt;
	if (((tmp_fresh_cnt + 1) < tmp_total) && (tmp_fresh_cnt != 0)) {
		mgr->all_total_frames +=  tframes;
		ret = 0;
	} else {
		if ((tmp_fresh_cnt == 0) || (tmp_fresh_cnt == tmp_total)) {
			EINK_INFO_MSG("Fresh is zero may begin or fresh = total, fresh=%d\n", tmp_fresh_cnt);
			mgr->all_total_frames = tframes;
			mgr->all_fresh_frame_cnt = 0;
			mgr->all_dec_frame_cnt = 0;
			ret = 0;
		} else {
			/*fix here
			 *TODO: too late to insert, pls wait power off
			 *mgr->list.all_total_frames = tframes;
			 *mgr->list.all_fresh_frame_cnt = 0;
			 *mgr->list.all_dec_frame_cnt = 0;
			*/
			ret = -4;
			EINK_INFO_MSG("inserting pipe(%d) is not fast, total=%d, fresh=%d, decode=%d\n",
					pipe->pipe_id, tmp_total, tmp_fresh_cnt, tmp_dec_cnt);
		}
	}
	tmp_cur_total = mgr->all_total_frames;
	spin_unlock_irqrestore(&mgr->frame_lock, flags);

	EINK_INFO_MSG("active one pipe(%d), last_total=%d, dec=%d, fresh=%d, cur_total=%d\n",
			pipe->pipe_id, tmp_total, tmp_dec_cnt, tmp_fresh_cnt, tmp_cur_total);
#endif
	return ret;
}

static void release_pipe(struct pipe_manager *mgr, struct pipe_info_node *pipe)
{
	/* for pipe debug */
	if (eink_get_print_level() == 3) {
		EINK_INFO_MSG("Before Config Pipe\n");
		print_free_pipe_list(mgr);
		print_used_pipe_list(mgr);
	}
	/* hardware disable this pipe self */
	/* eink_pipe_disable(pipe->pipe_id);*/

	/* initial and add to free list */
	pipe->active_flag = false;
	pipe->dec_frame_cnt = 0;
	pipe->fresh_frame_cnt = 0;
	pipe->total_frames = 0;

	if (eink_get_print_level() == 3) {
		EINK_INFO_MSG("After Config Pipe\n");
		print_free_pipe_list(mgr);
		print_used_pipe_list(mgr);
	}

	EINK_DEBUG_MSG("release pipe_id=%d\n", pipe->pipe_id);

	return;
}

void reset_all_pipe(struct pipe_manager *mgr)
{
	struct pipe_info_node *pipe_node = NULL, *tmp_node = NULL;
	unsigned long flags = 0;

	if (mgr == NULL) {
		pr_err("%s:pipe mgr is NULL!\n", __func__);
		return;
	}

	spin_lock_irqsave(&mgr->list_lock, flags);
	if (!list_empty(&mgr->pipe_used_list)) {
		list_for_each_entry_safe(pipe_node, tmp_node, &mgr->pipe_used_list, node) {
			eink_pipe_disable(pipe_node->pipe_id);
			release_pipe(mgr, pipe_node);
			list_move_tail(&pipe_node->node, &mgr->pipe_free_list);

		}
	}
	spin_unlock_irqrestore(&mgr->list_lock, flags);
	EINK_INFO_MSG("finish!\n");

	return;
}

u64 get_free_pipe_state(struct pipe_manager *mgr)
{
	struct pipe_info_node *pipe_node = NULL, *tmp_node = NULL;
	u64 state = 0;
	unsigned long flags = 0;

	if (mgr == NULL) {
		pr_err("%s:pipe mgr is NULL!\n", __func__);
		return 0;
	}

	spin_lock_irqsave(&mgr->list_lock, flags);
	list_for_each_entry_safe(pipe_node, tmp_node, &mgr->pipe_free_list, node) {
		state = state | (1ULL << pipe_node->pipe_id);
	}
	spin_unlock_irqrestore(&mgr->list_lock, flags);

	return state;
}

int pipeline_mgr_enable(struct pipe_manager *mgr)
{
	int ret = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&mgr->list_lock, flags);
	if (mgr->enable_flag == true) {
		EINK_DEBUG_MSG("pipe mgr already enable!\n");
		spin_unlock_irqrestore(&mgr->list_lock, flags);
		return 0;
	}

	eink_set_out_panel_mode(&mgr->panel_info);
	eink_set_data_reverse();/* ------------ big small edian*/
	eink_irq_enable();

	mgr->enable_flag = true;
	spin_unlock_irqrestore(&mgr->list_lock, flags);

	return ret;
}

static int pipeline_mgr_disable(void)
{
	struct pipe_manager *mgr = g_pipe_mgr;
	unsigned long flags = 0;

	if (mgr == NULL) {
		pr_err("%s:mgr is null!\n", __func__);
		return -EINVAL;
	}
	spin_lock_irqsave(&mgr->list_lock, flags);
	if (mgr->enable_flag == false) {
		spin_unlock_irqrestore(&mgr->list_lock, flags);
		return 0;
	}
	/* ee start clean hw self*/

	mgr->enable_flag = false;
	spin_unlock_irqrestore(&mgr->list_lock, flags);

	return 0;
}

#ifdef OFFLINE_SINGLE_MODE
static int refresh_pipe_data(struct pipe_manager *mgr)
{
	u32 bit_num = 0, step = 0;
	unsigned long flags = 0;
	struct pipe_info_node *pipe = NULL, *temp = NULL;

	bit_num = mgr->panel_info.bit_num;
	step = bit_num << 1; /* one frame  4bit panel 256, 5bit 1024*/

	EINK_INFO_MSG("REFRESH DATA input!\n");

	/* for debug */
	if (eink_get_print_level() == 3) {
		EINK_INFO_MSG("Before Refresh Pipe\n");
		print_used_pipe_list(mgr);
	}

	spin_lock_irqsave(&mgr->list_lock, flags);
	list_for_each_entry_safe(pipe, temp, &mgr->pipe_used_list, node) {
		/*pipe->dec_frame_cnt++;*//* irq proc dec_cnt = total_frame */
		/* hardware will self plus 1,wav addr find by himself */
		/* pipe finish disable self software neednt disable it */
		pipe->dec_frame_cnt = eink_get_dec_cnt(pipe->pipe_id);

		EINK_INFO_MSG("[Pipe %d] dec_frame_cnt = %d, total = %d, step = %d\n",
				pipe->pipe_id, pipe->dec_frame_cnt, pipe->total_frames, step);
	}
	spin_unlock_irqrestore(&mgr->list_lock, flags);
	return 0;
}

static int write_edma(struct pipe_manager *mgr)
{
	int ret = 0;
	unsigned long wav_paddr = 0;

	wav_paddr = (unsigned long)request_buffer_for_display(&mgr->wavedata_ring_buffer);
	if (wav_paddr == 0) {
		ret = -EBUSY;
		return ret;
	}

	EINK_INFO_MSG("EDMA wav paddr=0x%x\n", (unsigned int)wav_paddr);
	eink_edma_cfg(&mgr->panel_info);
	eink_edma_set_wavaddr(wav_paddr);
	eink_edma_start();

	return ret;
}

static void eink_decode_proc(struct pipe_manager *mgr)
{
	unsigned long flags = 0;
	unsigned long dec_frames = 0, total_frames = 0, fresh_frames = 0;
	u32 wavbuf_fail_cnt = 0;
	void *wav_paddr = NULL;
	void *wav_vaddr = NULL;

	spin_lock_irqsave(&mgr->frame_lock, flags);
	mgr->all_dec_frame_cnt++;
	dec_frames = mgr->all_dec_frame_cnt;
	fresh_frames = mgr->all_fresh_frame_cnt;
	total_frames = mgr->all_total_frames;
	spin_unlock_irqrestore(&mgr->frame_lock, flags);

	EINK_INFO_MSG("dec_frames=%ld, total_frames=%ld\n", dec_frames, total_frames);
	if (dec_frames >= total_frames) {/* fix me */
		mgr->ee_processing = false;
		return;
	}

	/* pre-decode PRE_DECODE_NUM frames for EDMA */
	if (dec_frames == PRE_DECODE_NUM) {
		write_edma(mgr);
	}

	do {
		wav_paddr = request_buffer_for_decode(&mgr->wavedata_ring_buffer, &wav_vaddr);
		if (wav_paddr == NULL) {
			wavbuf_fail_cnt++;
			if (wavbuf_fail_cnt >= 200) {
				wavbuf_fail_cnt = 0;
				pr_err("[%s]LINE:%d:no wavedata buf to decode!\n", __func__, __LINE__);
				break;
			}
			msleep(5);
		}
	} while (wav_paddr == NULL);

	EINK_INFO_MSG("dec wav vaddr=0x%p, paddr=0x%p\n", wav_vaddr, wav_paddr);

	if (wav_paddr == NULL)
		return;

	eink_prepare_decode((unsigned long)wav_paddr, &mgr->panel_info);
	eink_start();

	return;
}

static void eink_decode_ctrl_task(struct work_struct *work)
{
	struct pipe_manager *mgr = g_pipe_mgr;

	EINK_INFO_MSG("DECODE TASK input!\n");
	if (mgr == NULL) {
		pr_err("%s:pipe mgr is NULL", __func__);
		return;
	}
	/* 记住可能要加timer */

	refresh_pipe_data(mgr);

	/* push the last dec frame to edma tansfer to disp */
	queue_wavedata_buffer(&mgr->wavedata_ring_buffer);

	eink_decode_proc(mgr);
	return;
}

static void edma_transfer_task(struct work_struct *work)
{
	unsigned long flags = 0;
	u32 fresh_cnt = 0, decode_cnt = 0, total_frame = 0;
	int ret = 0, edma_err_cnt = 0;

	struct pipe_manager *mgr = g_pipe_mgr;

	spin_lock_irqsave(&mgr->frame_lock, flags);
	total_frame = mgr->all_total_frames;
	decode_cnt = mgr->all_dec_frame_cnt;
	mgr->all_fresh_frame_cnt++;
	fresh_cnt = mgr->all_fresh_frame_cnt;

	if (total_frame == 0) {
		mgr->all_fresh_frame_cnt = 0;
		spin_unlock_irqrestore(&mgr->frame_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&mgr->frame_lock, flags);

	EINK_INFO_MSG("Fresh_cnt=%d, Total_frame=%d\n", fresh_cnt, total_frame);

	if ((fresh_cnt == total_frame)) {
		/* clean_used_wavedata_buffer(&mgr->wavedata_ring_buffer); */
		mgr->ee_processing = false;

	} else if (fresh_cnt < total_frame) {/* fresh_cnt + 1? */
		do {
			ret = write_edma(mgr);
			if (ret < 0) {
				edma_err_cnt++;
				if (edma_err_cnt > 200) {
					EINK_INFO_MSG("NO Wavdata for edma!\n");
					edma_err_cnt = 0;
				}
				msleep(5);

			}
		} while (ret < 0);

	}
	clean_used_wavedata_buffer(&mgr->wavedata_ring_buffer);

	return;
}
#endif

#ifdef DEC_WAV_DEBUG
extern int eink_edma_wb_addr(unsigned long addr);
extern int eink_edma_wb_en(int en);
extern int eink_edma_wb_frm_cnt(unsigned int frm_cnt);

static int wav_calc_data_len(int total_cnt)
{
	u32 data_len = 0;
	u32 vsync = 0, hsync = 0;
	struct eink_manager *mgr = get_eink_manager();
	if (mgr == NULL) {
		pr_err("%s, eink mgr is NULL\n", __func__);
		return -1;
	}

	hsync = mgr->timing_info.lbl + mgr->timing_info.lsl + mgr->timing_info.ldl + mgr->timing_info.lel;
	vsync = mgr->timing_info.fbl + mgr->timing_info.fsl + mgr->timing_info.fdl + mgr->timing_info.fel;

	if (mgr->panel_info.data_len == 8) {
		data_len = hsync * vsync * (1 + 1);/* 8bit timing, 8bit wav */
	} else {
		data_len = hsync * vsync * (2 + 1);/* 8bit timing, 16bit wav */
	}

	data_len = data_len * total_cnt;
	EINK_INFO_MSG("data_len = %d, total_cnt = %d hsync = %d, vsync = %d\n",
						data_len, total_cnt, hsync, vsync);

	return data_len;
}

static int __pipe_mgr_config_wb(struct pipe_manager *mgr, struct pipe_info_node *info)
{
	if (!mgr || !info) {
		return -1;
	}

	if (mgr->dec_wav_vaddr) {
		pr_err("dec_wav_vaddr is not null! try to free\n");
		eink_free((void *)mgr->dec_wav_vaddr, (void *)mgr->wav_wb_paddr, mgr->wav_len);
	}

	mgr->wav_len = wav_calc_data_len(info->total_frames);
	if (!mgr->wav_len) {
		pr_err("Zero length:%u %u\n", mgr->wav_len, info->total_frames);
		return -2;
	}
	mgr->wav_wb_vaddr = eink_malloc(mgr->wav_len, &mgr->wav_wb_paddr);
	if (mgr->wav_wb_vaddr == NULL) {
		pr_err("%s:fail to alloc mem for dec wave, len=%d\n", __func__, mgr->wav_len);
		return -ENOMEM;
	}

	eink_edma_wb_addr((unsigned long)mgr->wav_wb_paddr);
	eink_edma_wb_frm_cnt(0);
	eink_edma_wb_en(1);
	return 0;
}

static int __pipe_mgr_dump_wav_data(struct pipe_manager *mgr)
{
	char file_name[256] = {0};
	static u32 id;

	if (!mgr) {
		return -1;
	}

	sprintf(file_name, "/data/dec_wav%d.bin", id);
	pr_err("save wavedata:%s \n", file_name);

	save_as_bin_file((u8 *)mgr->wav_wb_vaddr, file_name, mgr->wav_len, 0);
	id++;
	eink_edma_wb_en(0);
	eink_free((void *)mgr->dec_wav_vaddr, (void *)mgr->wav_wb_paddr, mgr->wav_len);
	mgr->dec_wav_vaddr = NULL;
	mgr->wav_wb_paddr = NULL;
	mgr->wav_len = 0;
	return 0;
}


void wav_dbg_work_tasket(struct work_struct *work)
{
	struct pipe_manager *pipe_mgr =
		container_of(work, struct pipe_manager, wav_dbg_work);
	if (pipe_mgr) {
		pipe_mgr->pipe_mgr_dump_wav_data(pipe_mgr);
	}
	return;
}
#endif

int pipe_mgr_init(struct eink_manager *eink_mgr)
{
	int ret = 0, i = 0;
	struct pipe_manager *pipe_mgr = NULL;
	struct eink_panel_info  *panel_info = NULL;
	struct timing_info *timing = NULL;
	struct pipe_info_node **pipe_node = NULL;

	pipe_mgr = (struct pipe_manager *)kmalloc(sizeof(struct pipe_manager), GFP_KERNEL | __GFP_ZERO);
	if (pipe_mgr == NULL) {
		pr_err("pipe mgr kmalloc failed!\n");
		ret = -ENOMEM;
		goto pipe_mgr_err;
	}

	memset((void *)pipe_mgr, 0, sizeof(struct pipe_manager));
	panel_info = &eink_mgr->panel_info;
	timing = &eink_mgr->timing_info;

	if (panel_info->bit_num == 4) {
		pipe_mgr->max_pipe_cnt = 64;
	} else if (panel_info->bit_num == 5) {
		pipe_mgr->max_pipe_cnt = 31;
	}

	memcpy(&pipe_mgr->panel_info, &eink_mgr->panel_info, sizeof(struct eink_panel_info));
	spin_lock_init(&pipe_mgr->list_lock);
#ifdef OFFLINE_SINGLE_MODE
	spin_lock_init(&pipe_mgr->frame_lock);
	pipe_mgr->dec_workqueue = alloc_workqueue("EINK_DECODE_WORK",
			WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	pipe_mgr->edma_workqueue = alloc_workqueue("EINK_EDMA_WORK",
			WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	INIT_WORK(&pipe_mgr->decode_ctrl_work, eink_decode_ctrl_task);
	INIT_WORK(&pipe_mgr->edma_ctrl_work, edma_transfer_task);
	pipe_mgr->all_total_frames = 0;
	pipe_mgr->all_dec_frame_cnt = 0;
	pipe_mgr->all_fresh_frame_cnt = 0;
#endif
	INIT_LIST_HEAD(&pipe_mgr->pipe_free_list);
	INIT_LIST_HEAD(&pipe_mgr->pipe_used_list);

#ifdef DEC_WAV_DEBUG
	pipe_mgr->wav_dbg_workqueue = alloc_workqueue("EINK_WAVE_DEBUG",
			WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	INIT_WORK(&pipe_mgr->wav_dbg_work, wav_dbg_work_tasket);

#endif
	pipe_node =
		(struct pipe_info_node **)kmalloc(pipe_mgr->max_pipe_cnt * sizeof(struct pipe_info_node *), GFP_KERNEL | __GFP_ZERO);

	if (pipe_node == NULL) {
		pr_err("pipe node ** malloc failed!\n");
		ret = -ENOMEM;
		goto pipe_node_err;
	}
	memset((void *)pipe_node, 0, pipe_mgr->max_pipe_cnt * sizeof(struct pipe_info_node *));

	for (i = 0; i < pipe_mgr->max_pipe_cnt; i++) {
		pipe_node[i] = (struct pipe_info_node *)kmalloc(sizeof(struct pipe_info_node), GFP_KERNEL | __GFP_ZERO);
		if (pipe_node[i] == NULL) {
			pr_err("%s:malloc pipe failed!\n", __func__);
			ret = -ENOMEM;
			goto err_out;
		}
		memset((void *)pipe_node[i], 0, sizeof(struct pipe_info_node));
		pipe_node[i]->pipe_id = i;
		pipe_node[i]->active_flag = false;

		list_add_tail(&pipe_node[i]->node, &pipe_mgr->pipe_free_list);
	}

#ifdef OFFLINE_SINGLE_MODE
	ret = init_dec_wav_buffer(&pipe_mgr->wavedata_ring_buffer, panel_info, timing);
	if (ret) {
		pr_err("%s: init_wavedata_buf err!\n", __func__);
		goto err_out;
	}
#endif
	pipe_mgr->pipe_mgr_enable = pipeline_mgr_enable;
	pipe_mgr->pipe_mgr_disable = pipeline_mgr_disable;
	pipe_mgr->get_free_pipe_state = get_free_pipe_state;
	pipe_mgr->request_pipe = request_pipe;
	pipe_mgr->config_pipe = config_pipe;
	pipe_mgr->active_pipe = active_pipe;
	pipe_mgr->release_pipe = release_pipe;
	pipe_mgr->reset_all_pipe = reset_all_pipe;
#ifdef DEC_WAV_DEBUG
	pipe_mgr->pipe_mgr_config_wb = __pipe_mgr_config_wb;
	pipe_mgr->pipe_mgr_dump_wav_data = __pipe_mgr_dump_wav_data;
#endif

	eink_mgr->pipe_mgr = pipe_mgr;
	g_pipe_mgr = pipe_mgr;

	return 0;
err_out:
	for (i = 0; i < pipe_mgr->max_pipe_cnt; i++) {
		kfree(pipe_node[i]);
	}
pipe_node_err:
	kfree(pipe_node);
pipe_mgr_err:
	kfree(pipe_mgr);

	return ret;
}
