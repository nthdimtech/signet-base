#include <bfd.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <memory.h>

#include <json-c/json.h>
#include "b64/cencode.h"

int main(int argc, char **argv)
{
	if (argc < 3) {
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

	FILE *json_out = fopen(argv[2], "wb");

	printf("Format %s\n", b->xvec->name);

	asection *s;

	json_object *doc = json_object_new_object();
	json_object *sections = json_object_new_object();

	json_object_object_add(doc, "sections", sections);

	char temp[1000000];

	for (s = b->sections; s; s = s->next) {
		if (bfd_get_section_flags (b, s) & (SEC_LOAD)) {
			int sz = bfd_section_size (b, s);
			uint32_t lma = bfd_section_lma (b, s);
			uint8_t *contents = (uint8_t *)malloc(sz);
			bfd_boolean ret = bfd_get_section_contents(b, s, contents, 0, sz);
			if (ret != FALSE) {
				printf("Encoding section %s: lma = 0x%08x (vma = 0x%08x)  size = 0x%08x\n",
					bfd_section_name (b, s),
					(unsigned int) lma,
					(unsigned int) bfd_section_vma (b, s),
					(unsigned int) sz);
				json_object *section = json_object_new_object();
				json_object_object_add(section, "lma",
						json_object_new_int(lma));
				json_object_object_add(section, "size",
						json_object_new_int(sz));

				base64_encodestate es;
				base64_init_encodestate(&es);
				char *out = temp;
				out += base64_encode_block(contents, sz, out, &es);
				out += base64_encode_blockend(out, &es);
				*out = 0;

				json_object_object_add(section, "contents",
						json_object_new_string(temp));

				json_object_object_add(sections, bfd_section_name (b, s), section);
			}
			free(contents);

		}
	}
	fprintf(json_out,"%s\n",json_object_to_json_string_ext(doc, JSON_C_TO_STRING_PRETTY));
	fclose(json_out);
	bfd_close(b);
	return 0;
}
