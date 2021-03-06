#include "server_stat.h"
#include "server.h"
#include "server_stat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>


/**
 * Wrapper around the sendto function that handles timed statistics
 *
 * @param s the server
 * @param buf the data
 * @param len the length of buf
 * @param flags sendto flags (see man(2) sendto)
 * @param dest_addr the destination address
 * @param addrlen the length of dest_addr
 *
 * @return the number of characters sent, or -1
 */
ssize_t send_to(struct server *s, const void *buf, size_t len, int flags,
		const struct sockaddr *dest_addr, socklen_t addrlen)
{
	ssize_t ret;

	sstat_add_packet(s->stats, len, 1);
	ret = sendto(s->socket_desc, buf, len, flags, dest_addr, addrlen);
	if (ret == -1)
		printf("(WW) send_to failed : %s\n", strerror(errno));
	return ret;
}

/**
 * Allocate and initializes a server statistics structure.
 *
 * @return the allocated statistics
 */
struct server_stat *new_sstat()
{
	struct server_stat *st;

	st = (struct server_stat *)calloc(1, sizeof(struct server_stat));
	if (st == NULL) {
		printf("(WW) new_sstat, calloc of st failed : %s.\n", strerror(errno));
		return NULL;
	}
	st->pkt_max = 10000;
	st->pkt_sizes = (size_t *)calloc(st->pkt_max, sizeof(size_t));
	st->pkt_timestamps = (struct timeval *)calloc(st->pkt_max, sizeof(struct timeval));
	st->pkt_io = (char *)calloc(st->pkt_max, sizeof(char));
	st->start_time = time(NULL);

	if (st->pkt_sizes == NULL || st->pkt_timestamps == NULL || st->pkt_io == NULL) {
		printf("(WW) new_sstat, calloc failed : %s.\n", strerror(errno));
		if (st->pkt_sizes != NULL)
			free(st->pkt_sizes);
		if (st->pkt_timestamps != NULL)
			free(st->pkt_timestamps);
		if (st->pkt_io != NULL)
			free(st->pkt_io);
		free(st);
		return NULL;
	}
	return st;
}

/**
 * Add a packet to the statistics :
 * - add its size to the total size
 * - increment the counter
 * - insert it into the timed statistics
 *
 * @param st the server statistics
 * @param size the size of the packet
 * @param in_out 0 if input packet, 1 if output packet
 */
void sstat_add_packet(struct server_stat *st, size_t size, char in_out)
{
	struct timeval now, res;
	size_t *tmp_sizes;
	char *tmp_io;
	struct timeval *tmp_timestamps;
	int i;

	if (in_out == 1) {
		st->pkt_sent++;
		st->size_sent += size;
	} else if (in_out == 0) {
		st->pkt_rec++;
		st->size_rec += size;
	}

	gettimeofday(&now, NULL);

	/* Insert in the table if possible */
	for (i = 0 ; i < st->pkt_max ; i++) {
		timersub(&now, &st->pkt_timestamps[i], &res);
		if (st->pkt_sizes[i] == 0 || res.tv_sec > 60) {
			st->pkt_sizes[i] = size;
			gettimeofday(&st->pkt_timestamps[i], NULL);
			st->pkt_io[i] = in_out;
			return;
		}
	}
	st->pkt_max *= 2;
	/* reallocate and clean */
	tmp_sizes = (size_t *)realloc(st->pkt_sizes, sizeof(size_t) * st->pkt_max);
	if (tmp_sizes == NULL) {
		printf("(WW) sstat_add_packet, pkt_sizes realloc failed : %s.\n", strerror(errno));
		st->pkt_max /= 2;	/* restore the old size */
		return;
	}
	st->pkt_sizes = tmp_sizes;
	bzero(st->pkt_sizes + (st->pkt_max / 2), sizeof(size_t) * st->pkt_max / 2); /* realloc does not set to zero! */

	/* reallocate */
	tmp_timestamps = (struct timeval *)realloc(st->pkt_timestamps, sizeof(struct timeval) * st->pkt_max);
	if (tmp_timestamps == NULL) {
		printf("(WW) sstat_add_packet, pkt_timestamps realloc failed : %s.\n", strerror(errno));
		st->pkt_max /= 2;
		return;
	}
	st->pkt_timestamps = tmp_timestamps;

	tmp_io = (char *)realloc(st->pkt_io, sizeof(char) * st->pkt_max);
	if (tmp_io == NULL) {
		printf("(WW) sstat_add_packet, pkt_io realloc failed : %s.\n", strerror(errno));
		st->pkt_max /= 2;
		return;
	}
	st->pkt_io = tmp_io;

	/* insert */
	st->pkt_sizes[(st->pkt_max / 2) + 1] = size;
	gettimeofday(&st->pkt_timestamps[(st->pkt_max / 2) + 1], NULL);
	st->pkt_io[i] = in_out;
}

/**
 * Compute time relative statistics (bytes/sec or bytes/min)
 *
 * @param st the server statistics
 * @param stats the results
 */
void compute_timed_stats(struct server_stat *st, uint32_t *stats)
{
	struct timeval now, res;
	int i;

	gettimeofday(&now, NULL);
	/* res[0] = Rx / sec
	 * res[1] = Tx / sec
	 * res[2] = Rx / min
	 * res[3] = Tx / sec */
	for (i = 0 ; i < st->pkt_max ; i++) {
		timersub(&now, &st->pkt_timestamps[i], &res);
		if (res.tv_sec < 60) {
			stats[2 + st->pkt_io[i]] += st->pkt_sizes[i];
			if (res.tv_sec < 1) {
				stats[0 + st->pkt_io[i]] += st->pkt_sizes[i];
			}
		} else {
			/* clear */
			st->pkt_sizes[i] = 0;
		}
	}
}
