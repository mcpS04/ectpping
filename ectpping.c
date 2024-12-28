/*
 *	ectpping
 *	~~~~~~~~
 *
 * A 'ping' like program for the Ethernet V2.0 Configuration Testing Protocol.
 *
 * Copyright (C) 2008-2009, Mark Smith <markzzzsmith@yahoo.com.au>
 * All rights reserved.
 *
 * Licensed under the GNU General Public Licence (GPL) Version 2 only.
 * This explicitly does not include later versions, such as revisions of 2 or
 * Version 3, and later versions.
 * See the accompanying LICENSE file for full terms and conditions.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/ether.h>

#include "libenetaddr.h"
#include "libectp.h"

/*
 * Struct defs
 */

/*
 * Program parameters in internal program format
 */
struct program_parameters {
	char iface[IFNAMSIZ];
	int ifindex;
	struct ether_addr srcmac;
	struct ether_addr dstmac;
	bool uc_dstmac;
	uint8_t *ectp_user_data;
	unsigned int ectp_user_data_size;
	bool no_resolve;
	bool zero_pkt_output;
	unsigned int interval_ms;
	struct ether_addr *fwdaddrs;
	unsigned int num_fwdaddrs;
};


/*
 * Program options in external user format
 */
struct program_options {
	char iface[IFNAMSIZ];
	enum { ucast, mcast, bcast } dst_type;
	char *uc_dst_str; /* mac address or /etc/ethers hostname string */
	bool no_resolve;
	bool zero_pkt_output;
	unsigned int interval_ms;
	char *fwdaddrs_str;
};


/*
 * Arguments passed to the TX thread
 */
struct tx_thread_arguments {
	struct program_parameters *prog_parms;
	int *tx_sockfd;
};


/*
 * Arguments passed to the RX thread
 */
struct rx_thread_arguments {
	struct program_parameters *prog_parms;
	int *rx_sockfd;
};


/*
 *
 */
struct ectpping_payload {
	uint32_t seq_num;
	struct timeval tv;
};



/*
 * Function Prototypes
 */


void setup_sigint_hdlr(struct sigaction *sigint_action);

void print_ethaddr_hostname(const struct ether_addr *ethaddr, bool resolve);

void print_prog_header(const struct program_parameters *prog_parms);

void set_sigint_hdlr(struct sigaction *sigint_action);

void sigint_hdlr(int signum);

enum GET_PROG_PARMS {
	GET_PROG_PARMS_GOOD,
	GET_PROG_PARMS_BADIFINDEX,
};
enum GET_PROG_PARMS get_prog_parms(const int argc,
				   char *argv[],
				   struct program_parameters *prog_parms);


void set_default_prog_opts(struct program_options *prog_opts);
enum GET_CLI_OPTS {
	GET_CLI_OPTS_GOOD,
	GET_CLI_OPTS_BAD_HELP,
	GET_CLI_OPTS_BAD_UNKNOWN_OPT,
	GET_CLI_OPTS_BAD_MISSING_ARG,
	GET_CLI_OPTS_BAD_NEED_UID_0,
};
enum GET_CLI_OPTS get_cli_opts(const int argc,
			       char *argv[],
			       struct program_options *prog_opts,
			       int *erropt);

enum GET_CLI_OPTS get_cli_opts_eh(const enum GET_CLI_OPTS ret,
				  int *erropt);

void print_help(void);

enum PROCESS_PROG_OPTS {
	PROCESS_PROG_OPTS_GOOD,
	PROCESS_PROG_OPTS_BAD_IFACE,
	PROCESS_PROG_OPTS_BAD_IFMAC,
	PROCESS_PROG_OPTS_BAD_DSTMACFMT,
	PROCESS_PROG_OPTS_BAD
};
enum PROCESS_PROG_OPTS process_prog_opts(const struct program_options
						*prog_opts,
					 struct program_parameters *prog_parms,
					 const char **errmsg);

enum PROCESS_PROG_OPTS process_prog_opts_eh(const enum PROCESS_PROG_OPTS ret,
					    const char *errmsg);

enum GET_PROG_OPT_FWDADDRS {
	GET_PROG_OPT_FWDADDRS_GOOD,
	GET_PROG_OPT_FWDADDRS_BAD
};
enum GET_PROG_OPT_FWDADDRS get_prog_opt_fwdaddrs(const char *fwdaddrs_str,
			 			 struct ether_addr **fwdaddrs,
						 unsigned int *num_fwdaddrs);


enum DO_IFREQ_IOCTL {
	DO_IFREQ_IOCTL_GOOD,
	DO_IFREQ_IOCTL_BADSOCKET,
	DO_IFREQ_IOCTL_BADIOCTL
};
enum DO_IFREQ_IOCTL do_ifreq_ioctl(const int ioctl_request,
				   const char iface[IFNAMSIZ],
				   struct ifreq *ifr);


enum GET_IFINDEX {
	GET_IFINDEX_GOOD,
	GET_IFINDEX_BADSOCKET,
	GET_IFINDEX_BADIFACE
};
enum GET_IFINDEX get_ifindex(const char iface[IFNAMSIZ], int *ifindex);


enum GET_IFMAC {
	GET_IFMAC_GOOD,
	GET_IFMAC_BADSOCKET,
	GET_IFMAC_BADIFACE
};
enum GET_IFMAC get_ifmac(const char iface[IFNAMSIZ],
			 struct ether_addr *ifmac);


void open_sockets(int *tx_sockfd, int *rx_sockfd, const int ifindex);


enum OPEN_TX_SKT {
	OPEN_TX_SKT_GOOD,
	OPEN_TX_SKT_BADSOCKET, 		/* socket() call failed */
	OPEN_TX_SKT_BADIFINDEX,		/* supplied ifindex invalid */
};
enum OPEN_TX_SKT open_tx_socket(int *sockfd, const int tx_ifindex);


enum OPEN_RX_SKT {
	OPEN_RX_SKT_GOOD,
	OPEN_RX_SKT_BADSOCKET, 		/* socket() call failed */
	OPEN_RX_SKT_BADIFINDEX,		/* supplied ifindex invalid */
};
enum OPEN_RX_SKT open_rx_socket(int *sockfd, const int rx_ifindex);


void prepare_thread_args(struct tx_thread_arguments *tx_thread_args,
			 struct rx_thread_arguments *rx_thread_args,
			 struct program_parameters *prog_parms,
			 int *tx_sockfd,
			 int *rx_sockfd);

void build_ectp_eth_hdr(const struct ether_addr *srcmac,
			const struct ether_addr *dstmac,
			struct ether_header *eth_hdr);

enum BUILD_ECTP_FRAME {
	BUILD_ECTP_FRAME_GOOD,
	BUILD_ECTP_FRAME_BADBUFSIZE,
	BUILD_ECTP_FRAME_NOMEM
};
enum BUILD_ECTP_FRAME build_ectp_frame(
				   const struct program_parameters *prog_parms,
				   uint8_t frame_buf[],
				   const unsigned int frame_buf_sz,
				   const uint8_t *prog_data,
				   const unsigned int prog_data_size,
				   unsigned int *ectp_frame_len);

void *tx_thread(struct tx_thread_arguments *tx_thread_args);

void *rx_thread(struct rx_thread_arguments *rx_thread_args);

enum ECTP_PKT_VALID {
	ECTP_PKT_VALID_GOOD,
	ECTP_PKT_VALID_TOOSMALL,
	ECTP_PKT_VALID_BADSKIPCOUNT,
	ECTP_PKT_VALID_BADMSGTYPE,
	ECTP_PKT_VALID_WRONGRCPTNUM
};
enum ECTP_PKT_VALID ectp_pkt_valid(const struct ectp_packet *ectp_pkt,
				   const unsigned int ectp_pkt_size,
				   const struct program_parameters *prog_parms,
				   uint8_t **ectp_data,
				   unsigned int *ectp_data_size);

void print_rxed_packet(const struct program_parameters *prog_parms,
		       const struct timeval *pkt_arrived,
		       const struct ether_addr *srcmac,
		       const unsigned int pkt_len,
		       const struct ectp_packet *ectp_pkt,
		       const uint8_t *ectp_data,
		       const unsigned int ectp_data_size);

void print_ectp_src_rt(const struct ectp_packet *ectp_pkt, bool resolve);

void process_rxed_frames(int *rx_sockfd,
			 const struct program_parameters *prog_parms);

void rx_new_packet(int *sockfd,
		  unsigned char *pkt_buf,
		  const unsigned int pkt_buf_sz,
		  struct timeval *pkt_arrived,
		  unsigned char *pkt_type,
		  unsigned int *pkt_len,
		  struct ether_addr *srcmac);

void close_sockets(int *tx_sockfd, int *rx_sockfd);

enum CLOSE_TX_SKT {
	CLOSE_TX_SKT_GOOD,
	CLOSE_TX_SKT_BAD,
};
enum CLOSE_TX_SKT close_tx_socket(int *tx_sockfd);


enum CLOSE_RX_SKT {
	CLOSE_RX_SKT_GOOD,
	CLOSE_RX_SKT_BAD,
};
enum CLOSE_RX_SKT close_rx_socket(int *rx_sockfd);


/*
 * Global Variables
 */

char ectpping_version[] = "ECTPPING version 0.2, 2009-05-09, by Mark Smith"\
			  " <markzzzsmith@yahoo.com.au>";


/*
 * ectpping process id
 */
pid_t ectpping_pid;


/*
 * tx & rx thread handles
 */
pthread_t tx_thread_hdl;
pthread_t rx_thread_hdl;


/*
 * rtt and other stats
 */
unsigned int txed_pkts = 0;
unsigned int rxed_pkts = 0;
struct timeval min_rtt = {
	.tv_sec = INT_MAX,
	.tv_usec = INT_MAX,
};
struct timeval max_rtt = {
	.tv_sec = 0,
	.tv_usec = 0,
};
struct timeval sum_rtts = {
	.tv_sec = 0,
	.tv_usec = 0,
};

/*
 * Program parameters (needs to be global so signal handler can see it)
 */
struct program_parameters prog_parms;

/*
 * Functions
 */

int main(int argc, char *argv[])
{
    struct sigaction sigint_action;
    int tx_sockfd, rx_sockfd;
    struct tx_thread_arguments tx_thread_args;
    struct rx_thread_arguments rx_thread_args;
    unsigned char ectp_data[] =
        __BASE_FILE__ ", built " __TIMESTAMP__ ", using GCC version "
        __VERSION__;
    int ret;
    pthread_attr_t threads_attrs;
	pthread_t rx_thread_hdl;

    get_prog_parms(argc, argv, &prog_parms);

    prog_parms.ectp_user_data = ectp_data;
    prog_parms.ectp_user_data_size = sizeof(ectp_data);

	ret = open_sockets(&tx_sockfd, &rx_sockfd, prog_parms.ifindex);
	if (ret != 0) {
		perror("Failed to open sockets");
		close_sockets(&tx_sockfd, &rx_sockfd);
		return EXIT_FAILURE;
	}

    prepare_thread_args(&tx_thread_args, &rx_thread_args, &prog_parms,
        &tx_sockfd, &rx_sockfd);

    setup_sigint_hdlr(&sigint_action);

    print_prog_header(&prog_parms);

    ectpping_pid = getpid();

    ret = pthread_attr_init(&threads_attrs);
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize thread attributes\n");
        return ret;
    }

    ret = pthread_attr_setschedpolicy(&threads_attrs, SCHED_FIFO);
    if (ret != 0) {
        fprintf(stderr, "Failed to set thread scheduling policy\n");
        pthread_attr_destroy(&threads_attrs);
        return ret;
    }

	// Create the transmitter thread
	ret = pthread_create(&tx_thread_hdl, &threads_attrs, tx_thread, &tx_thread_args);
	if (ret != 0) {
		fprintf(stderr, "Failed to create tx thread\n");
		if (pthread_attr_destroy(&threads_attrs) != 0) {
			fprintf(stderr, "Failed to destroy thread attributes\n");
		}
		return ret;
	}

    // Create the receiver thread
    ret = pthread_create(&rx_thread_hdl, &threads_attrs, rx_thread, &rx_thread_args);
    if (ret != 0) {
        fprintf(stderr, "Failed to create rx thread\n");
        pthread_attr_destroy(&threads_attrs);
        return ret;
    }

    // Wait for threads to finish
    pthread_join(tx_thread_hdl, NULL);
    pthread_join(rx_thread_hdl, NULL);

    pthread_attr_destroy(&threads_attrs);

    close_sockets(&tx_sockfd, &rx_sockfd);

    return EXIT_SUCCESS;
}

/*
 * Setup things needed for the sigint handler
 */
void setup_sigint_hdlr(struct sigaction *sigint_action)
{


	set_sigint_hdlr(sigint_action);

}


/*
 * Print the supplied mac address, and if an entry exists
 * in /etc/ethers, print that too. Doesn't do any line feeding,
 * tabbing etc.
 */
void print_ethaddr_hostname(const struct ether_addr *ethaddr, bool resolve)
{
	char macpbuf[ENET_PADDR_MAXSZ];
	char machostn[1024]; /* see ether_ntoh.c in glibc for size */


	enet_ntop(ethaddr, ENET_NTOP_UNIX, macpbuf,
		ENET_PADDR_MAXSZ);

	printf("%s", macpbuf);

	if (resolve && (ether_ntohost(machostn, ethaddr) == 0))
		printf(" (%s)", machostn);

}


/*
 * Print program header text
 */
void print_prog_header(const struct program_parameters *prog_parms)
{


	printf("ECTPPING ");

	print_ethaddr_hostname(&prog_parms->dstmac,
		!prog_parms->no_resolve);
		
	printf(" using %s\n", prog_parms->iface);

}


/*
 * Replaces SIGINT handler
 */
void set_sigint_hdlr(struct sigaction *sigint_action)
{


	sigint_action->sa_handler = sigint_hdlr;
	sigemptyset(&(sigint_action->sa_mask));
	sigint_action->sa_flags = 0;

	sigaction(SIGINT, sigint_action, NULL);

}


/*
 * Called upon SIGINT 
 */
void sigint_hdlr(int signum)
{


	pthread_cancel(tx_thread_hdl);

	if (rxed_pkts != txed_pkts)
		usleep(100000); /* 100ms delay to try to catch an in flight
				 * pkts */

	pthread_cancel(rx_thread_hdl);

	if (prog_parms.fwdaddrs != NULL)
		free(prog_parms.fwdaddrs);

	putchar('\n');

	fflush(NULL);


	printf("---- ");

	print_ethaddr_hostname(&prog_parms.dstmac,
		!prog_parms.no_resolve);

	printf(" ECTPPING Statistics ----\n");


	printf("%d packets transmitted, %d packets received", txed_pkts,
		rxed_pkts);
		
	if (txed_pkts > 0) {
		if (rxed_pkts <= txed_pkts) 			
			printf(", %f%% packet loss\n",
				((txed_pkts - rxed_pkts) / (txed_pkts * 1.0))
					* 100);
		else
			printf(", %.2f times packet increase\n",
				(rxed_pkts / (txed_pkts * 1.0)));

		if (rxed_pkts > 0) {
			long sum_rtts_sec_avg = (sum_rtts.tv_sec * 1000000)/
				rxed_pkts;
			
			printf("round-trip (sec)  min/avg/max/total = "
				"%ld.%06ld/%ld.%06ld/%ld.%06ld/%ld.%06ld\n",
				min_rtt.tv_sec, min_rtt.tv_usec,
				sum_rtts.tv_sec/rxed_pkts,
				(sum_rtts_sec_avg < 1000000 ?
					(sum_rtts.tv_usec/rxed_pkts) +
					sum_rtts_sec_avg :
					sum_rtts.tv_usec/rxed_pkts),
				max_rtt.tv_sec, max_rtt.tv_usec,
				sum_rtts.tv_sec, sum_rtts.tv_usec); 
		}
	} else {
		putchar('\n');
	}

	fflush(NULL);

	exit(EXIT_SUCCESS);

}


/*
 * Routine to collect program parameters from various sources e.g. cli
 * options, .rc file
 */
enum GET_PROG_PARMS get_prog_parms(const int argc,
				   char *argv[],
				   struct program_parameters *prog_parms)
{
	struct program_options prog_opts;
	int erropt;
	const char *errmsg;
	enum PROCESS_PROG_OPTS process_prog_opts_ret;

	set_default_prog_opts(&prog_opts);

	get_cli_opts_eh(get_cli_opts(argc, argv, &prog_opts, &erropt), &erropt);

	process_prog_opts_ret = process_prog_opts(&prog_opts, prog_parms,
			&errmsg);
	process_prog_opts_eh(process_prog_opts_ret, errmsg);

	return GET_PROG_PARMS_GOOD;

}


/*
 * Set some reasonable program option defaults
 */ 
void set_default_prog_opts(struct program_options *prog_opts)
{
	char *default_iface = "eth0";


	memset(prog_opts, 0, sizeof(struct program_options));

	/* default interface */
	strncpy(prog_opts->iface, default_iface, IFNAMSIZ);
	prog_opts->iface[IFNAMSIZ-1] = '\0';

	prog_opts->dst_type = mcast;

	prog_opts->no_resolve = false;

	prog_opts->zero_pkt_output = false;

	prog_opts->interval_ms = 1000;

	prog_opts->fwdaddrs_str = NULL;

}


/*
 * Routine to collect program options from *argv[];
 */
enum GET_CLI_OPTS get_cli_opts(const int argc,
			       char *argv[],
			       struct program_options *prog_opts,
			       int *erropt)
{
	int opt;


	opterr = 0;

	while ((opt = getopt(argc, argv, ":i:bnzI:f:h")) != -1) {
		switch (opt) {
		case 'i':
			strncpy(prog_opts->iface, optarg, IFNAMSIZ);
			prog_opts->iface[IFNAMSIZ-1] = 0;
			break;
		case 'b':
			prog_opts->dst_type = bcast;
			break;
		case 'n':
			prog_opts->no_resolve = true;
			break;
		case 'z':
			prog_opts->zero_pkt_output = true;
			break;
		case 'I':
			if (getuid() != 0) {
				*erropt = 'I';
				return GET_CLI_OPTS_BAD_NEED_UID_0;
			}
			prog_opts->interval_ms = atoi(optarg);
			break;
		case 'f':
			prog_opts->fwdaddrs_str = optarg;
			break;
		case '?':
			*erropt = optopt;
			return GET_CLI_OPTS_BAD_UNKNOWN_OPT;
			break;
		case ':':
			*erropt = optopt;
			return GET_CLI_OPTS_BAD_MISSING_ARG;
			break;
		case 'h':
			return GET_CLI_OPTS_BAD_HELP;
		}
	}

	/* first non-opt is assumed to be dest mac addr */
	if (optind < argc) { 
		prog_opts->dst_type = ucast;
		prog_opts->uc_dst_str = argv[optind];
	}

	return GET_CLI_OPTS_GOOD;

}


/*
 * Error handler for get_cli_opts()
 *
 */
enum GET_CLI_OPTS get_cli_opts_eh(const enum GET_CLI_OPTS ret,
				  int *erropt)
{


	switch (ret) {
	case GET_CLI_OPTS_BAD_UNKNOWN_OPT:
		fprintf(stderr, "-%c: Unknown option\n", *erropt);	
		exit(EXIT_FAILURE);
		break;
	case GET_CLI_OPTS_BAD_MISSING_ARG:
		fprintf(stderr, "-%c: Missing option argument\n", *erropt);	
		exit(EXIT_FAILURE);
		break;
	case GET_CLI_OPTS_BAD_NEED_UID_0:
		fprintf(stderr, "-%c: Need to be root user (getuid() == 0) to "
				"use this option\n",
			*erropt);	
		exit(EXIT_FAILURE);
		break;
	case GET_CLI_OPTS_BAD_HELP:
		print_help();
		exit(EXIT_FAILURE);
		break;
	default:
		return ret;
	}

}

void print_help(void)
{


	fprintf(stderr, "\n%s\n\n", ectpping_version);

	fprintf(stderr, "ectpping [options] [<unicast MAC address>|"
			"</etc/ethers hostname>]\n\n");

	fprintf(stderr, "ECTPPING options\n");
	fprintf(stderr, "-i <intf>\t: Network interface to use. Default is "
			"eth0.\n");
	fprintf(stderr, "-b\t\t: Use broadcast ECTP packet instead of "
			"multicast ECTP packet.\n");
	fprintf(stderr, "-n\t\t: Don't resolve names using /etc/ethers.\n"
			"\t\t  See ethers(5) for details.\n");
	fprintf(stderr, "-z\t\t: Zero output of per packet "
				"responses.\n");
	fprintf(stderr, "-I <ms>\t\t: Milliseconds between packet "
			"transmits. Default is 1000.\n");
	fprintf(stderr, "\t\t  Need to be root i.e. getuid() == 0 to use this "
			"option.\n");
	fprintf(stderr, "-f \"fwdaddr1 ... fwdaddrN\"\n\t\t: "
			"List of up to 10 forward addresses in the ECTP packet.\n");
	fprintf(stderr, "\t\t  The first forward address specified is not used"
			" as the first\n");
	fprintf(stderr, "\t\t  ECTP hop i.e. the destination MAC address in "
		        "the address \n");
        fprintf(stderr, "\t\t  the transmitted ECTP packet header. That will need to be\n");
        fprintf(stderr, "\t\t  specified separately on the command line.\n");
        fprintf(stderr, "\t\t  To have the ECTP packet follow a complete loop, "
			"specify this\n");
       	fprintf(stderr, "\t\t  host's outgoing interface MAC address as the "
			"last hop.\n");

	fprintf(stderr, "\n");

}	


/*
 * Routine to convert collected program options into internal program
 * parameters
 */
enum PROCESS_PROG_OPTS process_prog_opts(const struct program_options
						*prog_opts,
					 struct program_parameters *prog_parms,
					 const char **errmsg)
{
	const uint8_t bcast_addr[ETH_ALEN] = { 0xff, 0xff, 0xff,
					       0xff, 0xff, 0xff };
	const uint8_t lc_mcaddr[ETH_ALEN] = ECTP_LA_MCADDR;

	
	

	memset(prog_parms, 0, sizeof(struct program_parameters));

	if (get_ifindex(prog_opts->iface, &prog_parms->ifindex)
		!= GET_IFINDEX_GOOD) {
		*errmsg = prog_opts->iface;
		return PROCESS_PROG_OPTS_BAD_IFACE;
	}

	if (get_ifmac(prog_opts->iface, &prog_parms->srcmac)
		!= GET_IFMAC_GOOD) {
		*errmsg = prog_opts->iface;
		return PROCESS_PROG_OPTS_BAD_IFMAC;
	}

	strncpy(prog_parms->iface, prog_opts->iface, IFNAMSIZ);
	prog_parms->iface[IFNAMSIZ-1] = '\0';

	switch (prog_opts->dst_type) {
	case ucast:
		prog_parms->uc_dstmac = true;
		if (ether_hostton(prog_opts->uc_dst_str,
			&prog_parms->dstmac) == 0) {
			break;
		}
		if (enet_pton(prog_opts->uc_dst_str,
			&prog_parms->dstmac) !=
			ENET_PTON_GOOD) {
			return PROCESS_PROG_OPTS_BAD_DSTMACFMT;
		}
		break;
	case bcast:
		prog_parms->uc_dstmac = false;
		memcpy(&prog_parms->dstmac, bcast_addr,
			sizeof(struct ether_addr));
		break;
	case mcast:
	default:
		prog_parms->uc_dstmac = false;
		memcpy(&prog_parms->dstmac, lc_mcaddr,
			sizeof(struct ether_addr));
		break;
	}

	prog_parms->no_resolve = prog_opts->no_resolve;

	prog_parms->zero_pkt_output = prog_opts->zero_pkt_output;

	prog_parms->interval_ms = prog_opts->interval_ms;

	if (prog_opts->fwdaddrs_str != NULL) {
		get_prog_opt_fwdaddrs(prog_opts->fwdaddrs_str,
			&prog_parms->fwdaddrs,
			&prog_parms->num_fwdaddrs);
	} else {
		prog_parms->fwdaddrs = NULL;
		prog_parms->num_fwdaddrs = 0;
	}

	return PROCESS_PROG_OPTS_GOOD;

}


/*
 * Error handler for process_prog_opts() routine
 */
enum PROCESS_PROG_OPTS process_prog_opts_eh(const enum PROCESS_PROG_OPTS ret,
					    const char *errmsg)
{


	switch (ret) {
	case PROCESS_PROG_OPTS_BAD_IFACE: 
		fprintf(stderr, "Unknown interface or interface "
				"inaccessible (got CAP_NET_RAW?) - %s.\n",
				errmsg);
		exit (EXIT_FAILURE);
		break;
	case PROCESS_PROG_OPTS_BAD_IFMAC:
		fprintf(stderr, "Error retrieving interface MAC address.\n");
		exit (EXIT_FAILURE);
		break;
	case PROCESS_PROG_OPTS_BAD_DSTMACFMT:
		fprintf(stderr, "Bad destination MAC address format.\n");
		exit (EXIT_FAILURE);
		break;
	default:
		return ret;
	}

}


/*
 * routine to convert fwdaddr string into an array of mac addresses
 * n.b. allocates space for the array via calloc if there is at least one
 * mac address, so free() must be called on *fwdaddrs at some point in the
 * future
 */
enum GET_PROG_OPT_FWDADDRS get_prog_opt_fwdaddrs(const char *fwdaddrs_str,
						 struct ether_addr **fwdaddrs,
						 unsigned int *num_fwdaddrs)
{
	char fa_str[10][50];
	struct ether_addr *j;
	unsigned int i, k;


	*fwdaddrs = calloc(10, ETH_ALEN);
	if (*fwdaddrs == NULL)
		return GET_PROG_OPT_FWDADDRS_BAD;

	memset(fa_str, 0, sizeof(fa_str));

	sscanf(fwdaddrs_str, "%s %s %s %s %s %s %s %s %s %s", fa_str[0],
		fa_str[1], fa_str[2], fa_str[3], fa_str[4], fa_str[5],
		fa_str[6], fa_str[7], fa_str[8], fa_str[9]);

	j = *fwdaddrs;
	k = 0;
	for (i = 0; i < 10; i++) {
		if (enet_pton(fa_str[i], j) == ENET_PTON_GOOD) {
			j++;
			k++;
		} else {
			if (ether_hostton(fa_str[i], j) == 0) {
				j++;
				k++;
			}
		}
	}

	*num_fwdaddrs = k;

	return GET_PROG_OPT_FWDADDRS_GOOD;

}


/*
 * Routine to perform the specified interface ioctl
 */
enum DO_IFREQ_IOCTL do_ifreq_ioctl(const int ioctl_request,
				   const char iface[IFNAMSIZ],
				   struct ifreq *ifr)
{
	int sockfd;
	int ioctlret;
	int enable = 1;



	sockfd = socket(AF_PACKET, SOCK_RAW, 0);
	if (sockfd == -1)
		return DO_IFREQ_IOCTL_BADSOCKET;

	
	if (setsockopt(sockfd, SOL_SOCKET, SO_TIMESTAMP, &enable, sizeof(enable)) < 0) {
    	perror("setsockopt");
    	exit(EXIT_FAILURE);
	}	

	memset(ifr, 0, sizeof(struct ifreq));

	strncpy(ifr->ifr_name, iface, IFNAMSIZ);
	ifr->ifr_name[IFNAMSIZ-1] = 0;

	ioctlret = ioctl(sockfd, ioctl_request, ifr);

	if (close(sockfd) == -1)
		return DO_IFREQ_IOCTL_BADSOCKET;

	if (ioctlret == -1)
		return DO_IFREQ_IOCTL_BADIOCTL;
	else
		return DO_IFREQ_IOCTL_GOOD;

}


/*
 * Routine to get the ifindex of the supplied interface name
 */
enum GET_IFINDEX get_ifindex(const char iface[IFNAMSIZ], int *ifindex)
{
	struct ifreq ifr;


	if (do_ifreq_ioctl(SIOCGIFINDEX, iface, &ifr) == DO_IFREQ_IOCTL_GOOD) {
		*ifindex = ifr.ifr_ifindex;
		return GET_IFINDEX_GOOD;
	} else {
		return GET_IFINDEX_BADIFACE;
	}

}


/*
 * Routine to get the mac address for an interface
 */
enum GET_IFMAC get_ifmac(const char iface[IFNAMSIZ],
			 struct ether_addr *ifmac)
{
	struct ifreq ifr;


	if (do_ifreq_ioctl(SIOCGIFHWADDR, iface, &ifr) == DO_IFREQ_IOCTL_GOOD) {
		if (ifr.ifr_hwaddr.sa_family == ARPHRD_ETHER) {
			memcpy(ifmac, ifr.ifr_hwaddr.sa_data,
				sizeof(struct ether_addr));
			return GET_IFMAC_GOOD;
		} else {
			return GET_IFMAC_BADIFACE;
		}
	} else {
		return GET_IFMAC_BADIFACE;
	}

}


/*
 * Routine to open TX and RX PF_PACKET sockets
 */
int open_sockets(int *tx_sockfd, int *rx_sockfd, int ifindex) {
    struct sockaddr_ll sa_ll;

    // Create the transmit socket
    *tx_sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (*tx_sockfd == -1) {
        perror("socket");
        return -1;
    }

    // Create the receive socket
    *rx_sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (*rx_sockfd == -1) {
        perror("socket");
        close(*tx_sockfd);
        return -1;
    }

    // Bind the receive socket to the interface
    memset(&sa_ll, 0, sizeof(sa_ll));
    sa_ll.sll_family = AF_PACKET;
    sa_ll.sll_ifindex = ifindex;
    sa_ll.sll_protocol = htons(ETH_P_ALL);

    if (bind(*rx_sockfd, (struct sockaddr *)&sa_ll, sizeof(sa_ll)) == -1) {
        perror("bind");
        close(*tx_sockfd);
        close(*rx_sockfd);
        return -1;
    }

    return 0;
}


/*
 * Prepares the argument structures passed to the tx and rx threads
 */
void prepare_thread_args(struct tx_thread_arguments *tx_thread_args,
			 struct rx_thread_arguments *rx_thread_args,
			 struct program_parameters *prog_parms,
			 int *tx_sockfd,
			 int *rx_sockfd)
{


	tx_thread_args->prog_parms = prog_parms;
	tx_thread_args->tx_sockfd = tx_sockfd;

	rx_thread_args->prog_parms = prog_parms;
	rx_thread_args->rx_sockfd = rx_sockfd;

}


/*
 * Build the ectp frame ethernet header
 */
void build_ectp_eth_hdr(const struct ether_addr *srcmac,
			const struct ether_addr *dstmac,
			struct ether_header *eth_hdr)
{


	memcpy(eth_hdr->ether_shost, srcmac, sizeof(struct ether_addr));
	memcpy(eth_hdr->ether_dhost, dstmac, sizeof(struct ether_addr));

	eth_hdr->ether_type = htons(ETHERTYPE_LOOPBACK);

}


enum BUILD_ECTP_FRAME build_ectp_frame(
				   const struct program_parameters *prog_parms,
				   uint8_t frame_buf[],
				   const unsigned int frame_buf_sz,
				   const uint8_t *prog_data,
				   const unsigned int prog_data_size,
				   unsigned int *ectp_frame_len)
{
	unsigned int ectp_pkt_len;
	unsigned int frame_payload_size;
	uint8_t *frame_payload;
	const struct ether_addr *fwdaddrs;
	unsigned int num_fwdaddrs;


	if (sizeof(struct ether_header) > frame_buf_sz)
		return BUILD_ECTP_FRAME_BADBUFSIZE;

	build_ectp_eth_hdr(&prog_parms->srcmac, &prog_parms->dstmac,
		(struct ether_header *)&frame_buf[0]);
	
	frame_payload_size = prog_data_size + prog_parms->ectp_user_data_size;

	ectp_pkt_len = ectp_calc_packet_size(1, frame_payload_size);

	if (ectp_pkt_len > (frame_buf_sz - ETH_HLEN))
		return BUILD_ECTP_FRAME_BADBUFSIZE;

	frame_payload = malloc(frame_payload_size);
	if (frame_payload == NULL)
		return BUILD_ECTP_FRAME_NOMEM;

	memcpy(frame_payload, prog_data, prog_data_size);
	memcpy(&frame_payload[prog_data_size], prog_parms->ectp_user_data,
		prog_parms->ectp_user_data_size);

	if (prog_parms->num_fwdaddrs) {
		num_fwdaddrs = prog_parms->num_fwdaddrs;
		fwdaddrs = prog_parms->fwdaddrs;
	} else {
		num_fwdaddrs = 1;
		fwdaddrs = &prog_parms->srcmac;
	}

	ectp_build_packet(0, fwdaddrs, num_fwdaddrs, ectpping_pid,
		frame_payload,
		frame_payload_size, &frame_buf[ETH_HLEN],
		frame_buf_sz - ETH_HLEN, 0x00);

	*ectp_frame_len = ETH_HLEN + ectp_pkt_len;

	free(frame_payload);

	return BUILD_ECTP_FRAME_GOOD;

}


/*
 * ECTP frame sender thread
 */
void *tx_thread(void *arg) {
    struct tx_thread_arguments *tx_args = (struct tx_thread_arguments *)arg;
    uint8_t tx_frame_buf[0xffff];
    unsigned int ectp_frame_len;
    struct ectpping_payload eping_payload = {
        .seq_num = 0,
    };

    while (true) {
        gettimeofday(&eping_payload.tv, NULL);

        build_ectp_frame(tx_args->prog_parms, tx_frame_buf,
            sizeof(tx_frame_buf), (uint8_t *)&eping_payload,
            sizeof(struct ectpping_payload),
            &ectp_frame_len);

        send(*tx_args->tx_sockfd, tx_frame_buf, ectp_frame_len,
            MSG_DONTWAIT);

        printf("Sending packet: seq_num=%u, timestamp=%ld.%06ld\n",
            eping_payload.seq_num, (long)eping_payload.tv.tv_sec, (long)eping_payload.tv.tv_usec);

        txed_pkts++;

        eping_payload.seq_num++;

        usleep(tx_args->prog_parms->interval_ms * 1000);
    }

    return NULL;
}

/*
 * ECTP frame receiver thread
 */
void *rx_thread(void *arg) {
    struct rx_thread_arguments *rx_args = (struct rx_thread_arguments *)arg;
    process_rxed_frames(rx_args->rx_sockfd, rx_args->prog_parms);
    return NULL;
}




/*
 * Opens the transmit socket used by the tx thread
 */
enum OPEN_TX_SKT open_tx_socket(int *tx_sockfd, const int tx_ifindex)
{
	struct sockaddr_ll sa_ll;


	*tx_sockfd = socket(PF_PACKET, SOCK_RAW, 0);
	if (*tx_sockfd == -1)
		return OPEN_TX_SKT_BADSOCKET;

	memset(&sa_ll, 0, sizeof(sa_ll));
	sa_ll.sll_family = PF_PACKET;
	sa_ll.sll_ifindex = tx_ifindex;

	if ( bind(*tx_sockfd, (struct sockaddr *) &sa_ll, sizeof(sa_ll)) != 0) 
		return OPEN_TX_SKT_BADIFINDEX;

	/* if we get here, all's good */
	return OPEN_TX_SKT_GOOD;

}


/*
 * Opens the receive socket used by the rx thread
 */
enum OPEN_RX_SKT open_rx_socket(int *rx_sockfd, const int rx_ifindex)
{
	struct sockaddr_ll sa_ll;


	*rx_sockfd = socket(PF_PACKET, SOCK_DGRAM, htons(ETHERTYPE_LOOPBACK));
	if (*rx_sockfd == -1)
		return OPEN_RX_SKT_BADSOCKET;

	memset(&sa_ll, 0, sizeof(sa_ll));
	sa_ll.sll_family = PF_PACKET;
	sa_ll.sll_ifindex = rx_ifindex;

	if ( bind(*rx_sockfd, (struct sockaddr *) &sa_ll, sizeof(sa_ll)) != 0) 
		return OPEN_RX_SKT_BADIFINDEX;

	/* if we get here, all's good */
	return OPEN_RX_SKT_GOOD;

}


/*
 * Validate the supplied ECTP packet, using the program parameters
 * to determine some of the validation tests
 */
enum ECTP_PKT_VALID ectp_pkt_valid(const struct ectp_packet *ectp_pkt,
				   const unsigned int ectp_pkt_size,
				   const struct program_parameters *prog_parms,
				   uint8_t **ectp_data,
				   unsigned int *ectp_data_size)
{
	unsigned int looklen;
	unsigned int skipcount;
	struct ectp_message *curr_ectp_msg;


	if (ectp_pkt_size < ECTP_PACKET_HDR_SZ)
		return ECTP_PKT_VALID_TOOSMALL;

	looklen = ECTP_PACKET_HDR_SZ;

	skipcount = ectp_get_skipcount(ectp_pkt);

	if (!ectp_skipc_basicchk_ok(skipcount, ectp_pkt_size))
		return ECTP_PKT_VALID_BADSKIPCOUNT;

	looklen += skipcount + ECTP_MSG_FUNC_SZ;

	if (looklen >= ectp_pkt_size)
		return ECTP_PKT_VALID_TOOSMALL;

	curr_ectp_msg = ectp_get_curr_msg_ptr(ectp_pkt);

	if (ectp_get_msg_type(curr_ectp_msg) != ECTP_RPLYMSG)
		return ECTP_PKT_VALID_BADMSGTYPE;

	looklen += sizeof(struct ectp_reply_message);

	if (looklen >= ectp_pkt_size)
		return ECTP_PKT_VALID_TOOSMALL;

	if(ectp_get_rplymsg_rcpt_num(curr_ectp_msg) != ectpping_pid)
		return ECTP_PKT_VALID_WRONGRCPTNUM;

	*ectp_data_size = ectp_pkt_size - looklen;

	*ectp_data = ectp_get_rplymsg_data_ptr(curr_ectp_msg);

	return ECTP_PKT_VALID_GOOD;

}


/*
 * Print data about received packet
 */
void print_rxed_packet(const struct program_parameters *prog_parms,
		       const struct timeval *pkt_arrived,
		       const struct ether_addr *srcmac,
		       const unsigned int pkt_len,
		       const struct ectp_packet *ectp_pkt,
		       const uint8_t *ectp_data,
		       const unsigned int ectp_data_size)
{
	struct ectpping_payload eping_payload;
	struct timeval tv_diff;


	memcpy(&eping_payload, ectp_data, sizeof(struct ectpping_payload));
	timersub(pkt_arrived, &eping_payload.tv, &tv_diff);

	timeradd(&sum_rtts, &tv_diff, &sum_rtts);

	if ((tv_diff.tv_sec < min_rtt.tv_sec) ||
	    ((tv_diff.tv_sec == min_rtt.tv_sec) &&
	     (tv_diff.tv_usec < min_rtt.tv_usec)))
		min_rtt = tv_diff;

	if ((tv_diff.tv_sec > max_rtt.tv_sec) ||
	    ((tv_diff.tv_sec == max_rtt.tv_sec) &&
	     (tv_diff.tv_usec > max_rtt.tv_usec)))
		max_rtt = tv_diff;

	if (!prog_parms->zero_pkt_output) {

		printf("%d bytes from ", pkt_len);
				
		print_ethaddr_hostname(srcmac,
			!prog_parms->no_resolve);
				
		printf(": ectp_seq=%d time=%ld.%06ld sec\n",
			eping_payload.seq_num,
			tv_diff.tv_sec,
			tv_diff.tv_usec);

		if (ectp_get_skipcount(ectp_pkt) > 8)
			print_ectp_src_rt(ectp_pkt, !prog_parms->no_resolve);

		fflush(NULL);

	}

}


void print_ectp_src_rt(const struct ectp_packet *ectp_pkt, bool resolve)
{
	unsigned int skipcount = 0;
	struct ectp_message *ectp_msg;


	ectp_msg = ectp_get_msg_ptr(skipcount, ectp_pkt);
	while (ectp_get_msg_type(ectp_msg) == ECTP_FWDMSG) {

		printf("\t\t\tfwdaddr: ");
		
		print_ethaddr_hostname((struct ether_addr *)ectp_get_fwdaddr(ectp_msg),
			resolve);

		putchar('\n');

		skipcount += ECTP_FWDMSG_SZ;
		ectp_msg = ectp_get_msg_ptr(skipcount, ectp_pkt);
	}

	putchar('\n');

}


/*
 * Wait for incoming ECTP frames, and print their details when received
 */
void process_rxed_frames(int *rx_sockfd, struct program_parameters *prog_parms) {
    struct timeval tv;
    struct msghdr msg;
    struct iovec iov;
    char buf[2048];
    char control[1024];
    struct cmsghdr *cmsg;
    struct sockaddr_ll sa_ll;
    unsigned int sa_ll_len;
    unsigned char pkt_type;
    unsigned int pkt_len;
    struct ether_addr srcmac;

    while (true) {
        iov.iov_base = buf;
        iov.iov_len = sizeof(buf);
        msg.msg_name = &sa_ll;
        msg.msg_namelen = sizeof(sa_ll);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        msg.msg_flags = 0;

        pkt_len = recvmsg(*rx_sockfd, &msg, 0);
        if (pkt_len < 0) {
            perror("recvmsg");
            continue;
        }

        // Extract the timestamp
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMP) {
                memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
                break;
            }
        }

        memcpy(&srcmac, sa_ll.sll_addr, sizeof(struct ether_addr));
        pkt_type = sa_ll.sll_pkttype;

        printf("Received packet: src_mac=%s, length=%u, timestamp=%ld.%06ld\n",
            ether_ntoa(&srcmac), pkt_len, (long)tv.tv_sec, (long)tv.tv_usec);

        // Process the received packet
        // process_packet(buf, pkt_len);
    }
}


/*
 * Receive a pending ECTP frame
 */
void rx_new_packet(int *rx_sockfd,
		  unsigned char *pkt_buf,
		  const unsigned int pkt_buf_sz,
		  struct timeval *pkt_arrived,
		  unsigned char *pkt_type,
		  unsigned int *pkt_len,
		  struct ether_addr *srcmac)
{
	struct sockaddr_ll sa_ll;
	unsigned int sa_ll_len;
	struct msghdr msg;
    struct iovec iov;
    char control[1024];
    struct cmsghdr *cmsg;

	memset(pkt_buf, 0, pkt_buf_sz);

	sa_ll_len = sizeof(sa_ll);

	iov.iov_base = pkt_buf;
    iov.iov_len = pkt_buf_sz;
    msg.msg_name = &sa_ll;
    msg.msg_namelen = sa_ll_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    msg.msg_flags = 0;

	*pkt_len = recvmsg(*rx_sockfd, &msg, 0);
    if (*pkt_len < 0) {
        perror("recvmsg");
        return;
    }

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMP) {
            memcpy(pkt_arrived, CMSG_DATA(cmsg), sizeof(struct timeval));
            break;
        }
    }

	memcpy(srcmac, sa_ll.sll_addr, sizeof(struct ether_addr));

	*pkt_type = sa_ll.sll_pkttype;	
	
}


/*
 * Close tx & rx sockets
 */
void close_sockets(int *tx_sockfd, int *rx_sockfd)
{


	close_tx_socket(tx_sockfd);
	close_rx_socket(rx_sockfd);

}


/*
 * Close the transmit socket
 */
enum CLOSE_TX_SKT close_tx_socket(int *tx_sockfd)
{


	if (close(*tx_sockfd) == -1) 
		return CLOSE_TX_SKT_BAD;

	/* all's good if we get here */
	return CLOSE_TX_SKT_GOOD;

}


/*
 * Close the receive socket
 */
enum CLOSE_RX_SKT close_rx_socket(int *rx_sockfd)
{


	if (close(*rx_sockfd) == -1) 
		return CLOSE_RX_SKT_BAD;

	/* all's good if we get here */
	return CLOSE_RX_SKT_GOOD;

}
