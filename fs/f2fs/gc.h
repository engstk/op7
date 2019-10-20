// SPDX-License-Identifier: GPL-2.0
/*
 * fs/f2fs/gc.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
#define GC_THREAD_MIN_WB_PAGES		1	/*
						 * a threshold to determine
						 * whether IO subsystem is idle
						 * or not
						 */
#define DEF_GC_THREAD_URGENT_SLEEP_TIME	500	/* 500 ms */
#define DEF_GC_THREAD_MIN_SLEEP_TIME	2000	/* milliseconds */
#define DEF_GC_THREAD_MAX_SLEEP_TIME	30000
#define DEF_GC_THREAD_NOGC_SLEEP_TIME	600000	/* wait 10 min */
#define LIMIT_FREE_BLOCK	75
#define LIMIT_URGENT_FREE_BLOCK	30
#define LIMIT_INVALID_BLOCK	3

#define DEF_GC_FAILED_PINNED_FILES	2048

/* Search max. number of dirty segments to select a victim segment */
#define DEF_MAX_VICTIM_SEARCH 4096 /* covers 8GB */

struct f2fs_gc_kthread {
	struct task_struct *f2fs_gc_task;
	wait_queue_head_t gc_wait_queue_head;

	/* for gc sleep time */
	unsigned int urgent_sleep_time;
	unsigned int min_sleep_time;
	unsigned int max_sleep_time;
	unsigned int no_gc_sleep_time;

	/* for changing gc mode */
	unsigned int gc_wake;
};

struct gc_inode_list {
	struct list_head ilist;
	struct radix_tree_root iroot;
};

/*
 * inline functions
 */
static inline block_t free_user_blocks(struct f2fs_sb_info *sbi)
{
	if (free_segments(sbi) < overprovision_segments(sbi))
		return 0;
	else
		return (free_segments(sbi) - overprovision_segments(sbi))
			<< sbi->log_blocks_per_seg;
}

static inline block_t limit_invalid_user_blocks(struct f2fs_sb_info *sbi)
{
	return (long)(sbi->user_block_count * LIMIT_INVALID_BLOCK) / 100;
}

static inline block_t limit_free_user_blocks(struct f2fs_sb_info *sbi, int percentage)
{
	block_t reclaimable_user_blocks = sbi->user_block_count -
		written_block_count(sbi);
	return (long)(reclaimable_user_blocks * percentage) / 100;
}

static inline bool has_enough_invalid_blocks(struct f2fs_sb_info *sbi)
{
	static int _2si_gc = 0;

	if (_2si_gc && free_user_blocks(sbi) < limit_free_user_blocks(sbi, 80))
		return true;

	if (free_user_blocks(sbi) < limit_free_user_blocks(sbi, LIMIT_FREE_BLOCK)) {
		_2si_gc = 1;
		return true;
	}

	_2si_gc = 0;
	return false;
}

static inline bool has_many_invalid_blocks(struct f2fs_sb_info *sbi)
{
	block_t invalid_user_blocks = sbi->user_block_count -
					written_block_count(sbi);

	if (invalid_user_blocks < limit_invalid_user_blocks(sbi))
		return false;

	if (free_user_blocks(sbi) < limit_free_user_blocks(sbi, LIMIT_URGENT_FREE_BLOCK))
		return true;

	return false;
}
