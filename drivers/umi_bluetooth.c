/* UMI QCA6390 Bluetooth UART, volatile firmware bootstrap and radio diagnostics.
 * Board wiring comes from UMI DT and Xiaomi GCC/GENI drivers; Hastings NVM
 * and unified HCI replies follow upstream btqca.c. */
#include <libkern/libkern.h>
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <sys/kauth.h>
#include <sys/kern_control.h>
#include <sys/kpi_mbuf.h>
#include <sys/termios.h>
#include <sys/ttycom.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/hakios.bluetooth.h>
extern kern_return_t thread_terminate(thread_t thread);
#include "umi_bt_firmware.inc"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern bool umi_wifi_is_board(void);
extern void loader_return(void);
extern int umi_rpmh_bluetooth_power(void);
extern int umi_bluetooth_publish(const uint8_t address[6],const uint8_t commands[64]);
extern void umi_bluetooth_set_ready(bool ready);
static uint32_t bt_read(vm_offset_t base, uint32_t offset)
{
    uint32_t value = *(volatile uint32_t *)(base + offset);
    __asm__ volatile("dmb oshld" ::: "memory");
    return value;
}
static void bt_write(vm_offset_t base, uint32_t offset, uint32_t value)
{
    __asm__ volatile("dmb oshst" ::: "memory");
    *(volatile uint32_t *)(base + offset) = value;
    __asm__ volatile("dsb osh" ::: "memory");
}
/* Internal loopback exercises FIFO framing without driving the controller. */
static void bt_loopback(vm_offset_t uart)
{
    if ((bt_read(uart,0x40) & 0x1001) || bt_read(uart,0x800) || bt_read(uart,0x804)) {
        printf("UMI BT: loopback skipped: sequencer/FIFO busy\n"); return;
    }
    static const uint32_t offsets[] = {0x48,0x4c,0x22c,0x258,0x25c,0x260,0x264,
        0x268,0x26c,0x270,0x280,0x284,0x288,0x28c,0x294,0x2a4,0x2a8,0x80c,0x810,0x814};
    uint32_t saved[sizeof(offsets)/sizeof(offsets[0])];
    for (unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++) saved[i]=bt_read(uart,offsets[i]);
    bt_write(uart,0x48,0x41); bt_write(uart,0x4c,0x41);
    bt_write(uart,0x22c,1); bt_write(uart,0x258,0);
    bt_write(uart,0x25c,2); bt_write(uart,0x280,0);
    bt_write(uart,0x260,0xf); bt_write(uart,0x264,0);
    bt_write(uart,0x284,0x4380e); bt_write(uart,0x288,0xc3e0e);
    bt_write(uart,0x268,8); bt_write(uart,0x28c,8); bt_write(uart,0x26c,0);
    bt_write(uart,0x294,160); bt_write(uart,0x2a4,0); bt_write(uart,0x2a8,0);
    bt_write(uart,0x80c,1); bt_write(uart,0x810,0); bt_write(uart,0x814,6);
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    bt_write(uart,0x630,(1u<<27)|0x80);
    static const uint8_t pattern[]={0x01,0x03,0x0c,0x00,0x55,0xaa,0x7e,0xff};
    bt_write(uart,0x270,sizeof(pattern)); bt_write(uart,0x600,1u<<27);
    for (unsigned i=0;i<sizeof(pattern);i++) bt_write(uart,0x700,pattern[i]);
    uint8_t received[sizeof(pattern)]; unsigned count=0, elapsed;
    for(elapsed=0;elapsed<20000 && count<sizeof(pattern);elapsed++) {
        uint32_t status=bt_read(uart,0x804), words=status&0x1ffffff;
        while(words--) {
            unsigned bytes=4;
            if (!words && (status&(1u<<31))) bytes=(status>>28)&7;
            uint32_t value=bt_read(uart,0x780);
            for(unsigned b=0;b<bytes;b++) {
                if(count<sizeof(received)) received[count++]=(value>>(8*b))&0xff;
            }
        }
        delay(1);
    }
    printf("UMI BT: loopback %s: %u/%u bytes in %u us, IRQ=%#x/%#x\n",
        count==sizeof(pattern) && !memcmp(pattern,received,sizeof(pattern)) ? "PASS":"FAIL",
        count,(unsigned)sizeof(pattern),elapsed,bt_read(uart,0x610),bt_read(uart,0x640));
    for (unsigned i=0;i<count;i++) printf("UMI BT: loopback byte%u=%#x expected=%#x\n",i,received[i],pattern[i]);
    bt_write(uart,0x604,4); bt_write(uart,0x634,4);
    unsigned waited;
    for(waited=0;waited<10000 && (bt_read(uart,0x40)&0x1001);waited++) delay(1);
    if(bt_read(uart,0x40)&0x1001) {
        bt_write(uart,0x604,2); bt_write(uart,0x634,2);
        for(waited=0;waited<10000 && (bt_read(uart,0x40)&0x1001);waited++) delay(1);
    }
    printf("UMI BT: loopback cleanup status=%#x\n",bt_read(uart,0x40));
    if (!(bt_read(uart,0x40)&0x1001)) {
        for(unsigned i=0;i<sizeof(saved)/sizeof(saved[0]);i++) bt_write(uart,offsets[i],saved[i]);
        bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    }
}
#define BT_EVENT_MAX 258u
static struct hakios_bt_h4 bt_rx_packet;
static bool bt_ibs_ack_pending;
static unsigned bt_ibs_wakes, bt_ibs_sleeps;
static uint8_t bt_nvm_work[8192];
static uint8_t bt_public_address[6];
static uint8_t bt_supported_commands[64];
static unsigned bt_unsolicited;
/* The UART engine samples at 16x with its own divider at 4 (GENI 0x48/0x4c =
 * 0x41): 300 MHz * 384/15625 = 7.3728 MHz, /4 = 1.8432 MHz, /16 = 115200.
 * A2DP needs 229-345 kbits, so the link is moved to 3.2 Mbaud after the
 * firmware download; the controller takes the rate from NVM tag 17 at Reset. */
#define BT_BAUD_SLOW 115200u
#define BT_BAUD_FAST 3200000u
static speed_t bt_link_speed=BT_BAUD_SLOW;
static bool bt_uart_clock(vm_offset_t gcc,uint32_t m,uint32_t n);
static int bt_baud_fast(vm_offset_t uart,vm_offset_t gcc);
static int bt_baud_upgrade(vm_offset_t uart,vm_offset_t gcc);
static void bt_baud_slow(vm_offset_t uart,vm_offset_t gcc);

#define BT_RX_RAW_SIZE 16384u
static uint8_t bt_rx_raw[BT_RX_RAW_SIZE];
static unsigned bt_rx_head, bt_rx_tail;
static bool bt_host_awake;
static int bt_rx_capture(vm_offset_t uart)
{
    uint32_t irq=bt_read(uart,0x640);
    if(irq) bt_write(uart,0x648,irq);
    for(unsigned budget=0;budget<64;budget++) {
        uint32_t status=bt_read(uart,0x804),words=status&0x1ffffff;
        if(!words) return 0;
        if(words>((bt_read(uart,0xe28)>>16)&0x3f)) return EIO;
        unsigned last=(status>>28)&7;
        unsigned bytes=words==1 && (status>>31) && last ? last : 4;
        if(bytes>4) return EIO;
        if(BT_RX_RAW_SIZE-(bt_rx_head-bt_rx_tail)<bytes) return ENOBUFS;
        uint32_t word=bt_read(uart,0x780);
        for(unsigned b=0;b<bytes;b++) bt_rx_raw[bt_rx_head++&(BT_RX_RAW_SIZE-1)]=(word>>(8*b))&0xff;
    }
    return 0;
}
static int bt_rx_frame(vm_offset_t uart,uint8_t *frame,unsigned capacity,unsigned *length,bool events_only)
{
    int error=bt_rx_capture(uart);
    if(error) return -error;
    while(bt_rx_head!=bt_rx_tail) {
        uint8_t byte=bt_rx_raw[bt_rx_tail++&(BT_RX_RAW_SIZE-1)];
        int result=hakios_bt_h4_push(&bt_rx_packet,byte);
        if(result<0) { if(bt_unsolicited++<8) printf("UMI BT: H4 framing error=%d byte=%#x\n",result,byte); memset(&bt_rx_packet,0,sizeof(bt_rx_packet)); return -EILSEQ; }
        if(result==2) {
            if(byte==0xfd) { bt_ibs_ack_pending=true; bt_ibs_wakes++; }
            if(byte==0xfe) bt_ibs_sleeps++;
            if(byte==0xfc) bt_host_awake=true;
        }
        if(result!=1 || (events_only && bt_rx_packet.bytes[0]!=4)) continue;
        if(bt_rx_packet.used>capacity) return -EMSGSIZE;
        *length=bt_rx_packet.used;
        memcpy(frame,bt_rx_packet.bytes,*length);
        return 1;
    }
    return 0;
}
static int bt_rx_event(vm_offset_t uart,uint8_t event[BT_EVENT_MAX],unsigned *length)
{
    return bt_rx_frame(uart,event,BT_EVENT_MAX,length,true);
}
static int bt_uart_tx(vm_offset_t uart, const uint8_t *packet, unsigned length)
{
    unsigned sent = 0, depth = (bt_read(uart, 0xe24) >> 16) & 0x3f;
    if (!depth || depth > 64 || !length || length > HAKIOS_BT_H4_MAX) return EINVAL;
    bt_write(uart, 0x618, 0xffffffff);
    bt_write(uart, 0x270, length);
    bt_write(uart, 0x600, 1u << 27);
    for (unsigned elapsed = 0; elapsed < length * 180 + 10000; elapsed++) {
        int rx_error=bt_rx_capture(uart);
        if(rx_error) return rx_error;
        unsigned used = bt_read(uart, 0x800) & 0x0fffffff;
        if (used > depth) return EIO;
        for (unsigned free = depth - used; free && sent < length; free--)
            bt_write(uart, 0x700, packet[sent++]);
        uint32_t irq = bt_read(uart, 0x610);
        if (sent == length && (irq & 1)) {
            bt_write(uart, 0x618, irq);
            return 0;
        }
        delay(1);
    }
    printf("UMI BT: TX timeout sent=%u/%u irq=%#x fifo=%#x\n",
        sent, length, bt_read(uart, 0x610), bt_read(uart, 0x800));
    bt_write(uart, 0x604, 4);
    return ETIMEDOUT;
}
static int bt_service_ibs(vm_offset_t uart)
{
    if (!bt_ibs_ack_pending) return 0;
    const uint8_t ack = 0xfc;
    int error = bt_uart_tx(uart, &ack, 1);
    if (!error) bt_ibs_ack_pending = false;
    return error;
}
static int bt_hci_command(vm_offset_t uart, uint16_t opcode, const uint8_t *params,
    unsigned param_length, bool wait, uint8_t event[BT_EVENT_MAX], unsigned *event_length)
{
    uint8_t packet[259];
    if (param_length > 255) return EINVAL;
    packet[0] = 1; packet[1] = opcode & 0xff; packet[2] = opcode >> 8; packet[3] = param_length;
    if (param_length) memcpy(packet + 4, params, param_length);
    *event_length = 0;
    int error = bt_uart_tx(uart, packet, param_length + 4);
    if (error || !wait) return error;
    for (unsigned elapsed = 0; elapsed < 100000; elapsed++) {
        int result = bt_rx_event(uart, event, event_length);
        if (result < 0) return -result;
        error = bt_service_ibs(uart);
        if (error) return error;
        if (result == 1) {
            int status = hakios_bt_cc_status(event, *event_length, opcode);
            if (status >= 0) {
                if (status) printf("UMI BT: HCI %#x status=%#x\n", opcode, status);
                return status ? EIO : 0;
            }
            if (opcode == 0xfc00 && hakios_bt_qca_tlv_ack(event, *event_length)) return 0;
            /* Inquiry and other asynchronous commands acknowledge with Command Status. */
            if (*event_length == 7 && event[1] == 0x0f &&
                event[5] == (uint8_t)opcode && event[6] == (uint8_t)(opcode >> 8))
                return event[3] ? EIO : 0;
            /* Bytes sent at a rate the controller does not run at come back as
             * event type 0xff storms; keep them out of the log. */
            if (event[1] != 0xff && bt_unsolicited++ < 8)
                printf("UMI BT: unsolicited event=%#x while waiting for %#x\n", event[1], opcode);
        }
        *event_length = 0;
        delay(10);
    }
    printf("UMI BT: HCI %#x timed out RX=%#x SIRQ=%#x\n", opcode,
        bt_read(uart, 0x804), bt_read(uart, 0x640));
    return ETIMEDOUT;
}
static int bt_qca_download(vm_offset_t uart, const uint8_t *tlv, size_t size, bool skip_events)
{
    if (!tlv || size < 4 || size > 512 * 1024 ||
        (tlv[1] | (uint32_t)tlv[2] << 8 | (uint32_t)tlv[3] << 16) != size - 4) return EINVAL;
    size_t offset = 0;
    unsigned segment = 0;
    while (offset < size) {
        unsigned part = (unsigned)(size - offset);
        if (part > 243) part = 243;
        uint8_t params[245], event[BT_EVENT_MAX]; unsigned length = 0;
        params[0] = 0x1e; params[1] = part;
        memcpy(params + 2, tlv + offset, part);
        offset += part; segment++;
        bool ack = !skip_events || offset == size;
        int error = bt_hci_command(uart, 0xfc00, params, part + 2, ack, event, &length);
        if (!error && ack && !hakios_bt_qca_tlv_ack(event, length)) error = EIO;
        if (error) {
            printf("UMI BT: TLV type=%u segment=%u/%u failed errno=%d\n",
                tlv[0], segment, (unsigned)((size + 242) / 243), error);
            return error;
        }
    }
    printf("UMI BT: TLV type=%u accepted: %u bytes, %u segments\n", tlv[0], (unsigned)size, segment);
    return 0;
}
static uint16_t bt_le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t bt_le32(const uint8_t *p) { return bt_le16(p) | (uint32_t)bt_le16(p + 2) << 16; }
static int bt_qca_firmware(vm_offset_t uart, vm_offset_t gcc, vm_offset_t gpio, bool fast)
{
    uint8_t event[BT_EVENT_MAX]; unsigned length = 0;
    const uint8_t version_request = 0x19;
    int error = bt_hci_command(uart, 0xfc00, &version_request, 1, true, event, &length);
    if (error) return error;
    if (length != 21 || event[7] != 0x19) {
        printf("UMI BT: unsupported QCA version response length=%u\n", length);
        return ENODEV;
    }
    uint32_t product = bt_le32(event + 9), soc = bt_le32(event + 17);
    uint16_t rom = bt_le16(event + 15);
    printf("UMI BT: QCA product=%#x ROM=%#x SoC=%#x patch=%#x\n",
        product, rom, soc, bt_le16(event + 13));
    if (product != 0x10 || rom != 0x200 || soc != 0x400a0200) return ENODEV;
    if (umi_bt_patch_size < 28 || umi_bt_nvm_size < 4) return ENOENT;
    if (bt_le16(umi_bt_patch + 16) != product || bt_le16(umi_bt_patch + 18) != rom)
        return EINVAL;
    unsigned mode = umi_bt_patch[14];
    if (mode != 0 && mode != 3) return EINVAL;
    if (umi_bt_nvm_size > sizeof(bt_nvm_work)) return E2BIG;
    memcpy(bt_nvm_work, umi_bt_nvm, umi_bt_nvm_size);
    /* Keep the NVM at 115200. Letting the controller take 0x11 there switches it
     * the moment the download is acknowledged (2026-11-19: the HCI_Reset that
     * followed never answered), which leaves no safe way back if the host's new
     * rate turns out wrong. The upgrade happens after the link is proven, with
     * the vendor command, so a failure changes nothing. */
    if (hakios_bt_qca6390_nvm_prepare(bt_nvm_work, umi_bt_nvm_size,
        HAKIOS_BT_QCA_BAUD_115200)) return EINVAL;
    printf("UMI BT: loading htbtfw20/htnv20 at 115200 baud (tag17 byte1=%#x)\n",
        HAKIOS_BT_QCA_BAUD_115200);
    error = bt_qca_download(uart, umi_bt_patch, umi_bt_patch_size, mode == 3);
    if (error) return error;
    delay(10000);
    error = bt_qca_download(uart, bt_nvm_work, umi_bt_nvm_size, false);
    if (error) return error;
    /* QCA6390's normal Linux sequence disables diagnostic logging before reset. */
    const uint8_t logging[] = {0x14, 0};
    error = bt_hci_command(uart, 0xfc17, logging, sizeof(logging), true, event, &length);
    if (error) return error;
    error = bt_hci_command(uart, 0x0c03, NULL, 0, true, event, &length);
    if (error) return error;
    error = bt_hci_command(uart, 0x1001, NULL, 0, true, event, &length);
    if (error) return error;
    if (length != 15 || bt_le16(event + 11) != 29 || bt_le16(event + 13) == 0x0d2b) return EIO;
    printf("UMI BT: firmware running after reset: HCI=%#x LMP=%#x subversion=%#x\n",
        event[7], event[10], bt_le16(event + 13));
    error = bt_hci_command(uart, 0x1002, NULL, 0, true, event, &length);
    if (error) return error;
    if (length != 71) return EILSEQ;
    memcpy(bt_supported_commands,event+7,sizeof(bt_supported_commands));
    error = bt_hci_command(uart, 0x1009, NULL, 0, true, event, &length);
    if (error) return error;
    if (length != 13) return EILSEQ;
    unsigned nonzero = 0;
    for (unsigned i=0;i<6;i++) { bt_public_address[i]=event[12-i]; nonzero |= bt_public_address[i]; }
    if (!nonzero || (bt_public_address[0]&1)) return EINVAL;
    if (fast) {
        int upgraded = bt_baud_upgrade(uart, gcc);
        if (upgraded < 0) return EAGAIN;
    }
    return 0;
}
static int bt_radio_scan(vm_offset_t uart)
{
    uint8_t event[BT_EVENT_MAX]; unsigned length = 0;
    const uint8_t mask[] = {0xff,0xff,0xfb,0xff,0x07,0xf8,0xbf,0x3d};
    const uint8_t le_mask[] = {2,0,0,0,0,0,0,0};
    const uint8_t params[] = {0,0x60,0,0x30,0,0,0};
    const uint8_t enable[] = {1,1}, disable[] = {0,0};
    int error = bt_hci_command(uart,0x0c01,mask,sizeof(mask),true,event,&length);
    if (!error) error = bt_hci_command(uart,0x2001,le_mask,sizeof(le_mask),true,event,&length);
    if (!error) error = bt_hci_command(uart,0x200b,params,sizeof(params),true,event,&length);
    if (!error) error = bt_hci_command(uart,0x200c,enable,sizeof(enable),true,event,&length);
    if (error) return error;
    unsigned reports = 0;
    uint64_t deadline;
    clock_interval_to_deadline(3000, NSEC_PER_MSEC, &deadline);
    while (mach_absolute_time() < deadline) {
        int result = bt_rx_event(uart,event,&length);
        if (result < 0) { error = -result; break; }
        error = bt_service_ibs(uart);
        if (error) break;
        if (result == 1 && event[1] == 0x3e && length >= 5 && event[3] == 2) reports += event[4];
        delay(100);
    }
    int stop = bt_hci_command(uart,0x200c,disable,sizeof(disable),true,event,&length);
    printf("UMI BT: LE passive scan reports=%u stop=%d\n",reports,stop);
    if (error || stop) return error ? error : stop;
    const uint8_t inquiry[] = {0x33,0x8b,0x9e,2,0};
    error = bt_hci_command(uart,0x0401,inquiry,sizeof(inquiry),true,event,&length);
    if (error) return error;
    unsigned classic = 0; bool complete = false;
    clock_interval_to_deadline(4000, NSEC_PER_MSEC, &deadline);
    while (mach_absolute_time() < deadline) {
        int result = bt_rx_event(uart,event,&length);
        if (result < 0) return -result;
        error = bt_service_ibs(uart);
        if (error) return error;
        if (result == 1) {
            if ((event[1] == 2 || event[1] == 0x22 || event[1] == 0x2f) && length >= 4) classic += event[3];
            if (event[1] == 1) { complete = true; break; }
        }
        delay(100);
    }
    printf("UMI BT: classic inquiry reports=%u complete=%u IBS wakes=%u sleeps=%u\n",classic,complete,bt_ibs_wakes,bt_ibs_sleeps);
    return complete ? 0 : ETIMEDOUT;
}
/* bluetoothd's built-in H4:POSIX backend uses a stream kernel-control socket.
 * The worker alone owns UART I/O. Socket callbacks only enqueue host bytes. */
#define BT_TX_QUEUE_SIZE 65536u
static LCK_GRP_DECLARE(bt_control_group,"umi-bluetooth");
static LCK_MTX_DECLARE(bt_control_lock,&bt_control_group);
static kern_ctl_ref bt_control;
static vm_offset_t bt_control_uart;
static uint32_t bt_control_unit,bt_control_generation;
static uint8_t bt_tx_queue[BT_TX_QUEUE_SIZE];

static unsigned bt_tx_head,bt_tx_tail;
static bool bt_control_fault;
static struct termios bt_control_termios;

static errno_t bt_control_connect(kern_ctl_ref ref,struct sockaddr_ctl *address,void **info)
{
    uid_t uid=kauth_cred_getuid(kauth_cred_get());
    if(uid!=0 && uid!=501) return EPERM;
    lck_mtx_lock(&bt_control_lock);
    int error=bt_control_fault ? EIO : (bt_control_unit ? EBUSY : 0);
    if(!error) {
        bt_control_unit=address->sc_unit;
        bt_control_generation++;
        bt_tx_head=bt_tx_tail=0;
        *info=NULL;
        printf("UMI BT: H4 client connected uid=%u unit=%u\n",uid,bt_control_unit);
    }
    lck_mtx_unlock(&bt_control_lock);
    return error;
}
static errno_t bt_control_disconnect(kern_ctl_ref ref,uint32_t unit,void *info)
{
    lck_mtx_lock(&bt_control_lock);
    if(bt_control_unit==unit) {
        bt_control_unit=0; bt_control_generation++; bt_tx_head=bt_tx_tail=0;
        printf("UMI BT: H4 client disconnected unit=%u\n",unit);
    }
    lck_mtx_unlock(&bt_control_lock);
    return 0;
}
static errno_t bt_control_send(kern_ctl_ref ref,uint32_t unit,void *info,mbuf_t message,int flags)
{
    size_t length=mbuf_pkthdr_len(message);
    int error=0;
    lck_mtx_lock(&bt_control_lock);
    if(bt_control_fault) error=EIO;
    else if(unit!=bt_control_unit) error=ENOTCONN;
    else if(length>BT_TX_QUEUE_SIZE-(bt_tx_head-bt_tx_tail)) error=ENOBUFS;
    for(size_t offset=0;!error && offset<length;) {
        uint8_t bytes[256]; size_t count=length-offset;
        if(count>sizeof(bytes)) count=sizeof(bytes);
        error=mbuf_copydata(message,offset,count,bytes);
        if(error) break;
        for(size_t i=0;i<count;i++) bt_tx_queue[bt_tx_head++&(BT_TX_QUEUE_SIZE-1)]=bytes[i];
        offset+=count;
    }
    lck_mtx_unlock(&bt_control_lock);
    mbuf_freem(message);
    return error;
}
static errno_t bt_control_getopt(kern_ctl_ref ref,uint32_t unit,void *info,int option,void *data,size_t *length)
{
    int modem=TIOCM_CTS|TIOCM_DSR|TIOCM_CAR;
    size_t wanted;
    if(option==TIOCGETA) wanted=sizeof(bt_control_termios);
    else if(option==TIOCMGET) wanted=sizeof(modem);
    else { printf("UMI BT: unknown H4 get option=%#x\n",option); return ENOPROTOOPT; }
    if(!data) { *length=wanted; return 0; }
    if(*length<wanted) return EMSGSIZE;
    lck_mtx_lock(&bt_control_lock);
    memcpy(data,option==TIOCGETA?(void *)&bt_control_termios:(void *)&modem,wanted);
    lck_mtx_unlock(&bt_control_lock);
    *length=wanted;
    return 0;
}
static errno_t bt_control_setopt(kern_ctl_ref ref,uint32_t unit,void *info,int option,void *data,size_t length)
{
    uint32_t opt=(uint32_t)option;
    if(opt==(uint32_t)TIOCSETA || opt==(uint32_t)TIOCSETAW || opt==(uint32_t)TIOCSETAF) {
        if(length!=sizeof(struct termios) || !data) return EINVAL;
        const struct termios *settings=data;
        /* The link rate is set by the firmware download, not by termios; accept
         * the speeds bluetoothd may request and always report the real one. */
        if((settings->c_ispeed && settings->c_ispeed!=BT_BAUD_SLOW && settings->c_ispeed!=BT_BAUD_FAST &&
            settings->c_ispeed!=3000000u && settings->c_ispeed!=921600u && settings->c_ispeed!=460800u) ||
            (settings->c_ospeed && settings->c_ospeed!=BT_BAUD_SLOW && settings->c_ospeed!=BT_BAUD_FAST &&
            settings->c_ospeed!=3000000u && settings->c_ospeed!=921600u && settings->c_ospeed!=460800u)) return EINVAL;
        struct termios applied=*settings;
        applied.c_ispeed=applied.c_ospeed=bt_link_speed;
        lck_mtx_lock(&bt_control_lock);
        bt_control_termios=applied;
        lck_mtx_unlock(&bt_control_lock);
        return 0;
    }
    if(option==TIOCSDTR || option==TIOCCDTR || option==TIOCMSET || option==TIOCMBIS ||
        option==TIOCMBIC || option==TIOCFLUSH) return 0;
    printf("UMI BT: unknown H4 set option=%#x length=%u\n",option,(unsigned)length);
    return ENOPROTOOPT;
}
static void bt_control_worker(void *unused,wait_result_t result)
{
    static struct hakios_bt_h4 tx;
    static uint8_t receive[HAKIOS_BT_H4_MAX];
    unsigned receive_length=0;
    uint32_t generation=0;
    unsigned commands=0;
    bool waking=false;
    uint64_t wake_deadline=0;
    for(;;) {
        lck_mtx_lock(&bt_control_lock);
        uint32_t unit=bt_control_unit;
        bool fault=bt_control_fault;
        if(generation!=bt_control_generation) {
            generation=bt_control_generation;
            memset(&tx,0,sizeof(tx)); receive_length=0; commands=0;
        }
        while(unit && !fault && !tx.complete && bt_tx_head!=bt_tx_tail) {
            uint8_t byte=bt_tx_queue[bt_tx_tail++&(BT_TX_QUEUE_SIZE-1)];
            int state=hakios_bt_h4_push(&tx,byte);
            if(state<0 || state==2 || (tx.used && tx.bytes[0]==4)) {
                fault=bt_control_fault=true;
                printf("UMI BT: rejected invalid host H4 stream\n");
            }
        }
        lck_mtx_unlock(&bt_control_lock);
        int error=0;
        if(unit && !fault && tx.complete) {
            if(!bt_host_awake) {
                if(!waking) {
                    const uint8_t wake=0xfd;
                    error=bt_uart_tx(bt_control_uart,&wake,1);
                    clock_interval_to_deadline(1000,NSEC_PER_MSEC,&wake_deadline);
                    waking=true;
                } else if(mach_absolute_time()>=wake_deadline) error=ETIMEDOUT;
            } else {
                waking=false;
                if(tx.bytes[0]==1 && commands++<128)
                    printf("UMI BT: iOS HCI opcode=%#x length=%u\n",bt_le16(tx.bytes+1),tx.used);
                if(tx.bytes[0]==1 && tx.used>=6 && bt_le16(tx.bytes+1)==0x200c)
                    printf("UMI BT: scan enable=%#x duplicates=%#x\n",tx.bytes[4],tx.bytes[5]);
                if(tx.bytes[0]==1 && tx.used==11 && bt_le16(tx.bytes+1)==0x200b)
                    printf("UMI BT: scan params type=%#x interval=%u window=%u own-address=%#x policy=%#x\n",tx.bytes[4],bt_le16(tx.bytes+5),bt_le16(tx.bytes+7),tx.bytes[9],tx.bytes[10]);
                error=bt_uart_tx(bt_control_uart,tx.bytes,tx.used);
                memset(&tx,0,sizeof(tx));
            }
        }
        if(!error && !receive_length) {
            int state=bt_rx_frame(bt_control_uart,receive,sizeof(receive),&receive_length,false);
            if(state<0) error=-state;
            if(state>0 && receive_length>=7 && receive[0]==4 && receive[1]==0x0e && receive[6])
                printf("UMI BT: HCI command rejected opcode=%#x status=%#x\n",bt_le16(receive+4),receive[6]);
        }
        if(!error) error=bt_service_ibs(bt_control_uart);
        if(error && !fault) {
            lck_mtx_lock(&bt_control_lock); bt_control_fault=true; lck_mtx_unlock(&bt_control_lock);
            umi_bluetooth_set_ready(false);
            printf("UMI BT: H4 transport fault errno=%d\n",error);
        }
        /* Never enqueue while holding bt_control_lock: socket callbacks acquire it. */
        if(receive_length) {
            int queued=unit?ctl_enqueuedata(bt_control,unit,receive,receive_length,0):ENOTCONN;
            if(queued!=ENOBUFS) receive_length=0;
        }
        delay(1000);
    }
}
static int bt_control_start(vm_offset_t uart)
{
    struct kern_ctl_reg registration={0};
    strlcpy(registration.ctl_name,"com.hakios.bluetooth",sizeof(registration.ctl_name));
    registration.ctl_flags=CTL_FLAG_REG_SOCK_STREAM;   /* umi_bluetool opens it as SOCK_STREAM */
    registration.ctl_sendsize=BT_TX_QUEUE_SIZE;
    registration.ctl_recvsize=BT_TX_QUEUE_SIZE;
    registration.ctl_connect=bt_control_connect;
    registration.ctl_disconnect=bt_control_disconnect;
    registration.ctl_send=bt_control_send;
    registration.ctl_getopt=bt_control_getopt;
    registration.ctl_setopt=bt_control_setopt;
    bt_control_termios.c_cflag=CS8|CREAD|CLOCAL;
    bt_control_termios.c_ispeed=bt_control_termios.c_ospeed=bt_link_speed;
    int error=ctl_register(&registration,&bt_control);
    if(error) return error;
    error=umi_bluetooth_publish(bt_public_address,bt_supported_commands);
    if(error) { ctl_deregister(bt_control); return error; }
    bt_control_uart=uart;
    bt_host_awake=false;
    thread_t thread;
    umi_bluetooth_set_ready(true);
    kern_return_t started=kernel_thread_start(bt_control_worker,NULL,&thread);
    if(started!=KERN_SUCCESS) { umi_bluetooth_set_ready(false); ctl_deregister(bt_control); return EIO; }
    thread_deallocate(thread);
    printf("UMI BT: persistent H4 endpoint ready: com.hakios.bluetooth\n");
    return 0;
}
/* Set the QUPv3 fractional root. GPLL0_OUT_EVEN is parent selector 6 and the
 * engine samples at 16x with its own divider at 4, so baud is 300 MHz * m / n / 64.
 * 384/15625 gives the documented 115200; 256/375 gives 204.8 MHz for 3.2 Mbaud. */
static bool bt_uart_clock(vm_offset_t gcc,uint32_t m,uint32_t n)
{
    bt_write(gcc,0x17738,m);
    bt_write(gcc,0x1773c,~(n-m)&0xffff);
    bt_write(gcc,0x17740,~n&0xffff);
    bt_write(gcc,0x17734,0x2601); /* dual-edge M/N, GPLL0-even, /1 */
    bt_write(gcc,0x17730,1);
    for(unsigned i=0;i<10000;i++) {
        if(!(bt_read(gcc,0x17730)&1)) return true;
        delay(1);
    }
    return false;
}
/* Discard whatever the engine and the H4 parser hold before a rate attempt: a
 * failed attempt makes the controller answer with bytes that frame as garbage. */
static void bt_rx_drain(vm_offset_t uart)
{
    for(unsigned i=0;i<64;i++) {
        uint32_t status=bt_read(uart,0x804),words=status&0x1ffffff;
        if(!words) break;
        while(words--) (void)bt_read(uart,0x780);
    }
    memset(&bt_rx_packet,0,sizeof(bt_rx_packet));
    bt_rx_head=bt_rx_tail=0; bt_ibs_ack_pending=false;
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
}
/* Two routes to the 51.2 MHz serial clock a 3.2 Mbaud link needs. The first
 * keeps the divider this port already runs, so only the root generator moves;
 * the second is the split the OnePlus 8 reference sets. */
static int bt_baud_fast(vm_offset_t uart,vm_offset_t gcc)
{
    static const struct { uint32_t m,n,cfg; } rates[] = {
        {128,375,0x21}, /* 102.4 MHz, divider 2 */
        {256,375,0x41}, /* 204.8 MHz, divider 4 */
        { 64,375,0x11}, /*  51.2 MHz, divider 1 */
    };
    uint8_t event[BT_EVENT_MAX]; unsigned length=0;
    for(unsigned i=0;i<sizeof(rates)/sizeof(rates[0]);i++) {
        bt_rx_drain(uart);
        bt_write(uart,0x48,rates[i].cfg); bt_write(uart,0x4c,rates[i].cfg);
        if(!bt_uart_clock(gcc,rates[i].m,rates[i].n)) {
            printf("UMI BT: root %u/%u did not lock\n",rates[i].m,rates[i].n);
            continue;
        }
        int error=bt_hci_command(uart,0x1001,NULL,0,true,event,&length);
        if(!error) {
            bt_link_speed=BT_BAUD_FAST;
            printf("UMI BT: link at 3.2 Mbaud: root %u/%u cfg %#x GENI %#x/%#x GCC %#x/%#x/%#x\n",
                rates[i].m,rates[i].n,rates[i].cfg,bt_read(uart,0x48),bt_read(uart,0x4c),
                bt_read(gcc,0x17738),bt_read(gcc,0x1773c),bt_read(gcc,0x17740));
            return 0;
        }
        printf("UMI BT: no reply at 3.2 Mbaud with root %u/%u cfg %#x errno=%d\n",
            rates[i].m,rates[i].n,rates[i].cfg,error);
    }
    printf("UMI BT: 3.2 Mbaud failed: GENI %#x/%#x GCC %#x/%#x/%#x\n",
        bt_read(uart,0x48),bt_read(uart,0x4c),
        bt_read(gcc,0x17738),bt_read(gcc,0x1773c),bt_read(gcc,0x17740));
    bt_baud_slow(uart,gcc);
    return ETIMEDOUT;
}
static void bt_baud_slow(vm_offset_t uart,vm_offset_t gcc)
{
    bt_write(uart,0x48,0x41); bt_write(uart,0x4c,0x41);
    bt_uart_clock(gcc,384,15625);
    bt_link_speed=BT_BAUD_SLOW;
}
/* Ask the controller for 3.2 Mbaud with the vendor command, which it answers at
 * the present rate. A no-op first tells us whether the command exists: if it
 * does not, nothing moves and the link stays exactly as proven. */
static int bt_baud_upgrade(vm_offset_t uart,vm_offset_t gcc)
{
    uint8_t event[BT_EVENT_MAX]; unsigned length=0;
    const uint8_t keep=0x00;
    int status=bt_hci_command(uart,0xfc48,&keep,1,true,event,&length);
    printf("UMI BT: 0xfc48 no-op (115200) status=%d length=%u\n",status,length);
    if(status) {
        printf("UMI BT: vendor rate command unavailable, staying at 115200\n");
        return 1;
    }
    const uint8_t fast=0x11;
    status=bt_hci_command(uart,0xfc48,&fast,1,true,event,&length);
    printf("UMI BT: 0xfc48 3200000 status=%d length=%u\n",status,length);
    if(status) {
        printf("UMI BT: rate change refused, staying at 115200\n");
        return 1;
    }
    delay(20000); /* the controller switches once that acknowledgement is out */
    if(bt_baud_fast(uart,gcc)) {
        printf("UMI BT: link lost after the rate change\n");
        return -1;
    }
    return 0;
}
static void bt_gpio(vm_offset_t gpio,unsigned pin,unsigned cfg,bool high)
{
    bt_write(gpio,pin*0x1000+4,high?2:0);
    bt_write(gpio,pin*0x1000,cfg);
}
static int bt_uart_quiesce(vm_offset_t uart)
{
    for (unsigned attempt=0; attempt<2; attempt++) {
        uint32_t active=bt_read(uart,0x40);
        if (!(active & 0x1001)) break;
        if (active & 1) bt_write(uart,0x604,attempt?2:4);
        if (active & 0x1000) bt_write(uart,0x634,attempt?2:4);
        for (unsigned t=0; t<10000 && (bt_read(uart,0x40)&0x1001); t++) delay(1);
    }
    if (bt_read(uart,0x40)&0x1001) return ETIMEDOUT;
    unsigned depth=(bt_read(uart,0xe28)>>16)&0x3f;
    unsigned words=bt_read(uart,0x804)&0x1ffffff;
    if (!depth || words>depth) return EIO;
    while(words--) (void)bt_read(uart,0x780);
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    return 0;
}
/* Bring the controller back from a rate the host could not follow: reset it as
 * the stock power sequence does, give the engine its 115200 divider back and
 * restart the H4 read command before the cold handshake. */
static int bt_link_restart(vm_offset_t uart,vm_offset_t gpio)
{
    uint8_t event[BT_EVENT_MAX]; unsigned length=0;
    bt_gpio(gpio,21,0x241,false);
    delay(56000);
    bt_gpio(gpio,21,0x241,true);
    delay(56000);
    (void)bt_uart_quiesce(uart);
    bt_write(uart,0x48,0x41); bt_write(uart,0x4c,0x41);
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    bt_write(uart,0x630,(1u<<27)|0x80);
    memset(&bt_rx_packet,0,sizeof(bt_rx_packet));
    bt_rx_head=bt_rx_tail=0; bt_host_awake=false; bt_ibs_ack_pending=false;
    return bt_hci_command(uart,0x0c03,NULL,0,true,event,&length);
}
/* Power the radio, issue HCI Reset and Read Local Version, then leave reset
 * asserted. Firmware download is a separate step after this ROM handshake. */
static bool bt_hci_probe(vm_offset_t gcc,vm_offset_t gpio,vm_offset_t uart,bool firmware,bool runtime)
{
    uint32_t root[5]={bt_read(gcc,0x17730),bt_read(gcc,0x17734),bt_read(gcc,0x17738),bt_read(gcc,0x1773c),bt_read(gcc,0x17740)};
    uint32_t pin21=bt_read(gpio,21*0x1000),io21=bt_read(gpio,21*0x1000+4),pin124=bt_read(gpio,124*0x1000),io124=bt_read(gpio,124*0x1000+4);
    uint32_t pins[4]; for(unsigned p=0;p<4;p++) pins[p]=bt_read(gpio,(16+p)*0x1000);
    static const uint32_t uregs[]={0x48,0x4c,0x22c,0x258,0x25c,0x260,0x264,0x268,0x26c,0x270,0x280,0x284,0x288,0x28c,0x294,0x2a4,0x2a8,0x80c,0x810,0x814};
    uint32_t uvals[sizeof(uregs)/sizeof(uregs[0])];
    for(unsigned p=0;p<sizeof(uvals)/sizeof(uvals[0]);p++) uvals[p]=bt_read(uart,uregs[p]);
    uint32_t vote=bt_read(gcc,0x52008),mask=(1u<<6)|(1u<<7)|(1u<<16);
    uint32_t hwcg=bt_read(gcc,0x17008)&2u, added=mask&~vote;
    bt_write(gcc,0x17008,bt_read(gcc,0x17008)&~2u);
    bt_write(gcc,0x52008,vote|mask);
    unsigned i;
    for(i=0;i<10000;i++) if(!((bt_read(gcc,0x17004)|bt_read(gcc,0x17008)|bt_read(gcc,0x1772c))&0x80000000)) break; else delay(1);
    int error= i==10000 ? ETIMEDOUT : bt_uart_quiesce(uart);
    if(!error && !bt_uart_clock(gcc,384,15625)) error=ETIMEDOUT;
    if(!error) error=umi_rpmh_bluetooth_power();
    if(error) { printf("UMI BT: power/clock setup failed errno=%d\n",error); goto cleanup; }
    /* Reset low, sideband awake, then release reset as the Android driver does. */
    bt_gpio(gpio,21,0x241, false); /* GPIO, 4 mA, pull-down, output */
    bt_gpio(gpio,124,0x240, true); /* GPIO, 4 mA, no pull, output */
    delay(56000); /* stock bt_power holds reset low for about 56 ms */
    bt_gpio(gpio,21,0x241, true);
    delay(56000);
    /* UART pins 16..19 use QUP6 alternate function 1. */
    for(unsigned pin=16;pin<=19;pin++) {
        uint32_t cfg=pin==17 ? 5u : (pin==18 ? 7u : 4u);
        bt_write(gpio,pin*0x1000,cfg);
    }
    bt_write(uart,0x48,0x41); bt_write(uart,0x4c,0x41);
    bt_write(uart,0x258,0); bt_write(uart,0x22c,0);
    bt_write(uart,0x25c,2); bt_write(uart,0x280,0);
    bt_write(uart,0x260,0xf); bt_write(uart,0x264,0);
    bt_write(uart,0x284,0x4380e); bt_write(uart,0x288,0xc3e0e);
    bt_write(uart,0x268,8); bt_write(uart,0x28c,8); bt_write(uart,0x26c,0);
    bt_write(uart,0x294,160); bt_write(uart,0x2a4,0); bt_write(uart,0x2a8,0);
    bt_write(uart,0x80c,1); bt_write(uart,0x810,0); bt_write(uart,0x814,6);
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    bt_write(uart,0x630,(1u<<27)|0x80); /* START_READ, RFR_OPEN */
    memset(&bt_rx_packet,0,sizeof(bt_rx_packet));
    bt_rx_head=bt_rx_tail=0; bt_host_awake=false; bt_ibs_ack_pending=false; bt_ibs_wakes=bt_ibs_sleeps=0;
    uint8_t event[BT_EVENT_MAX]; unsigned length=0;
    error=bt_hci_command(uart,0x0c03,NULL,0,true,event,&length);
    if(!error) error=bt_hci_command(uart,0x1001,NULL,0,true,event,&length);
    if(!error && length==15) {
        printf("UMI BT: cold HCI=%#x manufacturer=%u LMP subversion=%#x\n",
            event[7],bt_le16(event+11),bt_le16(event+13));
        if(firmware) {
            error=bt_qca_firmware(uart,gcc,gpio,true);
            if(error==EAGAIN) {
                printf("UMI BT: restarting the link at 115200 after the 3.2 Mbaud attempt\n");
                bt_link_speed=BT_BAUD_SLOW;
                error=bt_link_restart(uart,gpio);
                if(!error) error=bt_qca_firmware(uart,gcc,gpio,false);
            }
            if(!error && !runtime) error=bt_radio_scan(uart);
            if(!error && runtime) {
                error=bt_control_start(uart);
                if(!error) return true;
            }
        }
    } else if(!error) error=EILSEQ;
    printf("UMI BT: %s probe result=%d\n",firmware?"firmware/radio":"ROM HCI",error);
    bt_gpio(gpio,21,0x241,false);
    int stopped=bt_uart_quiesce(uart);
    printf("UMI BT: UART shutdown result=%d\n",stopped);
cleanup:
    for(unsigned p=0;p<sizeof(uvals)/sizeof(uvals[0]);p++) bt_write(uart,uregs[p],uvals[p]);
    bt_write(uart,0x618,0xffffffff); bt_write(uart,0x648,0xffffffff);
    bt_write(gpio,21*0x1000+4,io21&2); bt_write(gpio,21*0x1000,pin21);
    bt_write(gpio,124*0x1000+4,io124&2); bt_write(gpio,124*0x1000,pin124);
    for(unsigned p=0;p<4;p++) bt_write(gpio,(16+p)*0x1000,pins[p]);
    bt_write(gcc,0x17738,root[2]); bt_write(gcc,0x1773c,root[3]); bt_write(gcc,0x17740,root[4]);
    bt_write(gcc,0x17734,root[1]); bt_write(gcc,0x17730,root[0]|1);
    for(i=0;i<10000 && (bt_read(gcc,0x17730)&1);i++) delay(1);
    if(added) bt_write(gcc,0x52008,bt_read(gcc,0x52008)&~added);
    bt_write(gcc,0x17008,(bt_read(gcc,0x17008)&~2u)|hwcg);
    return false;
}
void umi_bluetooth_init(void)
{
    uint32_t probe = 0, enabled = 0;
    bool runtime = PE_parse_boot_argn("umi_bt", &enabled, sizeof(enabled)) && enabled == 1;
    bool diagnostic = PE_parse_boot_argn("umi_bt_probe", &probe, sizeof(probe));
    if (runtime && diagnostic) { printf("UMI BT: choose runtime or diagnostic mode\n"); return; }
    if (runtime) probe = 5;
    else if (!diagnostic || probe < 1 || probe > 5) return;
    if (!umi_wifi_is_board()) { printf("UMI BT: board identity mismatch\n"); return; }
    vm_offset_t gcc = ml_io_map(0x00100000, 0x80000);
    vm_offset_t gpio = ml_io_map(0x0f900000, 0x80000);
    if (!gcc || !gpio) { printf("UMI BT: GCC/TLMM mapping failed\n"); if(!runtime) loader_return(); return; }
    static const uint32_t clocks[] = {0x52008, 0x17004, 0x17008, 0x1772c, 0x17730, 0x17734, 0x17738, 0x1773c, 0x17740};
    for (unsigned i = 0; i < sizeof(clocks)/sizeof(clocks[0]); ++i)
        printf("UMI BT: GCC %#x=%#x\n", clocks[i], bt_read(gcc, clocks[i]));
    static const unsigned pins[] = {16,17,18,19,21,124};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i)
        printf("UMI BT: GPIO%u cfg=%#x io=%#x\n", pins[i], bt_read(gpio,pins[i]*0x1000), bt_read(gpio,pins[i]*0x1000+4));
    uint32_t added_votes = 0, saved_slave_hwcg = 0;
    if (probe >= 2) {
        uint32_t vote = bt_read(gcc, 0x52008), mask = (1u << 6) | (1u << 7) | (1u << 16);
        added_votes = mask & ~vote;
        saved_slave_hwcg = bt_read(gcc,0x17008) & 2u;
        bt_write(gcc,0x17008,bt_read(gcc,0x17008) & ~2u);
        bt_write(gcc, 0x52008, vote | mask);
        unsigned waited;
        for (waited = 0; waited < 10000; ++waited) {
            if (!((bt_read(gcc,0x17004) | bt_read(gcc,0x17008) | bt_read(gcc,0x1772c)) & 0x80000000)) break;
            delay(1);
        }
        printf("UMI BT: temporary UART clock vote: %u us, CBCR=%#x/%#x/%#x\n", waited,
              bt_read(gcc,0x17004),bt_read(gcc,0x17008),bt_read(gcc,0x1772c));
    }
    /* Avoid unclocked GENI reads, which can stall the interconnect. */
    if ((bt_read(gcc,0x17004) | bt_read(gcc,0x17008) | bt_read(gcc,0x1772c)) & 0x80000000) {
        printf("UMI BT: UART clocks halted; UART MMIO skipped\n");
    } else {
        vm_offset_t uart = ml_io_map(0x00998000, 0x4000);
        static const uint32_t regs[] = {0x40,0x48,0x4c,0x64,0x68,0x6c,0x7c,0x258,0x610,0x640,0x800,0x804,0xe24,0xe28};
        if (uart) for (unsigned i=0; i<sizeof(regs)/sizeof(regs[0]); ++i)
            printf("UMI BT: GENI %#x=%#x\n",regs[i],bt_read(uart,regs[i]));
        if (uart && probe == 3) bt_loopback(uart);
        if (uart && probe >= 4 && bt_hci_probe(gcc,gpio,uart,probe==5,runtime)) return;
    }
    if (probe >= 2) {
        bt_write(gcc,0x17008,(bt_read(gcc,0x17008) & ~2u) | saved_slave_hwcg);
        if (added_votes) bt_write(gcc,0x52008,bt_read(gcc,0x52008) & ~added_votes);
    }
    printf("UMI BT: %s finished\n",runtime?"startup":"diagnostic");
    if(!runtime) loader_return();
}

/* (cmi) bsd_init starts this instead of calling umi_bluetooth_init() inline: the firmware upload
 * blocks for as long as 188 KB take at 115200 baud, which delayed the audio DSP past the point
 * iOS waits for it. */
void
umi_bluetooth_thread(void *arg, wait_result_t wres)
{
    (void)arg;
    (void)wres;
    umi_bluetooth_init();
    thread_terminate(current_thread());
}
