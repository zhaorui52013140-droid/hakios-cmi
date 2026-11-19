/*
 * thyme_fts_stubs.c — link stubs for the FTS touch port's diagnostics.
 *
 * The published thyme tree registers a dozen bsd/dev/fts_port/*.c files in
 * bsd/conf/files but does not ship them (nor compat.h / ftsSoftware.h). They are
 * only called from thyme_goodix.c's diagnostic bringup_test(), so the module is
 * dropped from the build and these no-op definitions keep the link whole.
 *
 * cmi's touch goes through thyme_st_touch.c (ST/FocalTech FTS521 over bit-banged
 * I2C), which does not use any of these.
 */
#include <sys/types.h>

int
fts_port_warm_frame(void)
{
	return 0;
}

int
fts_port_frame_sum(void)
{
	return 0;
}

int
fts_port_selftest(void)
{
	return 0;
}

int
fts_touch_init(void)
{
	return 0;
}

uint32_t
fts_touch_poll_count(void)
{
	return 0;
}
