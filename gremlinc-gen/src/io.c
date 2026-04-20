#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "io.h"

char *
slurp(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long sz = ftell(f);
	if (sz < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	char *buf = malloc((size_t)sz + 1);
	if (buf == NULL) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (n != (size_t)sz) { free(buf); return NULL; }
	buf[n] = '\0';
	if (out_len != NULL) *out_len = n;
	return buf;
}

char *
read_under_root(const char *imports_root, const char *rel, size_t *out_len)
{
	size_t rl = strlen(imports_root);
	size_t fl = strlen(rel);
	char *full = malloc(rl + 1 + fl + 1);
	if (full == NULL) return NULL;
	memcpy(full, imports_root, rl);
	full[rl] = '/';
	memcpy(full + rl + 1, rel, fl + 1);
	char *buf = slurp(full, out_len);
	free(full);
	return buf;
}

char *
derive_output_path(const char *out_dir, const char *src_path)
{
	size_t src_len = strlen(src_path);
	/* Strip trailing ".proto" if present; whatever remains keeps its
	 * subdirectory structure so the output mirrors the input tree. */
	if (src_len > 6 && memcmp(src_path + src_len - 6, ".proto", 6) == 0) {
		src_len -= 6;
	}
	size_t od_len = strlen(out_dir);
	/* `.pb.h` = 5 chars + NUL. */
	char *out = malloc(od_len + 1 + src_len + 5 + 1);
	if (out == NULL) return NULL;
	memcpy(out, out_dir, od_len);
	out[od_len] = '/';
	memcpy(out + od_len + 1, src_path, src_len);
	memcpy(out + od_len + 1 + src_len, ".pb.h", 6);
	return out;
}

int
ensure_parent_dir(const char *file_path)
{
	/* Walk each `/` in file_path and mkdir the prefix. We skip
	 * repeated slashes and tolerate EEXIST. No trailing dir is
	 * created — the last segment is the file itself. */
	size_t len = strlen(file_path);
	char *tmp = malloc(len + 1);
	if (tmp == NULL) return -1;
	memcpy(tmp, file_path, len + 1);

	for (size_t i = 1; i < len; i++) {
		if (tmp[i] != '/') continue;
		tmp[i] = '\0';
		if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
			free(tmp);
			return -1;
		}
		tmp[i] = '/';
	}
	free(tmp);
	return 0;
}
