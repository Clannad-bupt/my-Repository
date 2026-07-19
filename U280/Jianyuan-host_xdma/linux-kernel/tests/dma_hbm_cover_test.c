// SPDX-License-Identifier: GPL-2.0
/*
 * XDMA memory-mapped HBM coverage R/W verify test
 *
 * Writes deterministic data patterns into a memory-mapped card address space
 * (e.g. an 8GB HBM window starting at address 0) through /dev/xdma*_h2c_*
 * and reads back through /dev/xdma*_c2h_* to verify correctness.
 *
 * This is intended for AXI-MM designs (XDMA memory mapped).
 */

#define _GNU_SOURCE
/* For pread/pwrite, clock_gettime/CLOCK_MONOTONIC, posix_memalign, getopt */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  -x <xdmaN>        XDMA device prefix (default: xdma0)\n"
		"  -H <n>            number of H2C channels to use (default: 1)\n"
		"  -C <n>            number of C2H channels to use (default: 1)\n"
		"  -p                per-channel smoke verify (default: on)\n"
		"  -O <n>            outstanding read verify depth (default: 0 = off)\n"
		"  -s <start>        start address offset bytes (default: 0)\n"
		"  -l <len>          total length bytes to cover (default: 8G)\n"
		"  -b <block>        block size bytes per transfer (default: 1048576)\n"
		"  -S <seed>         seed (default: 0x12345678)\n"
		"  -q                quiet (less progress output)\n"
		"\n"
		"Examples:\n"
		"  sudo %s -l 8G -b 1M -H 4 -C 4\n"
		"  sudo %s -s 0 -l 256M -b 4M -H 4 -C 4 -S 0xdeadbeef\n"
		"  sudo %s -H 4 -C 4 -b 4M -O 16 -l 64M   # outstanding(16) read verify\n"
		"\n"
		"Notes:\n"
		"  - The test assumes a memory-mapped address space is implemented in the FPGA.\n"
		"  - Offsets and sizes must be multiples of 4.\n"
		"  - Smoke verify touches one block per channel pair at distinct offsets.\n",
		prog, prog, prog, prog);
}

static bool parse_u64(const char *s, uint64_t *out)
{
	if (!s || !*s)
		return false;

	/* support suffixes K/M/G (powers of 1024) */
	size_t n = strlen(s);
	uint64_t mul = 1;
	char last = s[n - 1];
	char *end = NULL;
	errno = 0;

	if (last == 'K' || last == 'k') {
		mul = 1024ull;
	} else if (last == 'M' || last == 'm') {
		mul = 1024ull * 1024ull;
	} else if (last == 'G' || last == 'g') {
		mul = 1024ull * 1024ull * 1024ull;
	}

	char tmp[64];
	const char *num = s;
	if (mul != 1) {
		if (n >= sizeof(tmp))
			return false;
		memcpy(tmp, s, n - 1);
		tmp[n - 1] = '\0';
		num = tmp;
	}

	unsigned long long v = strtoull(num, &end, 0);
	if (errno || end == num || (end && *end != '\0'))
		return false;
	*out = (uint64_t)v * mul;
	return true;
}

static uint32_t pat_word(uint32_t seed, uint64_t byte_off, uint32_t word_idx)
{
	/* incorporate absolute address to avoid false positives with aliasing */
	uint32_t a = (uint32_t)((byte_off >> 2) & 0xffffffffu);
	uint32_t v = seed ^ a ^ (word_idx * 0x9E3779B1u);
	v ^= (v << 13);
	v ^= (v >> 17);
	v ^= (v << 5);
	return v;
}

static int open_chan(const char *xdev, const char *dir, unsigned int ch)
{
	char path[128];
	snprintf(path, sizeof(path), "/dev/%s_%s_%u", xdev, dir, ch);
	int fd = open(path, O_RDWR | O_SYNC);
	if (fd < 0)
		fprintf(stderr, "Error: open('%s') failed: %s\n", path, strerror(errno));
	return fd;
}

static int do_pwrite_all(int fd, const void *buf, size_t len, uint64_t off)
{
	const uint8_t *p = (const uint8_t *)buf;
	size_t left = len;
	while (left) {
		ssize_t w = pwrite(fd, p, left, (off_t)off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (w == 0)
			return -EIO;
		left -= (size_t)w;
		p += (size_t)w;
		off += (uint64_t)w;
	}
	return 0;
}

static int do_pread_all(int fd, void *buf, size_t len, uint64_t off)
{
	uint8_t *p = (uint8_t *)buf;
	size_t left = len;
	while (left) {
		ssize_t r = pread(fd, p, left, (off_t)off);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (r == 0)
			return -EIO;
		left -= (size_t)r;
		p += (size_t)r;
		off += (uint64_t)r;
	}
	return 0;
}

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static int verify_one_block(int h2c_fd, int c2h_fd, void *wbuf, void *rbuf,
			    size_t sz, uint64_t off, uint32_t seed)
{
	uint32_t *w = (uint32_t *)wbuf;
	uint32_t words = (uint32_t)(sz / 4);
	for (uint32_t i = 0; i < words; i++)
		w[i] = pat_word(seed, off, i);

	int rv = do_pwrite_all(h2c_fd, wbuf, sz, off);
	if (rv)
		return rv;
	rv = do_pread_all(c2h_fd, rbuf, sz, off);
	if (rv)
		return rv;
	if (memcmp(wbuf, rbuf, sz) != 0)
		return -EIO;
	return 0;
}

struct rd_job {
	const char *xdev;
	unsigned int cch;
	uint64_t off;
	size_t sz;
	uint32_t seed;
	int result; /* 0 pass, else errno-style negative or -EIO mismatch */
};

static void *rd_worker(void *arg)
{
	struct rd_job *j = (struct rd_job *)arg;
	int fd = open_chan(j->xdev, "c2h", j->cch);
	if (fd < 0) {
		j->result = -errno;
		return NULL;
	}

	void *rbuf = NULL;
	if (posix_memalign(&rbuf, 4096, j->sz)) {
		j->result = -ENOMEM;
		close(fd);
		return NULL;
	}

	int rv = do_pread_all(fd, rbuf, j->sz, j->off);
	if (rv) {
		j->result = rv;
		free(rbuf);
		close(fd);
		return NULL;
	}

	/* verify against expected pattern */
	uint32_t *r = (uint32_t *)rbuf;
	uint32_t words = (uint32_t)(j->sz / 4);
	for (uint32_t i = 0; i < words; i++) {
		uint32_t exp = pat_word(j->seed, j->off, i);
		if (r[i] != exp) {
			j->result = -EIO;
			free(rbuf);
			close(fd);
			return NULL;
		}
	}

	j->result = 0;
	free(rbuf);
	close(fd);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *xdev = "xdma0";
	unsigned int h2c_n = 1;
	unsigned int c2h_n = 1;
	uint64_t start = 0;
	uint64_t len = 8ull * 1024ull * 1024ull * 1024ull; /* 8G */
	uint64_t block = 1024ull * 1024ull;               /* 1M */
	uint64_t seed_u64 = 0x12345678u;
	bool quiet = false;
	bool smoke = true;
	uint64_t out_depth = 0;

	int opt;
	while ((opt = getopt(argc, argv, "x:H:C:ps:l:b:S:O:qh")) != -1) {
		switch (opt) {
		case 'x':
			xdev = optarg;
			break;
		case 'H': {
			uint64_t v;
			if (!parse_u64(optarg, &v) || v == 0 || v > 32) {
				fprintf(stderr, "Error: invalid -H %s\n", optarg);
				return 2;
			}
			h2c_n = (unsigned int)v;
			break;
		}
		case 'C': {
			uint64_t v;
			if (!parse_u64(optarg, &v) || v == 0 || v > 32) {
				fprintf(stderr, "Error: invalid -C %s\n", optarg);
				return 2;
			}
			c2h_n = (unsigned int)v;
			break;
		}
		case 's':
			if (!parse_u64(optarg, &start)) {
				fprintf(stderr, "Error: invalid -s %s\n", optarg);
				return 2;
			}
			break;
		case 'l':
			if (!parse_u64(optarg, &len) || len == 0) {
				fprintf(stderr, "Error: invalid -l %s\n", optarg);
				return 2;
			}
			break;
		case 'b':
			if (!parse_u64(optarg, &block) || block == 0) {
				fprintf(stderr, "Error: invalid -b %s\n", optarg);
				return 2;
			}
			break;
		case 'S':
			if (!parse_u64(optarg, &seed_u64)) {
				fprintf(stderr, "Error: invalid -S %s\n", optarg);
				return 2;
			}
			break;
		case 'q':
			quiet = true;
			break;
		case 'p':
			smoke = true;
			break;
		case 'O':
			if (!parse_u64(optarg, &out_depth) || out_depth == 0 || out_depth > 1024) {
				fprintf(stderr, "Error: invalid -O %s\n", optarg);
				return 2;
			}
			break;
		case 'h':
		default:
			usage(argv[0]);
			return (opt == 'h') ? 0 : 2;
		}
	}

	if ((start % 4) || (len % 4) || (block % 4)) {
		fprintf(stderr, "Error: start/len/block must be multiples of 4\n");
		return 2;
	}
	if (block > len)
		block = len;

	uint32_t seed = (uint32_t)(seed_u64 & 0xffffffffu);

	/* open channels */
	int *h2c_fds = calloc(h2c_n, sizeof(int));
	int *c2h_fds = calloc(c2h_n, sizeof(int));
	if (!h2c_fds || !c2h_fds) {
		fprintf(stderr, "Error: OOM allocating fd arrays\n");
		return 2;
	}
	for (unsigned int i = 0; i < h2c_n; i++) {
		h2c_fds[i] = open_chan(xdev, "h2c", i);
		if (h2c_fds[i] < 0)
			return 2;
	}
	for (unsigned int i = 0; i < c2h_n; i++) {
		c2h_fds[i] = open_chan(xdev, "c2h", i);
		if (c2h_fds[i] < 0)
			return 2;
	}

	/* allocate buffers */
	void *wbuf = NULL;
	void *rbuf = NULL;
	size_t buf_sz = (size_t)block;
	if (posix_memalign(&wbuf, 4096, buf_sz) || posix_memalign(&rbuf, 4096, buf_sz)) {
		fprintf(stderr, "Error: posix_memalign failed\n");
		return 2;
	}

	/*
	 * Outstanding verify: pre-fill a small window, then issue N concurrent reads.
	 * This is meant for ILA observation (RID/ARID usage). ID assignment is HW-defined;
	 * software here only increases concurrency.
	 */
	if (out_depth) {
		uint64_t need = out_depth * block;
		if (len < need) {
			fprintf(stderr,
				"Error: len (%" PRIu64 ") too small for -O %" PRIu64 ".\n"
				"       Need at least O*block = %" PRIu64 " (use larger -l or smaller -b)\n",
				len, out_depth, need);
			return 2;
		}
		if (!quiet)
			fprintf(stderr, "Info: outstanding read verify: O=%" PRIu64 " block=%" PRIu64 "\n",
				out_depth, block);

		/* write pattern into [start, start + O*block) */
		for (uint64_t i = 0; i < out_depth; i++) {
			uint64_t off = start + i * block;
			unsigned int hch = (unsigned int)(i % h2c_n);
			size_t this_sz = (size_t)block;
			uint32_t *w = (uint32_t *)wbuf;
			uint32_t words = (uint32_t)(this_sz / 4);
			for (uint32_t wi = 0; wi < words; wi++)
				w[wi] = pat_word(seed, off, wi);
			int rv = do_pwrite_all(h2c_fds[hch], wbuf, this_sz, off);
			if (rv) {
				fprintf(stderr, "FAIL: prefill write h2c=%u off=0x%016" PRIx64 " sz=%zu: %s\n",
					hch, off, this_sz, strerror(-rv));
				return 1;
			}
		}

		/* launch O concurrent reads */
		pthread_t *ths = calloc((size_t)out_depth, sizeof(*ths));
		struct rd_job *jobs = calloc((size_t)out_depth, sizeof(*jobs));
		if (!ths || !jobs) {
			fprintf(stderr, "Error: OOM allocating threads/jobs\n");
			return 2;
		}
		for (uint64_t i = 0; i < out_depth; i++) {
			jobs[i].xdev = xdev;
			jobs[i].cch = (unsigned int)(i % c2h_n);
			jobs[i].off = start + i * block;
			jobs[i].sz = (size_t)block;
			jobs[i].seed = seed;
			jobs[i].result = -1;
			int rc = pthread_create(&ths[i], NULL, rd_worker, &jobs[i]);
			if (rc != 0) {
				fprintf(stderr, "Error: pthread_create failed: %s\n", strerror(rc));
				return 2;
			}
		}
		int fail = 0;
		for (uint64_t i = 0; i < out_depth; i++) {
			(void)pthread_join(ths[i], NULL);
			if (jobs[i].result != 0) {
				fprintf(stderr,
					"FAIL: outstanding read job i=%" PRIu64 " c2h=%u off=0x%016" PRIx64
					": %s\n",
					i, jobs[i].cch, jobs[i].off,
					(jobs[i].result == -EIO) ? "data mismatch" : strerror(-jobs[i].result));
				fail = 1;
			}
		}
		free(ths);
		free(jobs);
		if (fail)
			return 1;

		if (!quiet)
			fprintf(stderr, "Info: outstanding read verify pass (O=%" PRIu64 ")\n", out_depth);
	}

	/* per-channel smoke verify: one block per channel pair */
	if (smoke) {
		uint64_t needed = (uint64_t)((h2c_n > c2h_n) ? h2c_n : c2h_n) * block;
		if (len < needed) {
			fprintf(stderr,
				"Error: len (%" PRIu64 ") too small for per-channel smoke verify.\n"
				"       Need at least max(H2C,C2H)*block = %" PRIu64
				" (use larger -l or smaller -b)\n",
				len, needed);
			return 2;
		}
		if (!quiet)
			fprintf(stderr, "Info: per-channel smoke verify: H2C=%u C2H=%u block=%" PRIu64 "\n",
				h2c_n, c2h_n, block);

		unsigned int pairs = (h2c_n > c2h_n) ? h2c_n : c2h_n;
		for (unsigned int i = 0; i < pairs; i++) {
			unsigned int hch = i % h2c_n;
			unsigned int cch = i % c2h_n;
			uint64_t off = start + (uint64_t)i * block;
			size_t this_sz = (size_t)block;

			int rv = verify_one_block(h2c_fds[hch], c2h_fds[cch],
						  wbuf, rbuf, this_sz, off, seed);
			if (rv) {
				fprintf(stderr,
					"FAIL: smoke verify h2c=%u c2h=%u off=0x%016" PRIx64
					" sz=%zu: %s\n",
					hch, cch, off, this_sz,
					(rv == -EIO) ? "data mismatch" : strerror(-rv));
				return 1;
			}
			if (!quiet)
				fprintf(stderr, "Info: smoke pass h2c=%u c2h=%u off=0x%016" PRIx64 "\n",
					hch, cch, off);
		}
	}

	uint64_t t0 = now_ms();
	uint64_t last_print = t0;
	uint64_t done = 0;

	for (uint64_t off = start; off < start + len; off += block) {
		uint64_t remain = (start + len) - off;
		size_t this_sz = (size_t)((remain < block) ? remain : block);

		unsigned int hch = (unsigned int)((off / block) % h2c_n);
		unsigned int cch = (unsigned int)((off / block) % c2h_n);

		int rv = verify_one_block(h2c_fds[hch], c2h_fds[cch],
					  wbuf, rbuf, this_sz, off, seed);
		if (rv) {
			fprintf(stderr,
				"FAIL: verify h2c=%u c2h=%u off=0x%016" PRIx64 " sz=%zu: %s\n",
				hch, cch, off, this_sz,
				(rv == -EIO) ? "data mismatch" : strerror(-rv));
			return 1;
		}

		done += (uint64_t)this_sz;
		uint64_t now = now_ms();
		if (!quiet && (now - last_print) >= 1000) {
			double sec = (now - t0) / 1000.0;
			double gb = done / (1024.0 * 1024.0 * 1024.0);
			double rate = sec > 0 ? (gb / sec) : 0.0;
			double pct = (double)done * 100.0 / (double)len;
			fprintf(stderr, "Progress: %6.2f%%  %.3f GiB / %.3f GiB  %.3f GiB/s\r",
				pct, gb, len / (1024.0 * 1024.0 * 1024.0), rate);
			fflush(stderr);
			last_print = now;
		}
	}

	if (!quiet)
		fprintf(stderr, "\n");
	printf("PASS: %s covered [0x%016" PRIx64 " .. 0x%016" PRIx64 "] bytes=%" PRIu64
	       " block=%" PRIu64 " H2C=%u C2H=%u seed=0x%08" PRIx32 "\n",
	       xdev, start, start + len - 1, len, block, h2c_n, c2h_n, seed);
	return 0;
}

