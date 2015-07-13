#include <linux/export.h>
#include <linux/sched.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/sysctl.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "ap_switch_utils.h"

#define MAX_CHANNELS 3
#define MAX_VIFS 6

/*
TW = Tw (or wait time); it's the amount of time (in miliseconds) that you
are required to wait on a certain channel since the last outgoing or incoming
packet; if there's silence for the duration of Tw, a channel switch is triggered
*/
#define NO_PCK_THRESH 5
#define FIRST_PCK_WAIT_TIME 10

/* frequencies of channels we are currently associated on */
unsigned int channels[MAX_VIFS];

/* num_channels: the number of different channels on which
 * the wifi device is listening/switching
 */
unsigned int num_channels;
/* 
 * num_vifs: actual number of vifs. Multiple vifs
 * can listen on the same channel, meaning that no
 * switching is required
 */
unsigned int num_vifs;

/* [VD]: completely enable or disable apsw */
int sysctl_timer_enabled = 1;
/* sysctl_wait_time represents Tw (check lengthy comment above) */
int sysctl_wait_time __read_mostly = 10;
EXPORT_SYMBOL(sysctl_wait_time);
/* sysctl_time_slot is the maximum amount of time allowed on a channel */
int sysctl_time_slot __read_mostly = 100;
EXPORT_SYMBOL(sysctl_time_slot);
int sysctl_duty_cycle __read_mostly = 300;
int sysctl_switch_wait_time __read_mostly = 3;
int g_switch_wait_time = 3;
EXPORT_SYMBOL(g_switch_wait_time);
int sysctl_stddev_multiplier __read_mostly = 10;
int sysctl_ap_switching = 0;
int sysctl_loss_rate = 0;
EXPORT_SYMBOL(sysctl_loss_rate);
int sysctl_alpha = 10;
EXPORT_SYMBOL(sysctl_alpha);
int sysctl_ecn_threshold = 50;
static struct ctl_table ap_switch_table[] = {
    {
        .procname = "timer_enabled",
        .data = &sysctl_timer_enabled,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "wait_time",
        .data = &sysctl_wait_time,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "time_slot",
        .data = &sysctl_time_slot,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "switch_wait_time",
        .data = &sysctl_switch_wait_time,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "stddev_multiplier",
        .data = &sysctl_stddev_multiplier,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "duty_cycle",
        .data = &sysctl_duty_cycle,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "switch_enabled",
        .data = &sysctl_ap_switching,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "loss_rate",
        .data = &sysctl_loss_rate,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "alpha",
        .data = &sysctl_alpha,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "ecn_threshold",
        .data = &sysctl_ecn_threshold,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "channels",
        .data = &channels,
        .maxlen = MAX_VIFS * sizeof(unsigned int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "num_channels",
        .data = &num_channels,
        .maxlen = sizeof(unsigned int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    {
        .procname = "num_vifs",
        .data = &num_vifs,
        .maxlen = sizeof(unsigned int),
        .mode = 0644,
        .proc_handler = proc_dointvec
    },
    { }
};

u16 g_mcs_table[32][4];
EXPORT_SYMBOL(g_mcs_table);

/*
struct timeval past_packet_times[1000];
EXPORT_SYMBOL(past_packet_times);
int past_packet_delivered[1000];
EXPORT_SYMBOL(past_packet_delivered);
int start, end, num_delivered;
EXPORT_SYMBOL(start);
EXPORT_SYMBOL(end);
EXPORT_SYMBOL(num_delivered);
*/

struct ap_switch {
    struct work_struct ap_switch_work;
};


struct ap_switch g_apsw;
/* the current channel; */
int g_current_channel;
/* the channel we need to switch to next */
int g_next_channel;
/* ieee80211_hw pointer; we get this from the driver */
struct ieee80211_hw *g_hw;
EXPORT_SYMBOL(g_hw);
/* timer for natural switch */
struct timer_list g_timer;
/* flag that enables/disables switching */
bool g_ap_switching;
EXPORT_SYMBOL(g_ap_switching);
/* flag that specifies whether the last switch was natural or forced */
bool g_natural_sw;
bool g_switch_waiting;
EXPORT_SYMBOL(g_switch_waiting);
/* timer for Tw (needed in rx and tx functions) */
struct timer_list g_rxtx_timer;
EXPORT_SYMBOL(g_rxtx_timer);
struct timer_list g_switch_wait_timer;
EXPORT_SYMBOL(g_switch_wait_timer);
/* flag that enables or disables forced switching */
bool g_rxtx_timer_allowed;
EXPORT_SYMBOL(g_rxtx_timer_allowed);
struct timespec g_last_packet_time;
EXPORT_SYMBOL(g_last_packet_time);
int g_delta_t;
EXPORT_SYMBOL(g_delta_t);
int g_bytes;
EXPORT_SYMBOL(g_bytes);
int g_nr_packets;
EXPORT_SYMBOL(g_nr_packets);
int *g_avg_delta_ts;
int *g_avg_bw;
int g_delta_ts[2000];
EXPORT_SYMBOL(g_delta_ts);
int *g_stddevs;
int *g_no_packets;
int g_time_slot_multiplier = 1;
int *g_time_slots;
int g_last_bitrate = 0;
EXPORT_SYMBOL(g_last_bitrate);
struct ieee80211_sub_if_data* g_last_packet_sdata = NULL;
EXPORT_SYMBOL(g_last_packet_sdata);

/* [VD]: made global in order to unregister */
struct ctl_table_header *ap_switch_sysctl_hdr;

int g_apsw_timer_enabled = 1;

/* Channel information used for switching */
/* TODO: duplicate channel info will occur in the case of same-channel association */
struct cfg80211_chan_def g_channel_info[MAX_VIFS];
unsigned int g_current_channel_index = 0;
unsigned int g_next_channel_index = 0;

static void ap_switch_populate_mcs_table(void)
{
    g_mcs_table[0][0] = 65;
    g_mcs_table[0][1] = 135;
    g_mcs_table[0][2] = 72;
    g_mcs_table[0][3] = 150;
    g_mcs_table[1][0] = 130;
    g_mcs_table[1][1] = 270;
    g_mcs_table[1][2] = 144;
    g_mcs_table[1][3] = 300;
    g_mcs_table[2][0] = 195;
    g_mcs_table[2][1] = 405;
    g_mcs_table[2][2] = 217;
    g_mcs_table[2][3] = 450;
    g_mcs_table[3][0] = 260;
    g_mcs_table[3][1] = 540;
    g_mcs_table[3][2] = 289;
    g_mcs_table[3][3] = 600;
    g_mcs_table[4][0] = 390;
    g_mcs_table[4][1] = 810;
    g_mcs_table[4][2] = 433;
    g_mcs_table[4][3] = 900;
    g_mcs_table[5][0] = 520;
    g_mcs_table[5][1] = 1080;
    g_mcs_table[5][2] = 578;
    g_mcs_table[5][3] = 1200;
    g_mcs_table[6][0] = 585;
    g_mcs_table[6][1] = 1215;
    g_mcs_table[6][2] = 650;
    g_mcs_table[6][3] = 1350;
    g_mcs_table[7][0] = 650;
    g_mcs_table[7][1] = 1350;
    g_mcs_table[7][2] = 722;
    g_mcs_table[7][3] = 1500;
    g_mcs_table[8][0] = 130;
    g_mcs_table[8][1] = 270;
    g_mcs_table[8][2] = 144;
    g_mcs_table[8][3] = 300;
    g_mcs_table[9][0] = 260;
    g_mcs_table[9][1] = 540;
    g_mcs_table[9][2] = 289;
    g_mcs_table[9][3] = 600;
    g_mcs_table[10][0] = 390;
    g_mcs_table[10][1] = 810;
    g_mcs_table[10][2] = 433;
    g_mcs_table[10][3] = 900;
    g_mcs_table[11][0] = 520;
    g_mcs_table[11][1] = 1080;
    g_mcs_table[11][2] = 578;
    g_mcs_table[11][3] = 1200;
    g_mcs_table[12][0] = 780;
    g_mcs_table[12][1] = 1620;
    g_mcs_table[12][2] = 867;
    g_mcs_table[12][3] = 1800;
    g_mcs_table[13][0] = 1040;
    g_mcs_table[13][1] = 2160;
    g_mcs_table[13][2] = 1156;
    g_mcs_table[13][3] = 2400;
    g_mcs_table[14][0] = 1170;
    g_mcs_table[14][1] = 2430;
    g_mcs_table[14][2] = 1303;
    g_mcs_table[14][3] = 2700;
    g_mcs_table[15][0] = 1300;
    g_mcs_table[15][1] = 2700;
    g_mcs_table[15][2] = 1444;
    g_mcs_table[15][3] = 3000;
    g_mcs_table[16][0] = 195;
    g_mcs_table[16][1] = 405;
    g_mcs_table[16][2] = 217;
    g_mcs_table[16][3] = 450;
    g_mcs_table[17][0] = 390;
    g_mcs_table[17][1] = 810;
    g_mcs_table[17][2] = 433;
    g_mcs_table[17][3] = 900;
    g_mcs_table[18][0] = 585;
    g_mcs_table[18][1] = 1215;
    g_mcs_table[18][2] = 650;
    g_mcs_table[18][3] = 1350;
    g_mcs_table[19][0] = 780;
    g_mcs_table[19][1] = 1620;
    g_mcs_table[19][2] = 867;
    g_mcs_table[19][3] = 1800;
    g_mcs_table[20][0] = 1170;
    g_mcs_table[20][1] = 2430;
    g_mcs_table[20][2] = 1300;
    g_mcs_table[20][3] = 2700;
    g_mcs_table[21][0] = 1560;
    g_mcs_table[21][1] = 3240;
    g_mcs_table[21][2] = 1733;
    g_mcs_table[21][3] = 3600;
    g_mcs_table[22][0] = 1755;
    g_mcs_table[22][1] = 3645;
    g_mcs_table[22][2] = 1950;
    g_mcs_table[22][3] = 4050;
    g_mcs_table[23][0] = 1950;
    g_mcs_table[23][1] = 4050;
    g_mcs_table[23][2] = 2167;
    g_mcs_table[23][3] = 4500;
    g_mcs_table[24][0] = 260;
    g_mcs_table[24][1] = 540;
    g_mcs_table[24][2] = 289;
    g_mcs_table[24][3] = 600;
    g_mcs_table[25][0] = 520;
    g_mcs_table[25][1] = 1080;
    g_mcs_table[25][2] = 578;
    g_mcs_table[25][3] = 1200;
    g_mcs_table[26][0] = 780;
    g_mcs_table[26][1] = 1620;
    g_mcs_table[26][2] = 867;
    g_mcs_table[26][3] = 1800;
    g_mcs_table[27][0] = 1040;
    g_mcs_table[27][1] = 2160;
    g_mcs_table[27][2] = 1156;
    g_mcs_table[27][3] = 2400;
    g_mcs_table[28][0] = 1560;
    g_mcs_table[28][1] = 3240;
    g_mcs_table[28][2] = 1733;
    g_mcs_table[28][3] = 3600;
    g_mcs_table[29][0] = 2080;
    g_mcs_table[29][1] = 4320;
    g_mcs_table[29][2] = 2311;
    g_mcs_table[29][3] = 4800;
    g_mcs_table[30][0] = 2340;
    g_mcs_table[30][1] = 4860;
    g_mcs_table[30][2] = 2600;
    g_mcs_table[30][3] = 5400;
    g_mcs_table[31][0] = 2600;
    g_mcs_table[31][1] = 5400;
    g_mcs_table[31][2] = 2889;
    g_mcs_table[31][3] = 6000;
}

int ap_switch_timediff(struct timeval t1, struct timeval t2)
{
    return (t2.tv_sec - t1.tv_sec) * 1000000 + 
           t2.tv_usec - t1.tv_usec;
}

int ap_switch_timediff_ns(struct timespec t1, struct timespec t2)
{
    return ((t2.tv_sec - t1.tv_sec) * 1000000000
            + (t2.tv_nsec - t1.tv_nsec));
}

/*
    [AC]: Called by driver upon initialization of physical interface
    params:
        struct ieee80211_hw * - pointer to driver-specific structure
    returns:
        void
    This function initialises a bunch of stuff
*/
void ap_switch_load_interface(struct ieee80211_hw *hw_info)
{
    /* [VD]: unused, so far */
    struct timespec now;
    g_hw = hw_info;
    setup_timer(&g_timer, timer_function, 0);
    setup_timer(&g_rxtx_timer, rxtx_timer_function, 0);
    setup_timer(&g_switch_wait_timer, wait_timer_function, 0);
    INIT_WORK(&g_apsw.ap_switch_work, ap_switch_work_handler);
    g_switch_waiting = false;
    g_natural_sw = false;
    g_rxtx_timer_allowed = false;
    /* [VD]: these should be freed */
    g_avg_delta_ts = kmalloc(MAX_CHANNELS*sizeof(int), GFP_KERNEL);
    g_avg_bw= kmalloc(MAX_CHANNELS*sizeof(int), GFP_KERNEL);
    g_no_packets = kmalloc(MAX_CHANNELS*sizeof(int), GFP_KERNEL);
    g_stddevs = kmalloc(MAX_CHANNELS*sizeof(int), GFP_KERNEL);
    g_time_slots = kmalloc(MAX_CHANNELS*sizeof(int), GFP_KERNEL);
    num_channels = 0;

    g_current_channel = 0;
    g_apsw_timer_enabled = sysctl_timer_enabled;
    getnstimeofday(&now);
    g_last_packet_time.tv_sec = now.tv_sec;
    g_last_packet_time.tv_nsec = now.tv_nsec;
    mod_timer(&g_timer, jiffies+sysctl_time_slot);

    ap_switch_populate_mcs_table();

    ap_switch_sysctl_hdr = register_net_sysctl(&init_net, "net/apsw",
                                        ap_switch_table);

    if (!ap_switch_sysctl_hdr) {
	return;
    }
	/* [VD]: Initially, switching is disabled. It needs to be
	 * activated via sysctl.
	 * [VD]: Rectification: switching will be dynamically activated,
	 * depending on the number of channels we're using.
	 */
    g_ap_switching = false;
    sysctl_ap_switching = 0;
}
EXPORT_SYMBOL(ap_switch_load_interface);

/* When physical interface no longer exists, we no longer switch */
void ap_switch_stop_interface(struct ieee80211_hw *hw_info)
{
    g_ap_switching = false;
    sysctl_ap_switching = 0;
    unregister_net_sysctl_table(ap_switch_sysctl_hdr);
    kfree(g_avg_delta_ts);
    kfree(g_avg_bw);
    kfree(g_no_packets);
    kfree(g_stddevs);
    kfree(g_time_slots);

    if (g_apsw_timer_enabled == 1) {
    del_timer(&g_timer);
    del_timer(&g_rxtx_timer);
    del_timer(&g_switch_wait_timer);
    }

    /* [VD]: stop all queues */
    ieee80211_stop_queues(g_hw);
}
EXPORT_SYMBOL(ap_switch_stop_interface);

/*
    [AC]: This function does a bunch of stuff; details below
    params:
        sdata: sub-interface (virtual interface) struct
        stop: if 0, we stop interfaces which are going offchannel
              if 1, we start interfaces that are going "on channel"
    If stop is 0, we find all the vifs that are associated and running on a
    channel that is not the channel we are going to switch to (the parameter).
    We stop those vifs by stopping their queues, sending a PS frame to the AP
    and flushing the tx queues to make sure the frame got sent and the AP is 
    aware that we are going to sleep.
    If stop is 1, we do the opposite of that.
    !!!! this function does not actually set the channel; it just does the prep
    work
*/
/* TODO: fix loss rate originating from improper PS and queue start/stop sequencing */
/* XXX: unused */
void ap_switch_change_channel(struct ieee80211_sub_if_data *sdata, 
                int current_channel, int next_channel, int stop)
{
    struct ieee80211_sub_if_data *l_sdata;
    bool found = false;
    struct ieee80211_local *local = hw_to_local(g_hw);

    if (local == NULL) {
        return;
    }

    list_for_each_entry(l_sdata, &local->interfaces, list) {
        if (l_sdata == sdata) {
            found = true;
            break;
        }
    }
    if (found == false)
        return;

    mutex_lock(&local->iflist_mtx);
    if (stop == 0) {
        list_for_each_entry(l_sdata, &local->interfaces, list) {
            if (!ieee80211_sdata_running(l_sdata))
                continue;

	    if (l_sdata->vif.type == NL80211_IFTYPE_STATION &&
		l_sdata->u.mgd.associated && 
		l_sdata->apsw_center_freq == current_channel) {
	      set_bit(SDATA_STATE_OFFCHANNEL, &l_sdata->state);
	      ieee80211_offchannel_ps_enable(l_sdata);
	      netif_tx_stop_all_queues(l_sdata->dev);
	      /* ieee80211_stop_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP, IEEE80211_QUEUE_STOP_REASON_FLUSH); */
	      /* ieee80211_flush_queues(local, l_sdata); */
	      drv_flush(local, 0, false);
	    }
        }
    }
    else {
        list_for_each_entry(l_sdata, &local->interfaces, list) {
            if (!ieee80211_sdata_running(l_sdata))
                continue;

            if (l_sdata->vif.type == NL80211_IFTYPE_MONITOR)
                continue;

            if (l_sdata->vif.type == NL80211_IFTYPE_STATION &&
                        l_sdata->u.mgd.associated &&
                        l_sdata->apsw_center_freq == next_channel &&
                        test_bit(SDATA_STATE_OFFCHANNEL, &l_sdata->state)) {
                clear_bit(SDATA_STATE_OFFCHANNEL, &l_sdata->state);
                ieee80211_offchannel_ps_disable(l_sdata);
                drv_flush(local, 0, false);
		/* ieee80211_flush_queues(local, l_sdata); */
                netif_tx_wake_all_queues(l_sdata->dev);
           }
        }
    }
    mutex_unlock(&local->iflist_mtx);
}

/* TODO: Write a proper preparation sequence for vif.
 * TODO: see if we need beacon code from offchannel
 * TODO: see if stop/wake_queue (singular) is needed in this context
 * See offchannel.c
 */

void ap_switch_vif_sleep(struct ieee80211_sub_if_data *sdata)
{
  struct ieee80211_local *local = hw_to_local(g_hw);
  unsigned int vindex = 0;

  if (WARN_ON(local->use_chanctx))
    return;

  /*
   * notify the AP about us leaving the channel and stop all
   * STA interfaces.
   */

  /*
   * Stop queues and transmit all frames queued by the driver
   * before sending nullfunc to enable powersave at the AP.
   */
  ieee80211_stop_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP,
					IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL);
  ieee80211_flush_queues(local, NULL);

  mutex_lock(&local->iflist_mtx);
  list_for_each_entry(sdata, &local->interfaces, list) {
    if (!ieee80211_sdata_running(sdata))
      continue;

    if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
      continue;

    if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
      set_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

    if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	sdata->u.mgd.associated &&
	channels[vindex] == g_current_channel)
      ieee80211_offchannel_ps_enable(sdata);
    vindex++;
  }
  mutex_unlock(&local->iflist_mtx);
}

void ap_switch_vif_wake(struct ieee80211_sub_if_data *sdata)
{
  struct ieee80211_local *local = hw_to_local(g_hw);
  unsigned int vindex = 0;

  if (WARN_ON(local->use_chanctx))
    return;

  mutex_lock(&local->iflist_mtx);
  list_for_each_entry(sdata, &local->interfaces, list) {
    if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
      continue;

    if (sdata->vif.type != NL80211_IFTYPE_MONITOR)
      clear_bit(SDATA_STATE_OFFCHANNEL, &sdata->state);

    if (!ieee80211_sdata_running(sdata))
      continue;

    /* Tell AP we're back */
    if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	sdata->u.mgd.associated &&
	channels[vindex] == g_next_channel)
      ieee80211_offchannel_ps_disable(sdata);
    vindex++;
  }
  mutex_unlock(&local->iflist_mtx);

  ieee80211_wake_queues_by_reason(&local->hw, IEEE80211_MAX_QUEUE_MAP,
				  IEEE80211_QUEUE_STOP_REASON_OFFCHANNEL);
}

void sort(unsigned int array[], unsigned int len)
{
  unsigned int i = 0;
  unsigned int j = 0;
  unsigned int aux = 0;

  for (i = 0; i < len - 1; i++)
    for (j = i + 1; j < len; j++)
      if (array[i] > array[j])
	{
	  aux = array[j];
	  array[j] = array[i];
	  array[i] = aux;
	}
}

/* Count number of unique non-zero channels */
unsigned int count_unique(unsigned int array[], unsigned int len)
{
  unsigned int i = 0;
  unsigned int num = 0;
  unsigned int new_array[len];

  /* copy into new array */
  for (i = 0; i < len; i++)
    new_array[i] = array[i];

  /* sort, leave array unmodified */
  sort(new_array, len);

  /* count nonzero uniques */
  for (i = 0; i < len; i++)
    {
      if (new_array[i] == new_array[i+1])
	continue;
      else if (new_array[i] != 0)
	num++;
    }

  return num;
}

/*
    [AC]: This is where all the channel switching logic happens
*/
void ap_switch_work_handler(struct work_struct *work)
{
    struct ieee80211_sub_if_data *sdata;
    struct ieee80211_local *local = hw_to_local(g_hw);
    int i;
    /* [VD]: unused */
    /* int zero, sum; */
    int j;
    int hw_ret = 0;
    unsigned int vif_index = 0;
    /* Used to index both channels[] and channel_info[] */
    /* It is effectively the first nonzero channel */
    int first_channel_index = -1;

    if (sysctl_timer_enabled == 0) {
        g_apsw_timer_enabled = 0;
	del_timer(&g_timer);
	del_timer(&g_rxtx_timer);
	del_timer(&g_switch_wait_timer);
        return;
    }

    /*
     * [VD]: Update num_channels and channels[];
     * iterate over vifs, check to see if they are of type station
     * and if they are associated;
     * update the cfg80211_chan_def structures at the same time;
     */

    list_for_each_entry(sdata, &local->interfaces, list) {
      if (sdata->vif.type == NL80211_IFTYPE_STATION && sdata->u.mgd.associated && channels[vif_index] == 0) {
	/* 
	 * Get the channel; see if it's already in the list. If not,
	 * add it to the list and increment num_channels. Also, turn on switching
	 * if num_channels > 2
	 */
	if (first_channel_index == -1)
	  first_channel_index = vif_index;
	channels[vif_index] = sdata->local->_oper_chandef.center_freq1;
    	sdata->apsw_center_freq = channels[vif_index];

	/* Save channel info */
	g_channel_info[vif_index] = sdata->local->_oper_chandef;
      }
      else if (sdata->vif.type == NL80211_IFTYPE_STATION && !sdata->u.mgd.associated && channels[vif_index] != 0) {
	  channels[vif_index] = 0;
	  sdata->apsw_center_freq = 0;
      }
      vif_index++;
    }

    num_channels = count_unique(channels, MAX_CHANNELS);
    num_vifs = vif_index;
    g_current_channel = local->_oper_chandef.center_freq1;

    if (num_channels == 1 || num_channels == 0) {
      g_ap_switching = false;
      sysctl_ap_switching = 0;

      mod_timer(&g_timer, jiffies+sysctl_time_slot*g_time_slot_multiplier);
      return;
    }

    if (num_channels > 1 && g_ap_switching == false) {
      g_ap_switching = true;
      sysctl_ap_switching = 1;
    }

    if (!g_switch_waiting) {
        g_next_channel = 0;
        for (i = first_channel_index; i < MAX_CHANNELS; i++) {
            if (g_current_channel == channels[i] && g_current_channel != 0) {
	      /* g_next_channel = channels[(i+1)%num_channels]; */
	      /* Get next channel */
	      j = (i + 1) % MAX_CHANNELS;
	      while (channels[j] == 0)
	      	j = (j + 1) % MAX_CHANNELS;
	      g_next_channel = channels[j];
	      g_next_channel_index = j;
	      g_current_channel_index = i;
	      break;
            }
        }

        list_for_each_entry(sdata, &local->interfaces, list) {
            break;
        }
        if (g_next_channel == 0) {
            return;
        }

        /* stop vifs on any channel other than next channel */
        /* ap_switch_change_channel(sdata, g_next_channel, g_current_channel, 0); */
	ap_switch_vif_sleep(sdata);

	/* Start wait timer after enabling PS */
        if (g_natural_sw && sysctl_switch_wait_time > 0) {
            g_switch_waiting = true;
            mod_timer(&g_switch_wait_timer, jiffies+sysctl_switch_wait_time);
        }
        else {
            goto work_continue;
        }
    } else {
work_continue:
        if (g_switch_waiting) {
            g_switch_waiting = false;
            list_for_each_entry(sdata, &local->interfaces, list) {
                break;
            }
            /* ap_switch_change_channel(sdata, g_current_channel, g_next_channel, 0); */
	    ap_switch_vif_sleep(sdata);
        }

	/* set struct cfg80211_chan_def to next channel */
	local->_oper_chandef = g_channel_info[g_next_channel_index];

        /* tell the driver to set the channel */
        hw_ret = ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);


        /* start all vifs on the next channel */
        /* ap_switch_change_channel(sdata, g_current_channel, g_next_channel, 1); */
	ap_switch_vif_wake(sdata);

	/* g_current_channel = channels[g_next_channel_index]; */
	/* g_next_channel = channels[g_current_channel_index]; */

        g_current_channel = g_next_channel;
        g_time_slot_multiplier = 1;

        g_nr_packets = 0;
        g_delta_t = 0;
        g_bytes = 0;

        /* set the timers for natural and forced switch */
        mod_timer(&g_timer, jiffies+sysctl_time_slot*g_time_slot_multiplier);
    }
}

/* natural switch timer function */
void timer_function(unsigned long data)
{
    struct timespec now;
    g_rxtx_timer_allowed = false;
    del_timer(&g_rxtx_timer);
    g_natural_sw = true;
    getnstimeofday(&now);
    g_delta_t += ap_switch_timediff_ns(g_last_packet_time, now);
    ieee80211_queue_work(g_hw, &g_apsw.ap_switch_work);
}

/* forced switch timer function */
void rxtx_timer_function(unsigned long data)
{
    g_rxtx_timer_allowed = false;
    g_natural_sw = false;
    ieee80211_queue_work(g_hw, &g_apsw.ap_switch_work);
}

void wait_timer_function(unsigned long data)
{
    ieee80211_queue_work(g_hw, &g_apsw.ap_switch_work);
}
