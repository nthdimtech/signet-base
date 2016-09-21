#include <bfd.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <memory.h>
#include <sys/wait.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Insufficient arguments\n");
		exit(-1);
	}

	bfd_init();
	bfd *b = bfd_openr(argv[1], NULL);
	if (!b) {
		fprintf(stderr, "Couldn't open binary\n");
		exit(-1);
	}
	bfd_check_format(b, bfd_object);

	printf("Format %s\n", b->xvec->name);

	asection *s;

	char image[1000000];
	int image_size = 0;
		
	for (s = b->sections; s; s = s->next) {
		if (bfd_get_section_flags (b, s) & (SEC_LOAD)) {
			int sz = bfd_section_size (b, s);
			uint32_t lma = bfd_section_lma (b, s);
			uint8_t *contents = (uint8_t *)malloc(sz);
			bfd_boolean ret = bfd_get_section_contents(b, s, contents, 0, sz);
			if (ret != FALSE) {
				printf("Copying section %s: lma = 0x%08x (vma = 0x%08x)  size = 0x%08x\n",
					bfd_section_name (b, s),
					(unsigned int) lma,
					(unsigned int) bfd_section_vma (b, s),
					(unsigned int) sz);
				int off = lma - 0x8000000;
				memcpy(image + off, contents, sz);
				if (off + sz > image_size)
					image_size = off + sz;	
			}
			free(contents);

		}
	}
	bfd_close(b);

	image_size = 2048*((image_size + 2047)/2048);

	int fds[2];
	pipe(fds);
	pid_t child = fork();
	if (child) {
		int index = 0;
		while (index < image_size) {
			int rc = write(fds[1], image + index, image_size - index);
			if (rc == -1) {
				printf("ABORT\n");
				exit(-1);
			}
			index += rc;
		}
		close(fds[1]);
		close(fds[0]);
		int status;
		waitpid(child, &status, WNOHANG);
		printf("DONE\n");
	} else {
		dup2(fds[0], 0);
		close(fds[0]);
		close(fds[1]);

		char *argv[] = {"/usr/bin/dfu-util", "-a", "0","-v",  "-s", "0x8000000:leave", "-D", "-", NULL};
		char *envp[] = {NULL};
		int rc = execve("/usr/bin/dfu-util", argv, envp);
		if (rc == -1) {
			printf("%s\n", strerror(errno));
			exit(-1);
		}
	}
	return 0;
}
