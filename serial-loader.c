#include <bfd.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <memory.h>

#define GET_CMD 0x00
#define GET_INFO_CMD 0x01
#define GET_ID_CMD 0x02
#define READ_MEMORY_CMD 0x11
#define GO_CMD 0x21
#define WRITE_MEMORY_CMD 0x31
#define ERASE_MEMORY_CMD 0x43
#define ERASE_MEMORY_EXT_CMD 0x44
#define WRITE_PROTECT_CMD 0x63
#define WRITE_UNPROTECT_CMD 0x73
#define READOUT_PROTECT 0x82
#define READOUT_UNPROTECT 0x92

#define ACK 0x79
#define NACK 0x1F
#define START 0x7F

uint8_t checksum = 0;

uint16_t swap16(uint16_t src)
{
	uint16_t val = (src >> 8) | ((src & 0xff)<<8);
	return val;
}

uint32_t swap32(uint32_t src)
{
	uint32_t val = (src >> 24) | 
		((src & 0xff0000)>>8) |
		((src & 0xff00)<<8) |
		((src & 0xff)<<24);
	return val;
}

int wait_ack(int fd)
{
	uint8_t ret = 0;
	checksum = 0;
	int bytes = read(fd, &ret, 1);
	if (bytes != 1) {
		printf("Bad read\n");
		return -1;
	}
	if (ret != ACK) {
		printf("Bad ack %d %d\n", bytes, ret);
		return -1;
	} else {
		return 0;
	}
}

int get_N_bytes(int fd, uint8_t *b, int N)
{
	int M = 0;
	while (M < N) {
		int rc = read(fd, b + M, N - M);
		if (rc == -1)
			return -1;
		M += rc;
	}
	return 0;
}

int write_byte(int fd, uint8_t b)
{
	if (write(fd, &b, 1) != 1)
		return -1;
	checksum ^= b;
	return 0;
}

int write_bytes(int fd, uint8_t *b, int N)
{
	if (write(fd, b, N) != N)
		return -1;
	int i;
	for (i = 0; i < N; i++) {
		checksum ^= b[i];
	}
	return 0;
}

int write_addr(int fd, uint32_t addr)
{
	uint32_t addr_swap = swap32(addr);
	if (write_bytes(fd, (uint8_t *)&addr_swap, 4) == -1)
		return -1;
	return 0;
}

int done(int fd)
{
	if (write(fd, &checksum, 1) != 1)
		return -1;
	return wait_ack(fd);
}

int init_uart(int fd)
{
	uint8_t s[1] = {START}; 

	struct termios tios;

	if(tcgetattr(fd, &tios) == -1) {
		return -1;
	}
	
	cfmakeraw(&tios);
	cfsetspeed(&tios, 115200);
	tios.c_cflag |= PARENB;

	if (tcsetattr(fd, TCSANOW, &tios) == -1) {
		return -1;
	}
	if (write(fd, s, 1) != 1) {
		printf("bad write");
		return -1;
	}
	return wait_ack(fd);
}

int send_command(int fd, uint8_t code)
{
	if (write_byte(fd, code) == -1)
		return -1;
	if (write_byte(fd, ~code) == -1)
		return -1;
	return wait_ack(fd);
}

int write_unprotect(int fd)
{
	if (send_command(fd, WRITE_UNPROTECT_CMD) == -1)
		return -1;
	return 0;
}

int get_id(int fd, uint8_t *ID)
{
	if (send_command(fd, GET_ID_CMD) == -1)
		return -1;
	uint8_t N;
	if (read(fd, &N, 1) != 1) {
		return -1;
	}
	if (get_N_bytes(fd, ID, N + 1) == -1)
		return -1;
	return wait_ack(fd);
}

int erase_pages(int fd, uint16_t *pages, int n_pages)
{
	if (send_command(fd, ERASE_MEMORY_EXT_CMD) == -1)
		return -1;
	uint16_t w = swap16(n_pages - 1);
	if (write_bytes(fd, (uint8_t *)&w, 2) == -1)
		return -1;
	int i;
	for (i = 0; i < n_pages; i++) {
		uint16_t w = swap16(pages[i]);
		if (write_bytes(fd, (uint8_t *)&w, 2) == -1)
			return -1;
	}
	return done(fd);
}

int go(int fd, uint32_t addr)
{
	if (send_command(fd, GO_CMD) == -1)
		return -1;
	if (write_addr(fd, addr) == -1)
		return -1;
	return done(fd);	
}

int write_memory(int fd, uint32_t addr, uint8_t *data, int len)
{
	if (send_command(fd, WRITE_MEMORY_CMD) == -1) {
		return -1;
	}
	if (write_addr(fd, addr) == -1) {
		return -1;
	}

	if (done(fd) == -1) {
		return -1;
	}
	if (write_byte(fd, len - 1) == -1) {
		return -1;
	}
	if (write_bytes(fd, data, len) == -1) {
		return -1;
	}
	if (done(fd) == - 1) {
		return -1;
	}
	return 0;
}

int read_memory(int fd, uint32_t addr, uint8_t *data, int len)
{
	if (send_command(fd, READ_MEMORY_CMD) == -1)
		return -1;
	if (write_addr(fd, addr) == -1)
		return -1;
	if (done(fd) == -1)
		return -1;
	if (write_byte(fd, len - 1) == -1)
		return -1;
	checksum = ~checksum;
	if (done(fd) == -1) {
		return -1;
	}
	if (get_N_bytes(fd, data, len) == -1)
		return -1;
	return 0;
}

int read_memory_ext(int fd, uint32_t addr, uint8_t *data, int len)
{
	int N = 0;
	while (N < len) {
		int M = len - N;
		if (M > 256)
			M = 256;
		if (read_memory(fd, addr + N, data + N, M) == -1)
			return -1;
		N += M;
	}
	return 0;
}

int write_memory_ext(int fd, uint32_t addr, uint8_t *data, int len)
{
	int N = 0;
	while (N < len) {
		int M = len - N;
		if (M > 256)
			M = 256;
		if (write_memory(fd, addr + N, data + N, M) == -1)
			return -1;
		N += M;
	}
	return 0;
}

int main(int argc, char **argv) 
{
	if (argc < 3) {
		fprintf(stderr, "Insufficient arguments\n");	
		exit(-1);
	}

	int fd = open(argv[2], O_RDWR | O_NOCTTY | O_SYNC);
	if (fd == -1) {
		fprintf(stderr, "Couldn't open serial port: %s\n", strerror(errno));
		exit(-1);
	}
	printf("Initializing UART\n");
	if (init_uart(fd) == -1) {
		fprintf(stderr, "Couldn't initialize serial port\n");
		exit(-1);
	}

	uint8_t ID[257];
	if (get_id(fd, ID) == -1) {
		fprintf(stderr, "Couldn't get device ID\n");
		exit(-1);
	}
	printf("Device ID: %x %x\n", ID[0], ID[1]);

	bfd_init();
	bfd *b = bfd_openr(argv[1], NULL);

	if (!b) {
		fprintf(stderr, "Couldn't open binary\n");
		exit(-1);
	}	

	bfd_check_format(b, bfd_object);

	printf("Format %s\n", b->xvec->name);

	static int page_mask[512];
	asection *s;
	
	for (s = b->sections; s; s = s->next) {
		if (bfd_get_section_flags (b, s) & (SEC_LOAD)) {
			int sz = bfd_section_size (b, s);  
			uint32_t lma = bfd_section_lma (b, s);
			uint32_t lma_end = lma + sz;
			uint32_t end_page = (lma_end - 0x8000000)>>(11);
			
			while (1) {
				int32_t page = (lma - 0x8000000)>>(11);
				if (page > end_page)
					break;
				if (page > 511)
					break;
				if (page >= 0)
					page_mask[page] = 1;
				lma += 2048;
			}
		}
	}
	uint16_t pages[512];
	int n_pages = 0;
	int i;
	for (i = 0; i < 512; i++) {
		if (page_mask[i]) {
			pages[n_pages++] = i;
		}
	}
	printf("Erasing %d pages of flash memory...", n_pages);
	fflush(stdout);
	if (erase_pages(fd, pages, n_pages) == -1) {
		fprintf(stderr, "Failed to erase flash memory\n");
		printf("ERROR\n");
		exit(-1);
	}
	printf("DONE\n");

	for (s = b->sections; s; s = s->next) {
		if (bfd_get_section_flags (b, s) & (SEC_LOAD)) {
			int sz = bfd_section_size (b, s);
			if (!sz) continue; 
			uint32_t lma = (uint32_t) bfd_section_lma (b, s);
			uint8_t *contents = (uint8_t *)malloc(sz);
			uint8_t *contents2 = (uint8_t *)malloc(sz);
			bfd_boolean ret = bfd_get_section_contents(b, s, contents, 0, sz);
			if (ret != FALSE) {
				printf("writing section %s: lma = 0x%08x (vma = 0x%08x)  size = 0x%08x...",
					bfd_section_name (b, s),
					(unsigned int) lma,
					(unsigned int) bfd_section_vma (b, s),
					(unsigned int) sz);
				fflush(stdout);
				int rc = write_memory_ext(fd, lma, contents, sz);
				if (rc == -1) {
					fprintf(stderr, "Failed to write section\n");
					printf("ERROR\n");
					exit(-1);
				}
				rc = read_memory_ext(fd, lma, contents2, sz);
				if (rc == -1) {
					fprintf(stderr, "Failed to read back section\n");
					printf("ERROR\n");
					exit(-1);		
				}
				int errors = 0;
				for (int i = 0; i < sz; i++) {
					if (contents[i] != contents2[i])
						errors++;
				}
				if (memcmp(contents, contents2, sz)) {
					fprintf(stderr, "Read data does not match %d\n", errors);
					printf("ERROR\n");
					exit(-1);
				}
				printf("DONE\n");
			}
		}
	}
	bfd_close(b);
	return 0;
}
