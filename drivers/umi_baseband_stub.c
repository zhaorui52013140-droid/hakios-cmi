/*
 * (cmi) The CommCenter bridge, for umi_modem=1: linkable no-ops.
 *
 * umi-fixes keeps the bridge in drivers/umi/hakios.umi_baseband.cpp (an IOKit C++ driver, ~89 KB)
 * and the X55 transport calls it only when the bridge mode is on (umi_modem=2), where iOS's
 * CommCenter owns QMI and the kernel is a mere transport.  This kernel runs umi_modem=1 — the
 * kernel speaks QMI itself — so the kernel's half of the bridge is never reached; the symbols
 * still have to exist for md_thread() and md_ipcr_receive() to link.
 *
 * Porting the .cpp is what umi_modem=2 needs later; it is not part of the stage-1 bring-up.
 */
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/umi_baseband.h>

void
umi_bb_publish(void)
{
}

void
umi_bb_modem_ready(void)
{
}

void
umi_bb_modem_lost(void)
{
}

void
umi_bb_new_server(uint32_t service, uint32_t version_instance, uint32_t node, uint32_t port)
{
	(void)service; (void)version_instance; (void)node; (void)port;
}

void
umi_bb_del_server(uint32_t service, uint32_t node, uint32_t port)
{
	(void)service; (void)node; (void)port;
}

int
umi_bb_qrtr(uint32_t dst_port, uint32_t src_node, uint32_t src_port, const void *body, uint32_t size)
{
	(void)dst_port; (void)src_node; (void)src_port; (void)body; (void)size;
	return 0;                       /* nothing reaches the modem through a bridge that is off */
}

unsigned
umi_bb_poll(umi_bb_send_t send, umi_bb_release_t release)
{
	(void)send; (void)release;
	return 0;
}

void
umi_bb_link(uint32_t dl_max_size)
{
	(void)dl_max_size;
}

int
umi_bb_data_input(uint32_t mux, const uint8_t *ip, uint32_t length)
{
	(void)mux; (void)ip; (void)length;
	return 0;
}

uint32_t
umi_bb_data_take(uint8_t *mux, uint8_t *ip, uint32_t room)
{
	(void)mux; (void)ip; (void)room;
	return 0;
}

void
umi_bb_report(unsigned second)
{
	(void)second;
}
