/*
 *  drivers/cpufreq/cpufreq_alucard.c
 *
 *  Copyright (C)  2011 Samsung Electronics co. ltd
 *    ByungChang Cha <bc.cha@samsung.com>
 *
 *  Based on ondemand governor
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * Created by Alucard_24@xda
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/slab.h>
/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

static void do_alucard_timer(struct work_struct *work);

struct cpufreq_alucard_cpuinfo {
	u64 prev_cpu_wall;
	u64 prev_cpu_idle;
	struct cpufreq_frequency_table *freq_table;
	struct delayed_work work;
	struct cpufreq_policy *cur_policy;
#if 0
	ktime_t time_stamp;
#endif
	int cpu;
	unsigned int enable:1;
	/*
	 * mutex that serializes governor limit change with
	 * do_alucard_timer invocation. We do not want do_alucard_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};

static DEFINE_PER_CPU(struct cpufreq_alucard_cpuinfo, od_alucard_cpuinfo);

static struct workqueue_struct *alucard_wq;

static unsigned int alucard_enable;	/* number of CPUs using this policy */
/*
 * alucard_mutex protects alucard_enable in governor start/stop.
 */
static DEFINE_MUTEX(alucard_mutex);

/* alucard tuners */
static struct alucard_tuners {
	unsigned int sampling_rate;
	int inc_cpu_load_at_min_freq;
	int inc_cpu_load;
	int dec_cpu_load_at_min_freq;
	int dec_cpu_load;
	int freq_responsiveness;
	int pump_inc_step;
	int pump_dec_step;
} alucard_tuners_ins = {
	.sampling_rate = 60000,
	.inc_cpu_load_at_min_freq = 60,
	.inc_cpu_load = 80,
	.dec_cpu_load_at_min_freq = 40,
	.dec_cpu_load = 60,
	.freq_responsiveness = 960000,
	.pump_inc_step = 2,
	.pump_dec_step = 1,

};

/************************** sysfs interface ************************/

/* cpufreq_alucard Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%d\n", alucard_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(inc_cpu_load_at_min_freq, inc_cpu_load_at_min_freq);
show_one(inc_cpu_load, inc_cpu_load);
show_one(dec_cpu_load_at_min_freq, dec_cpu_load_at_min_freq);
show_one(dec_cpu_load, dec_cpu_load);
show_one(freq_responsiveness, freq_responsiveness);
show_one(pump_inc_step, pump_inc_step);
show_one(pump_dec_step, pump_dec_step);

/* sampling_rate */
static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(input,10000);
	
	if (input == alucard_tuners_ins.sampling_rate)
		return count;

	alucard_tuners_ins.sampling_rate = input;

	return count;
}

/* inc_cpu_load_at_min_freq */
static ssize_t store_inc_cpu_load_at_min_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		return -EINVAL;
	}

	input = min(input,alucard_tuners_ins.inc_cpu_load);

	if (input == alucard_tuners_ins.inc_cpu_load_at_min_freq)
		return count;

	alucard_tuners_ins.inc_cpu_load_at_min_freq = input;

	return count;
}

/* inc_cpu_load */
static ssize_t store_inc_cpu_load(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input,100),0);

	if (input == alucard_tuners_ins.inc_cpu_load)
		return count;

	alucard_tuners_ins.inc_cpu_load = input;

	return count;
}

/* dec_cpu_load_at_min_freq */
static ssize_t store_dec_cpu_load_at_min_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1) {
		return -EINVAL;
	}

	input = min(input,alucard_tuners_ins.dec_cpu_load);

	if (input == alucard_tuners_ins.dec_cpu_load_at_min_freq)
		return count;

	alucard_tuners_ins.dec_cpu_load_at_min_freq = input;

	return count;
}

/* dec_cpu_load */
static ssize_t store_dec_cpu_load(struct kobject *a, struct attribute *b,
					const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input,95),5);

	if (input == alucard_tuners_ins.dec_cpu_load)
		return count;

	alucard_tuners_ins.dec_cpu_load = input;

	return count;
}

/* freq_responsiveness */
static ssize_t store_freq_responsiveness(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	if (input == alucard_tuners_ins.freq_responsiveness)
		return count;

	alucard_tuners_ins.freq_responsiveness = input;

	return count;
}

/* pump_inc_step */
static ssize_t store_pump_inc_step(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input,3),1);

	if (input == alucard_tuners_ins.pump_inc_step)
		return count;

	alucard_tuners_ins.pump_inc_step = input;

	return count;
}

/* pump_dec_step */
static ssize_t store_pump_dec_step(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	int input;
	int ret;

	ret = sscanf(buf, "%d", &input);
	if (ret != 1)
		return -EINVAL;

	input = max(min(input,3),1);

	if (input == alucard_tuners_ins.pump_dec_step)
		return count;

	alucard_tuners_ins.pump_dec_step = input;

	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(inc_cpu_load_at_min_freq);
define_one_global_rw(inc_cpu_load);
define_one_global_rw(dec_cpu_load_at_min_freq);
define_one_global_rw(dec_cpu_load);
define_one_global_rw(freq_responsiveness);
define_one_global_rw(pump_inc_step);
define_one_global_rw(pump_dec_step);

static struct attribute *alucard_attributes[] = {
	&sampling_rate.attr,
	&inc_cpu_load_at_min_freq.attr,
	&inc_cpu_load.attr,
	&dec_cpu_load_at_min_freq.attr,
	&dec_cpu_load.attr,
	&freq_responsiveness.attr,
	&pump_inc_step.attr,
	&pump_dec_step.attr,
	NULL
};

static struct attribute_group alucard_attr_group = {
	.attrs = alucard_attributes,
	.name = "alucard",
};

/************************** sysfs end ************************/

#if 0
/* Will return if we need to evaluate cpu load again or not */
static inline bool need_load_eval(struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo,
		unsigned int sampling_rate)
{
	ktime_t time_now = ktime_get();
	s64 delta_us = ktime_us_delta(time_now, this_alucard_cpuinfo->time_stamp);

	/* Do nothing if we recently have sampled */
	if (delta_us < (s64)(sampling_rate / 2))
		return false;
	else
		this_alucard_cpuinfo->time_stamp = time_now;

	return true;
}
#endif

static void alucard_check_cpu(struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo)
{
	struct cpufreq_policy *cpu_policy;
	unsigned int min_freq;
	unsigned int max_freq;
	unsigned int freq_responsiveness = alucard_tuners_ins.freq_responsiveness;
	int dec_cpu_load = alucard_tuners_ins.dec_cpu_load;
	int inc_cpu_load = alucard_tuners_ins.inc_cpu_load;
	int pump_inc_step = alucard_tuners_ins.pump_inc_step;
	int pump_dec_step = alucard_tuners_ins.pump_dec_step;
	u64 cur_wall_time, cur_idle_time;
	unsigned int wall_time, idle_time;
	unsigned int index = 0;
	unsigned int tmp_freq = 0;
	unsigned int next_freq = 0;
	int cur_load = -1;
	unsigned int cpu;
	
	cpu = this_alucard_cpuinfo->cpu;
	cpu_policy = this_alucard_cpuinfo->cur_policy;

	cur_idle_time = get_cpu_idle_time(cpu, &cur_wall_time, 0);

	wall_time = (unsigned int)
			(cur_wall_time - this_alucard_cpuinfo->prev_cpu_wall);
	this_alucard_cpuinfo->prev_cpu_wall = cur_wall_time;

	idle_time = (unsigned int)
			(cur_idle_time - this_alucard_cpuinfo->prev_cpu_idle);
	this_alucard_cpuinfo->prev_cpu_idle = cur_idle_time;

	if (!cpu_policy)
		return;

	/*printk(KERN_ERR "TIMER CPU[%u], wall[%u], idle[%u]\n",cpu, wall_time, idle_time);*/
	if (wall_time >= idle_time) { /*if wall_time < idle_time, evaluate cpu load next time*/
		cur_load = wall_time > idle_time ? (100 * (wall_time - idle_time)) / wall_time : 1;/*if wall_time is equal to idle_time cpu_load is equal to 1*/

		cpufreq_notify_utilization(cpu_policy, cur_load);

		/* Checking Frequency Limit */
		min_freq = cpu_policy->min;
		/* Maximum increasing frequency possible */
		max_freq = max(min(cur_load * (cpu_policy->max / 100), cpu_policy->max), min_freq);

		/* CPUs Online Scale Frequency*/
		if (cpu_policy->cur < freq_responsiveness) {
			inc_cpu_load = alucard_tuners_ins.inc_cpu_load_at_min_freq;
			dec_cpu_load = alucard_tuners_ins.dec_cpu_load_at_min_freq;
		}		
		/* Check for frequency increase or for frequency decrease */
		if (cur_load >= inc_cpu_load && cpu_policy->cur < max_freq) {
			tmp_freq = min(cpu_policy->cur + (pump_inc_step * 108000), max_freq);
		} else if (cur_load < dec_cpu_load && cpu_policy->cur > min_freq) {
			tmp_freq = max(cpu_policy->cur - (pump_dec_step * 108000), min_freq);
		} else {
			/* if cpu frequency is already at maximum or minimum or cur_load is between inc_cpu_load and dec_cpu_load var, we don't need to set frequency! */
			return;
		}
		cpufreq_frequency_table_target(cpu_policy, this_alucard_cpuinfo->freq_table, tmp_freq,
			CPUFREQ_RELATION_L, &index);
	 	next_freq = this_alucard_cpuinfo->freq_table[index].frequency;
		/*printk(KERN_ERR "FREQ CALC.: CPU[%u], load[%d], target freq[%u], cur freq[%u], min freq[%u], max_freq[%u]\n",cpu, cur_load, next_freq, cpu_policy->cur, cpu_policy->min, max_freq);*/
		if (next_freq != cpu_policy->cur) {
			__cpufreq_driver_target(cpu_policy, next_freq, CPUFREQ_RELATION_L);
		}
	}

}

static void do_alucard_timer(struct work_struct *work)
{
	struct cpufreq_alucard_cpuinfo *alucard_cpuinfo;
	unsigned int sampling_rate;
	int delay;
	unsigned int cpu;

	alucard_cpuinfo = container_of(work, struct cpufreq_alucard_cpuinfo, work.work);
	cpu = alucard_cpuinfo->cpu;

	mutex_lock(&alucard_cpuinfo->timer_mutex);

	sampling_rate = alucard_tuners_ins.sampling_rate;
	delay = usecs_to_jiffies(sampling_rate);
	/* We want all CPUs to do sampling nearly on
	 * same jiffy
	 */
	if (num_online_cpus() > 1) {
		delay -= jiffies % delay;
	}

#if 0
	if (need_load_eval(alucard_cpuinfo, sampling_rate))
#endif
		alucard_check_cpu(alucard_cpuinfo);

	queue_delayed_work_on(cpu, alucard_wq, &alucard_cpuinfo->work, delay);
	mutex_unlock(&alucard_cpuinfo->timer_mutex);
}

static int cpufreq_governor_alucard(struct cpufreq_policy *policy,
				unsigned int event)
{
	unsigned int cpu;
	struct cpufreq_alucard_cpuinfo *this_alucard_cpuinfo;
	int rc, delay;

	cpu = policy->cpu;
	this_alucard_cpuinfo = &per_cpu(od_alucard_cpuinfo, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&alucard_mutex);

		this_alucard_cpuinfo->cur_policy = policy;

		this_alucard_cpuinfo->prev_cpu_idle = get_cpu_idle_time(cpu, &this_alucard_cpuinfo->prev_cpu_wall, 0);

		this_alucard_cpuinfo->freq_table = cpufreq_frequency_get_table(cpu);
		this_alucard_cpuinfo->cpu = cpu;

		alucard_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (alucard_enable == 1) {
			rc = sysfs_create_group(cpufreq_global_kobject,
						&alucard_attr_group);
			if (rc) {
				alucard_enable--;
				mutex_unlock(&alucard_mutex);
				return rc;
			}
		}
		mutex_unlock(&alucard_mutex);

		mutex_init(&this_alucard_cpuinfo->timer_mutex);

#if 0
		/* Initiate timer time stamp */
		this_alucard_cpuinfo->time_stamp = ktime_get();
#endif

		delay=usecs_to_jiffies(alucard_tuners_ins.sampling_rate);
		if (num_online_cpus() > 1) {
			delay -= jiffies % delay;
		}

		this_alucard_cpuinfo->enable = 1;
		INIT_DEFERRABLE_WORK(&this_alucard_cpuinfo->work, do_alucard_timer);
		queue_delayed_work_on(this_alucard_cpuinfo->cpu, alucard_wq, &this_alucard_cpuinfo->work, delay);

		break;

	case CPUFREQ_GOV_STOP:
		cancel_delayed_work_sync(&this_alucard_cpuinfo->work);

		mutex_lock(&alucard_mutex);
		mutex_destroy(&this_alucard_cpuinfo->timer_mutex);

		this_alucard_cpuinfo->enable = 0;

		alucard_enable--;
		if (!alucard_enable) {
			sysfs_remove_group(cpufreq_global_kobject,
					   &alucard_attr_group);			
		}
		mutex_unlock(&alucard_mutex);
		
		break;

	case CPUFREQ_GOV_LIMITS:
		if (!this_alucard_cpuinfo->cur_policy) {
			pr_debug("Unable to limit cpu freq due to cur_policy == NULL\n");
			return -EPERM;
		}
		mutex_lock(&this_alucard_cpuinfo->timer_mutex);
		if (policy->max < this_alucard_cpuinfo->cur_policy->cur)
			__cpufreq_driver_target(this_alucard_cpuinfo->cur_policy,
				policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_alucard_cpuinfo->cur_policy->cur)
			__cpufreq_driver_target(this_alucard_cpuinfo->cur_policy,
				policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_alucard_cpuinfo->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_ALUCARD
static
#endif
struct cpufreq_governor cpufreq_gov_alucard = {
	.name                   = "alucard",
	.governor               = cpufreq_governor_alucard,
	.owner                  = THIS_MODULE,
};


static int __init cpufreq_gov_alucard_init(void)
{
	alucard_wq = alloc_workqueue("alucard_wq", WQ_HIGHPRI, 0);

	if (!alucard_wq) {
		printk(KERN_ERR "Failed to create alucard workqueue\n");
		return -EFAULT;
	}

	return cpufreq_register_governor(&cpufreq_gov_alucard);
}

static void __exit cpufreq_gov_alucard_exit(void)
{
	destroy_workqueue(alucard_wq);
	cpufreq_unregister_governor(&cpufreq_gov_alucard);
}

MODULE_AUTHOR("Alucard24@XDA");
MODULE_DESCRIPTION("'cpufreq_alucard' - A dynamic cpufreq governor v1.1 (SnapDragon)");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ALUCARD
fs_initcall(cpufreq_gov_alucard_init);
#else
module_init(cpufreq_gov_alucard_init);
#endif
module_exit(cpufreq_gov_alucard_exit);
