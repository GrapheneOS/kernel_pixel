/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PA_KILL_CORE_H_
#define __PA_KILL_CORE_H_

long kill_processes(int oom_score_adj);
long kill_victim_task(struct task_struct *victim);

void reclaim_memory(unsigned long nr_demand_pages);
void pa_set_cpu_affinity(void);
void destroy_kill_threads(void);
int create_kill_threads(unsigned int nr_thread);

#endif
