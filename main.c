#include "airkiss.h"
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <linux/nl80211.h>
#include <linux/genetlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>
#include <netlink/attr.h>

#include <pcap.h>
#include <net/if.h>
#include <signal.h>
#include <time.h>

/* nl80211 */
struct nl80211_state {
    struct nl_sock *nl_sock;
    struct nl_cache *nl_cache;
    struct genl_family *nl80211;
} state;

typedef enum {
        DT_NULL = 0,
        DT_WLANNG,
        DT_HOSTAP,
        DT_MADWIFI,
        DT_MADWIFING,
        DT_BCM43XX,
        DT_ORINOCO,
        DT_ZD1211RW,
        DT_ACX,
        DT_MAC80211_RT,
        DT_AT76USB,
        DT_IPW2200

} DRIVER_TYPE;
int ieee80211_channel_to_frequency(int chan, enum nl80211_band band)
{
	/* see 802.11 17.3.8.3.2 and Annex J
	 * there are overlapping channel numbers in 5GHz and 2GHz bands */
	if (chan <= 0)
		return 0; /* not supported */
	switch (band) {
	case NL80211_BAND_2GHZ:
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
		break;
	case NL80211_BAND_5GHZ:
		if (chan >= 182 && chan <= 196)
			return 4000 + chan * 5;
		else
			return 5000 + chan * 5;
		break;
	case NL80211_BAND_60GHZ:
		if (chan < 5)
			return 56160 + chan * 2160;
		break;
	default:
		;
	}
	return 0; /* not supported */
}

static int linux_nl80211_init(struct nl80211_state *state)
{
    int err;

    state->nl_sock = nl_socket_alloc();

    if (!state->nl_sock) {
        fprintf(stderr, "Failed to allocate netlink socket.\n");
        return -ENOMEM;
    }

    if (genl_connect(state->nl_sock)) {
        fprintf(stderr, "Failed to connect to generic netlink.\n");
        err = -ENOLINK;
        goto out_handle_destroy;
    }

    if (genl_ctrl_alloc_cache(state->nl_sock, &state->nl_cache)) {
        fprintf(stderr, "Failed to allocate generic netlink cache.\n");
        err = -ENOMEM;
        goto out_handle_destroy;
    }

    state->nl80211 = genl_ctrl_search_by_name(state->nl_cache, "nl80211");
    if (!state->nl80211) {
        fprintf(stderr, "nl80211 not found.\n");
        err = -ENOENT;
        goto out_cache_free;
    }

    return 0;

 out_cache_free:
    nl_cache_free(state->nl_cache);
 out_handle_destroy:
    nl_socket_free(state->nl_sock);
    return err;
}

static void nl80211_cleanup(struct nl80211_state *state)
{
    genl_family_put(state->nl80211);
    nl_cache_free(state->nl_cache);
    nl_socket_free(state->nl_sock);
}
static int linux_set_channel_nl80211(const char *if_name, int channel)
{
    unsigned int devid;
    struct nl_msg *msg;
    unsigned int freq;
    unsigned int htval = NL80211_CHAN_NO_HT;
    msg=nlmsg_alloc();
    if (!msg) {
        fprintf(stderr, "failed to allocate netlink message\n");
        return 2;
    }

    devid=if_nametoindex(if_name);

    enum nl80211_band band;
    band = channel <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
    freq=ieee80211_channel_to_frequency(channel, band);

    genlmsg_put(msg, 0, 0, genl_family_get_id(state.nl80211), 0,
            0, NL80211_CMD_SET_WIPHY, 0);

    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, devid);
    NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FREQ, freq);
    NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE, htval);

    //ret = nl_send_auto_complete(state.nl_sock,msg);
    nlmsg_free(msg);

    return 0;

 nla_put_failure:
    return -ENOBUFS;
}



pcap_t *handle;                         /* Session handle */

static airkiss_context_t *akcontex = NULL;
const airkiss_config_t akconf = { 
(airkiss_memset_fn)&memset, 
(airkiss_memcpy_fn)&memcpy, 
(airkiss_memcmp_fn)&memcmp, 
(airkiss_printf_fn)&printf };

airkiss_result_t result;

struct itimerval my_timer;
int startTimer(struct itimerval *timer, int ms);
static int channel = 1;
const char *wifi_if = NULL;
void switch_channel_callback(void)
{
    //printf("Current channel is: %d\n", channel);
    linux_set_channel_nl80211(wifi_if, channel++);
    if(channel > 14)
    {
        channel = 1;
        printf("scan all channels\n");
    }
}

void recv_callback(u_char *args, const struct pcap_pkthdr *header, const u_char *packet)
{
    int ret;
    ret = airkiss_recv(akcontex, (void *)packet, header->len);
    if(ret==AIRKISS_STATUS_CONTINUE)
    {
        //return;
    }
    else if(ret == AIRKISS_STATUS_CHANNEL_LOCKED)
    {
        startTimer(&my_timer, 0);
        //printf("HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHERE IIIIIIIIIIIIIIIIIIIIII CCCCCCCCCCCCCCCCCOME!!!!!!!!\n");
        //printf("len: %d , caplen: %d\n", header->len, header->caplen);
        printf("Lock channel in %d\n", channel);
    }
    else if(ret == AIRKISS_STATUS_COMPLETE)
    {
        printf("Airkiss completed.\n");
        airkiss_get_result(akcontex, &result);
        printf("get result:\nssid:%s\nlength:%d\nkey:%s\nlength:%d\nssid_crc:%x\nrandom:%d\n", 
			result.ssid, result.ssid_length, result.pwd, result.pwd_length, result.reserved, result.random);

        pcap_close(handle);
    }
    //printf("len: %d, airkiss ret: %d\n", header->len, ret);
}
int main(int argc, char *argv[])
{
    if(argc!=2)
    {
        printf("Usage: %s <device-name>\n", argv[0]);
        return -1;
    }
    wifi_if = argv[1];

    char *dev;
    char errbuf[PCAP_ERRBUF_SIZE];

    dev = pcap_lookupdev(errbuf);
    if(NULL==dev)
    {
        printf("Error: %s\n", errbuf);
        return -1;
    }

    handle = pcap_open_live(dev, BUFSIZ, 1, 5, errbuf); //5ms recv timeout
    if(NULL==handle)
    {
        printf("Error: %s\n", errbuf);
        return -1;
    }
    /* airkiss setup */
    int result;
    akcontex = (airkiss_context_t *)malloc(sizeof(airkiss_context_t));
    result = airkiss_init(akcontex, &akconf);
    if(result != 0)
    {
        printf("Airkiss init failed!!\n");
        exit(1);
    }
    printf("Airkiss verson: %s\n", airkiss_version());

    /* 80211 */
    linux_nl80211_init(&state);

    /* Setup timer */
    startTimer(&my_timer, 100);   
    signal(SIGALRM,(__sighandler_t)&switch_channel_callback);
    
    pcap_loop(handle, -1, recv_callback, NULL);

    return 0;
}

int startTimer(struct itimerval *timer, int ms)
{
    time_t secs, usecs;
    secs = ms/1000;
    usecs = ms%1000 * 1000;

    timer->it_interval.tv_sec = secs;
    timer->it_interval.tv_usec = usecs;
    timer->it_value.tv_sec = secs;
    timer->it_value.tv_usec = usecs;

	setitimer(ITIMER_REAL, timer, NULL);
}