#ifndef SIGNETDEV_PRIV_H
#include "../common/signetdev_common.h"
#include "../common/signetdev_common_priv.h"
#include "signetdev.h"

void signetdev_priv_platform_init();
void signetdev_priv_platform_deinit();
void signetdev_priv_handle_error();
void signetdev_priv_handle_command_resp(void *user, int token, int dev_cmd, int api_cmd, int resp_code, const u8 *resp, unsigned int resp_len, int end_device_state, int expected_messages_remaining);
void signetdev_priv_handle_device_event(int event_type, const u8 *resp, int resp_len);

enum signetdev_commands {
	SIGNETDEV_CMD_OPEN,
	SIGNETDEV_CMD_CANCEL_OPEN,
	SIGNETDEV_CMD_CLOSE,
	SIGNETDEV_CMD_QUIT,
	SIGNETDEV_CMD_MESSAGE,
	SIGNETDEV_CMD_CANCEL_MESSAGE,
	SIGNETDEV_CMD_FD_ATTACHED,
	SIGNETDEV_CMD_FD_DETACHED,
	SIGNETDEV_CMD_RESET_CONNECTION,
	SIGNETDEV_CMD_HAS_KEYBOARD,
	SIGNETDEV_CMD_EMULATE_BEGIN,
	SIGNETDEV_CMD_EMULATE_END
};

struct send_message_req {
	int dev_cmd;
	int api_cmd;
	u8 *payload;
	unsigned int payload_size;
	unsigned int messages_remaining;
	u8 *resp;
	int *resp_code;
	void *user;
	int token;
	int interrupt;
	int end_device_state;
	struct send_message_req *next;
};

struct attach_message {
	int fd;
	int rx_endpoint;
	int tx_endpoint;
	int has_keyboard;
};

struct tx_message_state {
	u8 msg_buf[CMD_PACKET_BUF_SIZE];
	u8 packet_buf[RAW_HID_PACKET_SIZE + 1];
	unsigned int msg_size;
	unsigned int msg_packet_seq;
	unsigned int msg_packet_count;
	struct send_message_req *message;
};

struct rx_message_state {
        unsigned int expected_resp_size;
	int expected_messages_remaining;
	int resp_code;
	int resp_buffer[CMD_PACKET_BUF_SIZE];
	struct send_message_req *message;
};

void signetdev_priv_prepare_message_state(struct tx_message_state *msg, unsigned int dev_cmd, unsigned int messages_remaining, u8 *payload, unsigned int payload_size);
void signetdev_priv_advance_message_state(struct tx_message_state *msg);

extern void (*g_device_opened_cb)(void *);
extern void *g_device_opened_cb_param;

extern void (*g_device_closed_cb)(void *);
extern void *g_device_closed_cb_param;

#define SIGNETDEV_PRIV_GET_RESP 1
#define SIGNETDEV_PRIV_NO_RESP 0

int signetdev_priv_send_message(void *user, int token,  int dev_cmd, int api_cmd, unsigned int messages_remaining, const u8 *payload, unsigned int payload_size, int get_resp);
void signetdev_priv_message_send_resp(struct send_message_req *msg, int rc, int expected_messages_remaining);
void signetdev_priv_free_message(struct send_message_req **req);
void signetdev_priv_finalize_message(struct send_message_req **msg ,int rc);
void signetdev_priv_process_rx_packet(struct rx_message_state *state, u8 *rx_packet_buf);
int signetdev_priv_cancel_message(int dev_cmd, const u8 *payload, unsigned int payload_size);

void signetdev_priv_issue_command_no_resp(int command, void *p);
int signetdev_priv_issue_command(int command, void *p);

int signetdev_emulate_handle_message_priv(struct send_message_req *msg);

extern signetdev_conn_err_t g_error_handler;
extern void *g_error_handler_param;

#endif
